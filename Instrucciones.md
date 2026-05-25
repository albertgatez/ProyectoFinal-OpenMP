# ProyectoFinal-OpenMP

## 1. Instrucciones del Proyecto

1\. **Generación del Código Base**: Utiliza una IA generativa para generar un programa en C o C++ puramente secuencial que realice las siguientes dos tareas: 
- Tarea A: Generar una imagen de ultra-alta resolución (ej. 8K) del Conjunto de Mandelbrot o un fractal similar.
- Tarea B: Aplicar un filtro de convolución 2D pesado (ej. un desenfoque Gaussiano de radio amplio o un filtro Sobel) sobre la imagen generada. 

2\. **Línea Base Paralela**: Pídele a la IA que paralelize este código secuencial usando OpenMP. Guarda este código. Esta será tu ”Línea Base Paralela de la IA”. 

3\. **Balanceo de Carga (Schedulers)**: Evalúa el tiempo que toma la Tarea A con el planificador por defecto (static) y compara empíricamente los planificadores dynamic y guided para determinar cuál es el planificador y tama˜no de bloque (chunk size) óptimo para tu procesador. 

4\. **Sincronización y Falsos Compartimientos**: 
- Agrega una funcionalidad que calcule un histograma de colores de la imagen final (contar cuántos píxeles hay de cada color). 
- Compara una implementación que use exclusión mutua (#pragma omp atomic o critical) contra una que use variables estrictamente locales (o la cláusula reduction). 
- Documenta si se presentó el fenómeno de False Sharing al intentar guardar datos en arreglos compartidos. 

5\. **SPMD y Afinidad**:
- Modifica los bucles más internos del filtro de convolución (Tarea B) para forzar la vectorización utilizando la estructura SPMD. Verifica con las banderas de optimización de tu compilador que el código realmente se vectorizó.
- 10 pts. extra: Utiliza variables de entorno (OMP PROC BIND y OMP PLACES) para anclar los hilos a núcleos físicos específicos y medir si esto mejora el uso de la memoria caché L1/L2.

## 2. Entregables

2.1. **Repositorio de Código (Git/GitHub)**
Enlace al control de versiones con el código implementado. El historial de commits debe ser claro, comenzando obligatoriamente por el código secuencial base, seguido de la propuesta de paralelización original obtenida mediante IA, y finalmente los commits con las optimizaciones manuales de OpenMP. 

2.2. **Características del Reporte Técnico** 
El reporte deberá incluir, de forma clara y concisa, los siguientes elementos:
1. Especificaciones del Hardware de Prueba: Modelo de CPU, número de núcleos físicos vs. 
lógicos, y tamaño de memoria caché L1/L2/L3 y RAM. 
2. Sección de Metodología: Los prompts exactos utilizados para generar el código base, y un análisis de los errores lógicos o cuellos de botella de rendimiento detectados en el código inicial de la IA. 
3. Gráfica de Tiempo de Ejecución vs. Número de Hilos (evaluando desde 1 hilo hasta el doble de núcleos lógicos).
4. Gráfica de Aceleración (Speedup) vs. Número de Hilos.
5. Análisis de Rendimiento: Identificación clara y justificada en las gráficas del límite impuesto por la Ley de Amdahl y el punto exacto donde el overhead del sistema operativo degrada el rendimiento.
6. Conclusiones.