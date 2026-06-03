#ifndef SIM_MATH_H
#define SIM_MATH_H

/* ═══════════════════════════════════════════════════════
   SIM_MATH — Simulacao matematica densa (fp=100%)
   Monte Carlo Pi  +  Mandelbrot 2D
   Compativel MSVC e GCC, com e sem OpenMP
═══════════════════════════════════════════════════════ */

typedef struct {
    char   algoritmo[32]; /* "montecarlo", "mandelbrot" */
    char   modo[16];      /* "serial", "openmp", "gpu_sim", "cuda" */
    int    volume;
    int    threads;
    double t_segundos;
    double speedup;
    double valor;         /* pi_est para montecarlo, total_iter para mandelbrot */
} MathResult;

/* ───────────────────────────────────────────────────────
   Monte Carlo pi — N amostras aleatorias, fp=100%
─────────────────────────────────────────────────────── */
double montecarlo_serial (long n);
double montecarlo_openmp (long n, int threads);
double montecarlo_gpu_sim(long n);

/* ───────────────────────────────────────────────────────
   Mandelbrot 2D — N pixels na grade complexa [-2,1]x[-1.5,1.5]
   max_iter=256, fp=100%
   Retorna total de iteracoes (long) convertido para double
─────────────────────────────────────────────────────── */
double mandelbrot_serial (int n);
double mandelbrot_openmp (int n, int threads);
double mandelbrot_gpu_sim(int n);

#endif /* SIM_MATH_H */
