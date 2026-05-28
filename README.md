# ProyectoFinal-OpenMP

El programa fue ejecutado en una Macbook M1.
Se optó por utilizar OpenMP que viene con gcc instalado directamente desde Homebrew, por lo que cada vez que se quería compilar se hacía uso de la bandera `-fopenmp`, más concretamente `g++-15 -O2 -fopenmp archivo.cpp -o ejecutable`.
Cuando se vectoriza el código, se agrega la bandera `-fopt-info-vec-optimized` y se cambia `-O2` por `-O3`.
