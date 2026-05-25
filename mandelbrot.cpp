/*
 * ============================================================
 *  mandelbrot_pipeline.cpp  —  Programa secuencial en C++
 * ============================================================
 *  Tarea A: Genera una imagen 8K (7680 × 4320) del Conjunto
 *           de Mandelbrot con coloración suavizada (smooth
 *           coloring / escape-time continuo).
 *
 *  Tarea B: Aplica un filtro Gaussiano 2D de radio amplio
 *           (radio = 21 píxeles, σ = 7) sobre la imagen para
 *           producir un desenfoque Gaussiano profundo.
 *           A continuación aplica un filtro Sobel para
 *           detectar bordes en la imagen desenfocada.
 *
 *  Salida (formato PPM binario, sin dependencias externas):
 *    mandelbrot_8k.ppm          ← imagen original (Tarea A)
 *    mandelbrot_blurred.ppm     ← Gaussiano aplicado (Tarea B-1)
 *    mandelbrot_sobel.ppm       ← Sobel aplicado (Tarea B-2)
 *
 *  Compilación:
 *    g++ -O2 -std=c++17 -o mandelbrot_pipeline mandelbrot_pipeline.cpp
 *
 *  Ejecución (estimado ~5–20 min en CPU moderna):
 *    ./mandelbrot_pipeline
 * ============================================================
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

// ──────────────────────────────────────────────────────────────
//  Constantes de configuración
// ──────────────────────────────────────────────────────────────

// Resolución 8K UHD
static constexpr int WIDTH  = 7680;
static constexpr int HEIGHT = 4320;

// Región del plano complejo a renderizar
static constexpr double X_MIN = -2.5;
static constexpr double X_MAX =  1.0;
static constexpr double Y_MIN = -1.25;
static constexpr double Y_MAX =  1.25;

// Iteraciones máximas (mayor → más detalle, más tiempo)
static constexpr int MAX_ITER = 1024;

// Radio y sigma del filtro Gaussiano
static constexpr int    GAUSS_RADIUS = 21;   // (2R+1)² = 43×43 kernel
static constexpr double GAUSS_SIGMA  = 7.0;

// ──────────────────────────────────────────────────────────────
//  Tipos auxiliares
// ──────────────────────────────────────────────────────────────

struct RGB { uint8_t r, g, b; };

using Image = std::vector<RGB>;   // almacenamiento row-major

// ──────────────────────────────────────────────────────────────
//  Utilidades de tiempo
// ──────────────────────────────────────────────────────────────

using Clock     = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

static TimePoint now() { return Clock::now(); }

static double elapsed_s(TimePoint t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

// ──────────────────────────────────────────────────────────────
//  E/S de imagen PPM (binario P6)
// ──────────────────────────────────────────────────────────────

static void save_ppm(const std::string& path, const Image& img, int w, int h) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { std::cerr << "Error al abrir: " << path << "\n"; return; }
    f << "P6\n" << w << " " << h << "\n255\n";
    f.write(reinterpret_cast<const char*>(img.data()),
            static_cast<std::streamsize>(w * h * sizeof(RGB)));
    std::cout << "  Guardado: " << path
              << "  (" << (w * h * 3 / 1'000'000.0) << " MB)\n";
}

// ──────────────────────────────────────────────────────────────
//  Paleta de color: mapea valor continuo [0,1] → RGB
//  Usa una paleta cíclica estilo «Ultra Fractal»
// ──────────────────────────────────────────────────────────────

static RGB palette(double t) {
    // Función de suavizado cúbico
    auto smooth = [](double x) {
        return x * x * (3.0 - 2.0 * x);
    };

    // 5 puntos de control en el espacio de color (HSB → RGB inline)
    struct Stop { double pos; double r, g, b; };

    static constexpr std::array<Stop, 6> stops = {{
        {0.000, 0.000, 0.027, 0.392},   // azul profundo
        {0.160, 0.125, 0.420, 0.796},   // azul cielo
        {0.420, 0.929, 0.929, 0.929},   // blanco plateado
        {0.643, 1.000, 0.667, 0.000},   // dorado
        {0.857, 0.098, 0.027, 0.102},   // rojo oscuro
        {1.000, 0.000, 0.027, 0.392},   // cierre = inicio
    }};

    t = t - std::floor(t);   // cíclico

    int i = 0;
    while (i + 1 < static_cast<int>(stops.size()) - 1 &&
           t > stops[i + 1].pos) ++i;

    const Stop& a = stops[i];
    const Stop& b = stops[i + 1];
    double span = b.pos - a.pos;
    double local = (span > 1e-12) ? (t - a.pos) / span : 0.0;
    local = smooth(local);

    auto lerp = [&](double va, double vb) {
        return va + local * (vb - va);
    };

    return {
        static_cast<uint8_t>(std::clamp(lerp(a.r, b.r), 0.0, 1.0) * 255.0 + 0.5),
        static_cast<uint8_t>(std::clamp(lerp(a.g, b.g), 0.0, 1.0) * 255.0 + 0.5),
        static_cast<uint8_t>(std::clamp(lerp(a.b, b.b), 0.0, 1.0) * 255.0 + 0.5)
    };
}

// ──────────────────────────────────────────────────────────────
//  TAREA A: Renderizado del Conjunto de Mandelbrot
//  Algoritmo: escape-time con smooth coloring (Bernard&Levin)
//    nu = n - log2(log2|z|)          continuo, sin bandas
// ──────────────────────────────────────────────────────────────

static Image task_a_mandelbrot() {
    std::cout << "\n╔══════════════════════════════════════════╗\n";
    std::cout <<   "║  TAREA A — Mandelbrot 8K (" << WIDTH << "×" << HEIGHT << ")  ║\n";
    std::cout <<   "╚══════════════════════════════════════════╝\n";

    Image img(WIDTH * HEIGHT);
    TimePoint t0 = now();

    const double dx = (X_MAX - X_MIN) / WIDTH;
    const double dy = (Y_MAX - Y_MIN) / HEIGHT;

    // Barra de progreso simple (por filas)
    int progress_step = HEIGHT / 20;

    for (int py = 0; py < HEIGHT; ++py) {
        if (py % progress_step == 0) {
            int pct = py * 100 / HEIGHT;
            std::cout << "\r  Progreso: [";
            int filled = pct / 5;
            for (int k = 0; k < 20; ++k)
                std::cout << (k < filled ? "#" : "-");
            std::cout << "] " << std::setw(3) << pct << "%  " << std::fixed << std::setprecision(1) << elapsed_s(t0) << "s" << std::flush;
        }

        const double cy = Y_MIN + (py + 0.5) * dy;

        for (int px = 0; px < WIDTH; ++px) {
            const double cx = X_MIN + (px + 0.5) * dx;

            // Iteración de Mandelbrot z_{n+1} = z_n² + c
            double zx = 0.0, zy = 0.0;
            int n = 0;

            // Test de cardioide / período-2 (optimización clásica)
            {
                double q = (cx - 0.25) * (cx - 0.25) + cy * cy;
                if (q * (q + (cx - 0.25)) < 0.25 * cy * cy) {
                    img[py * WIDTH + px] = {0, 0, 0}; continue;
                }
                if ((cx + 1.0) * (cx + 1.0) + cy * cy < 0.0625) {
                    img[py * WIDTH + px] = {0, 0, 0}; continue;
                }
            }

            while (n < MAX_ITER) {
                double zx2 = zx * zx, zy2 = zy * zy;
                if (zx2 + zy2 > 256.0) break;   // radio de escape grande
                zy = 2.0 * zx * zy + cy;
                zx = zx2 - zy2 + cx;
                ++n;
            }

            if (n == MAX_ITER) {
                img[py * WIDTH + px] = {0, 0, 0};
            } else {
                // Smooth coloring
                double log_zn   = 0.5 * std::log(zx * zx + zy * zy);
                double nu       = std::log(log_zn / std::log(2.0)) / std::log(2.0);
                double smooth_n = n + 1.0 - nu;
                double t        = smooth_n / 64.0;  // ciclo de paleta
                img[py * WIDTH + px] = palette(t);
            }
        }
    }

    std::cout << "\r  Progreso: [████████████████████] 100%  " << std::fixed << std::setprecision(2) << elapsed_s(t0) << "s\n";
    std::cout << "  Tiempo Tarea A: " << elapsed_s(t0) << " s\n";

    save_ppm("mandelbrot_8k.ppm", img, WIDTH, HEIGHT);
    return img;
}

// ──────────────────────────────────────────────────────────────
//  TAREA B-1: Filtro Gaussiano 2D (convolución separable)
//  Se realiza en dos pasadas: horizontal y vertical.
//  Kernel: G(x) = exp(-x²/(2σ²)) normalizado.
// ──────────────────────────────────────────────────────────────

static Image task_b1_gaussian(const Image& src) {
    std::cout << "\n╔═══════════════════════════════════════════════╗\n";
    std::cout <<   "║  TAREA B-1 — Filtro Gaussiano (R=" << GAUSS_RADIUS << ", σ=" << GAUSS_SIGMA << ")  ║\n";
    std::cout <<   "╚═══════════════════════════════════════════════╝\n";

    TimePoint t0 = now();

    // ── Precalcular kernel 1D ──────────────────────────────────
    const int K = GAUSS_RADIUS;            // la mitad
    const int KSIZE = 2 * K + 1;
    std::vector<double> kernel(KSIZE);
    double ksum = 0.0;
    for (int i = -K; i <= K; ++i) {
        double v = std::exp(-0.5 * i * i / (GAUSS_SIGMA * GAUSS_SIGMA));
        kernel[i + K] = v;
        ksum += v;
    }
    for (auto& v : kernel) v /= ksum;   // normalizar

    // Buffer intermedio (float para acumular sin pérdida)
    using FBuf = std::vector<std::array<float, 3>>;
    FBuf tmp(WIDTH * HEIGHT);

    int progress_step = HEIGHT / 20;

    // ── Pasada horizontal ──────────────────────────────────────
    std::cout << "  Pasada horizontal...\n";
    for (int py = 0; py < HEIGHT; ++py) {
        if (py % progress_step == 0) {
            std::cout << "\r    Fila " << std::setw(5) << py << "/" << HEIGHT << "  " << std::fixed << std::setprecision(1) << elapsed_s(t0) << "s" << std::flush;
        }
        for (int px = 0; px < WIDTH; ++px) {
            double sr = 0, sg = 0, sb = 0;
            for (int k = -K; k <= K; ++k) {
                int qx = std::clamp(px + k, 0, WIDTH - 1);
                const RGB& p = src[py * WIDTH + qx];
                double w = kernel[k + K];
                sr += w * p.r;
                sg += w * p.g;
                sb += w * p.b;
            }
            tmp[py * WIDTH + px] = {
                static_cast<float>(sr),
                static_cast<float>(sg),
                static_cast<float>(sb)
            };
        }
    }
    std::cout << "\r    Pasada horizontal completada.                \n";

    // ── Pasada vertical ───────────────────────────────────────
    std::cout << "  Pasada vertical...\n";
    Image out(WIDTH * HEIGHT);
    for (int py = 0; py < HEIGHT; ++py) {
        if (py % progress_step == 0) {
            std::cout << "\r    Fila " << std::setw(5) << py << "/" << HEIGHT << "  " << std::fixed << std::setprecision(1) << elapsed_s(t0) << "s" << std::flush;
        }
        for (int px = 0; px < WIDTH; ++px) {
            double sr = 0, sg = 0, sb = 0;
            for (int k = -K; k <= K; ++k) {
                int qy = std::clamp(py + k, 0, HEIGHT - 1);
                const auto& p = tmp[qy * WIDTH + px];
                double w = kernel[k + K];
                sr += w * p[0];
                sg += w * p[1];
                sb += w * p[2];
            }
            out[py * WIDTH + px] = {
                static_cast<uint8_t>(std::clamp(sr, 0.0, 255.0)),
                static_cast<uint8_t>(std::clamp(sg, 0.0, 255.0)),
                static_cast<uint8_t>(std::clamp(sb, 0.0, 255.0))
            };
        }
    }
    std::cout << "\r    Pasada vertical completada.                  \n";
    std::cout << "  Tiempo Tarea B-1: " << elapsed_s(t0) << " s\n";

    save_ppm("mandelbrot_blurred.ppm", out, WIDTH, HEIGHT);
    return out;
}

// ──────────────────────────────────────────────────────────────
//  TAREA B-2: Filtro Sobel (detección de bordes)
//  Aplica los kernels Gx y Gy sobre el canal de luminancia
//  y produce una imagen de magnitud de gradiente normalizada.
// ──────────────────────────────────────────────────────────────

static void task_b2_sobel(const Image& src) {
    std::cout << "\n╔════════════════════════════════════╗\n";
    std::cout <<   "║  TAREA B-2 — Filtro Sobel 3×3      ║\n";
    std::cout <<   "╚════════════════════════════════════╝\n";

    TimePoint t0 = now();

    //   Gx:                Gy:
    //  -1  0 +1           -1 -2 -1
    //  -2  0 +2            0  0  0
    //  -1  0 +1           +1 +2 +1

    // Convertir a luminancia (escala de grises)
    std::vector<float> gray(WIDTH * HEIGHT);
    for (int i = 0; i < WIDTH * HEIGHT; ++i) {
        const RGB& p = src[i];
        gray[i] = 0.299f * p.r + 0.587f * p.g + 0.114f * p.b;
    }

    Image out(WIDTH * HEIGHT);
    float max_mag = 0.0f;

    // Primera pasada: calcular magnitudes y guardar en float temporal
    std::vector<float> mag(WIDTH * HEIGHT, 0.0f);
    int progress_step = HEIGHT / 20;

    for (int py = 1; py < HEIGHT - 1; ++py) {
        if (py % progress_step == 0) {
            std::cout << "\r  Fila " << std::setw(5) << py << "/" << HEIGHT << "  " << std::fixed << std::setprecision(1) << elapsed_s(t0) << "s" << std::flush;
        }
        for (int px = 1; px < WIDTH - 1; ++px) {
            // Vecinos (acceso a grilla 3×3)
            float tl = gray[(py-1)*WIDTH + (px-1)];
            float tc = gray[(py-1)*WIDTH +  px    ];
            float tr = gray[(py-1)*WIDTH + (px+1)];
            float ml = gray[ py   *WIDTH + (px-1)];
            // float mc = gray[ py   *WIDTH +  px    ];   // no usado
            float mr = gray[ py   *WIDTH + (px+1)];
            float bl = gray[(py+1)*WIDTH + (px-1)];
            float bc = gray[(py+1)*WIDTH +  px    ];
            float br = gray[(py+1)*WIDTH + (px+1)];

            float gx = -tl - 2*ml - bl + tr + 2*mr + br;
            float gy = -tl - 2*tc - tr + bl + 2*bc + br;

            float m = std::sqrt(gx*gx + gy*gy);
            mag[py * WIDTH + px] = m;
            if (m > max_mag) max_mag = m;
        }
    }

    // Segunda pasada: normalizar y colorear
    float inv_max = (max_mag > 0.0f) ? (255.0f / max_mag) : 1.0f;
    for (int i = 0; i < WIDTH * HEIGHT; ++i) {
        auto v = static_cast<uint8_t>(std::clamp(mag[i] * inv_max, 0.0f, 255.0f));
        out[i] = {v, v, v};
    }

    std::cout << "\r  Filtro Sobel completado.                       \n";
    std::cout << "  Gradiente máximo = " << max_mag << "\n";
    std::cout << "  Tiempo Tarea B-2: " << elapsed_s(t0) << " s\n";

    save_ppm("mandelbrot_sobel.ppm", out, WIDTH, HEIGHT);
}

// ──────────────────────────────────────────────────────────────
//  main
// ──────────────────────────────────────────────────────────────

int main() {
    std::cout << "═══════════════════════════════════════════════════\n";
    std::cout << "  Pipeline Secuencial: Mandelbrot 8K + Convolución\n";
    std::cout << "═══════════════════════════════════════════════════\n";
    std::cout << "  Resolución  : " << WIDTH << " × " << HEIGHT << " px\n";
    std::cout << "  Max iter    : " << MAX_ITER << "\n";
    std::cout << "  Kernel Gauss: " << (2*GAUSS_RADIUS+1) << "×" << (2*GAUSS_RADIUS+1) << "  (σ=" << GAUSS_SIGMA << ")\n\n";

    TimePoint t_total = now();

    // ── Tarea A ───────────────────────────────────────────────
    Image mandelbrot = task_a_mandelbrot();

    // ── Tarea B-1 ─────────────────────────────────────────────
    Image blurred = task_b1_gaussian(mandelbrot);

    // ── Tarea B-2 ─────────────────────────────────────────────
    task_b2_sobel(blurred);

    // ── Resumen ───────────────────────────────────────────────
    std::cout << "\n╔════════════════════════════════════════════════╗\n";
    std::cout <<   "║  PIPELINE COMPLETADO                           ║\n";
    std::cout <<   "╚════════════════════════════════════════════════╝\n";
    std::cout << "  Tiempo total: " << std::fixed << std::setprecision(2) << elapsed_s(t_total) << " s\n\n";
    std::cout << "  Archivos generados:\n";
    std::cout << "    mandelbrot_8k.ppm      ← Conjunto de Mandelbrot\n";
    std::cout << "    mandelbrot_blurred.ppm ← Desenfoque Gaussiano\n";
    std::cout << "    mandelbrot_sobel.ppm   ← Bordes Sobel\n\n";

    return 0;
}
