/* Suprime avisos de funcoes "inseguras" do MSVC */
#ifdef _MSC_VER
#  define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _MSC_VER
#  define _POSIX_C_SOURCE 199309L
#endif

#include <stdint.h>
#include <math.h>
#ifdef _OPENMP
#  include <omp.h>
#endif
#include "sim_math.h"

/* ═══════════════════════════════════════════════════════
   LCG thread-safe — compativel MSVC+GCC
   Multiplicador e incremento de Knuth (The Art of Computer Programming)
═══════════════════════════════════════════════════════ */
static inline uint64_t lcg_next(uint64_t *s) {
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return *s;
}
/* Converte para double em [0, 1) usando os 53 bits superiores */
static inline double lcg_double(uint64_t *s) {
    return (double)(lcg_next(s) >> 11) * (1.0 / (double)(1ULL << 53));
}

/* ═══════════════════════════════════════════════════════
   MONTE CARLO PI
   Algoritmo: gera N pontos (x,y) em [0,1)^2 via LCG.
   Conta quantos caem dentro do quarto de circulo x^2+y^2 <= 1.
   pi_estimado = 4 * inside / N
═══════════════════════════════════════════════════════ */

double montecarlo_serial(long n) {
    uint64_t state = 0xDEADBEEFCAFEBABEULL;
    long inside = 0;
    for (long i = 0; i < n; i++) {
        double x = lcg_double(&state);
        double y = lcg_double(&state);
        if (x * x + y * y <= 1.0) inside++;
    }
    return 4.0 * (double)inside / (double)n;
}

double montecarlo_openmp(long n, int threads) {
#define MC_BASE_SEED   0xDEADBEEFCAFEBABEULL
#define MC_LARGE_PRIME 6364136223846793005ULL
    long inside = 0;
#ifdef _OPENMP
    #pragma omp parallel num_threads(threads) reduction(+:inside)
    {
        int tid = omp_get_thread_num();
        uint64_t state = MC_BASE_SEED ^ ((uint64_t)(unsigned)tid * MC_LARGE_PRIME);
        /* divide o trabalho por thread, cada uma com sua propria sequencia LCG */
        long local_n = n / (long)threads;
        if (tid == threads - 1) local_n = n - local_n * (long)(threads - 1);
        long local_inside = 0;
        for (long i = 0; i < local_n; i++) {
            double x = lcg_double(&state);
            double y = lcg_double(&state);
            if (x * x + y * y <= 1.0) local_inside++;
        }
        inside += local_inside;
    }
#else
    (void)threads;
    return montecarlo_serial(n);
#endif
    return 4.0 * (double)inside / (double)n;
}

/* ═══════════════════════════════════════════════════════
   MANDELBROT 2D
   Grade: side x side pixels sobre o plano complexo [-2,1] x [-1.5,1.5]
   Para cada pixel c = (c_re, c_im):
     z = 0; iter = 0;
     while (|z|^2 <= 4 && iter < max_iter) { z = z^2 + c; iter++; }
   Retorna a soma total de iteracoes de todos os pixels (double)
═══════════════════════════════════════════════════════ */

#define MANDEL_MAX_ITER 256

double mandelbrot_serial(int n) {
    int side = (int)sqrt((double)n);
    if (side < 1) side = 1;
    long total = 0;
    for (int py = 0; py < side; py++) {
        float c_im = -1.5f + (float)py * (3.0f / (float)side);
        for (int px = 0; px < side; px++) {
            float c_re = -2.0f + (float)px * (3.0f / (float)side);
            float z_re = 0.0f, z_im = 0.0f;
            int iter = 0;
            while (z_re * z_re + z_im * z_im <= 4.0f && iter < MANDEL_MAX_ITER) {
                float tmp = z_re * z_re - z_im * z_im + c_re;
                z_im = 2.0f * z_re * z_im + c_im;
                z_re = tmp;
                iter++;
            }
            total += iter;
        }
    }
    return (double)total;
}

double mandelbrot_openmp(int n, int threads) {
    int side = (int)sqrt((double)n);
    if (side < 1) side = 1;
    long total = 0;
#ifdef _OPENMP
    int py; // Declare aqui fora
    #pragma omp parallel for num_threads(threads) reduction(+:total) schedule(dynamic,4)
    for (py = 0; py < side; py++) {
        float c_im = -1.5f + (float)py * (3.0f / (float)side);
        long row_total = 0;
        for (int px = 0; px < side; px++) {
            float c_re = -2.0f + (float)px * (3.0f / (float)side);
            float z_re = 0.0f, z_im = 0.0f;
            int iter = 0;
            while (z_re * z_re + z_im * z_im <= 4.0f && iter < MANDEL_MAX_ITER) {
                float tmp = z_re * z_re - z_im * z_im + c_re;
                z_im = 2.0f * z_re * z_im + c_im;
                z_re = tmp;
                iter++;
            }
            row_total += iter;
        }
        total += row_total;
    }
#else
    (void)threads;
    return mandelbrot_serial(n);
#endif
    return (double)total;
}
