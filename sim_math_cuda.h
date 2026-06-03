#ifndef SIM_MATH_CUDA_H
#define SIM_MATH_CUDA_H

/* ═══════════════════════════════════════════════════════
   SIM_MATH_CUDA — kernels CUDA para simulacao matematica densa
   Compilado por nvcc, linkado com benchmark_cuda.exe
═══════════════════════════════════════════════════════ */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * montecarlo_cuda — estimativa de pi via Monte Carlo na GPU
 *   n          : numero de amostras
 *   t_kernel_s : saida do tempo do kernel CUDA puro (em segundos)
 *   retorna    : estimativa de pi
 */
double montecarlo_cuda(long n, double *t_kernel_s);

/*
 * mandelbrot_cuda — Mandelbrot 2D side x side pixels na GPU
 *   n          : numero total de pixels (side = sqrt(n))
 *   t_kernel_s : saida do tempo do kernel CUDA puro (em segundos)
 *   retorna    : total de iteracoes (double)
 */
double mandelbrot_cuda(int  n, double *t_kernel_s);

#ifdef __cplusplus
}
#endif

#endif /* SIM_MATH_CUDA_H */
