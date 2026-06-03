/*
 * sim_math_cuda.cu — Kernels CUDA para Monte Carlo Pi e Mandelbrot 2D
 *
 * Requer: CUDA Toolkit, nvcc, sm_75 (GTX 1650 / Turing)
 * Compilar via build_cuda.bat (incluido no nvcc invocation)
 */

#include <cuda_runtime.h>
#include <stdio.h>
#include <math.h>
#include "sim_math_cuda.h"

/* ═══════════════════════════════════════════════════════
   LCG 32-bit por thread — rapido, sem dependencias
   state = (uint32_t)(gid * 1664525u + 1013904223u) ^ 0xDEADBEEFu
═══════════════════════════════════════════════════════ */
__device__ static inline unsigned int lcg32_next(unsigned int *s) {
    *s = *s * 1664525u + 1013904223u;
    return *s;
}
__device__ static inline float lcg32_float(unsigned int *s) {
    return (float)(lcg32_next(s) >> 8) * (1.0f / (float)(1u << 24));
}

/* ═══════════════════════════════════════════════════════
   KERNEL MONTE CARLO
   Grid-stride loop + shared memory reduction
   Cada thread: loop com float, conta inside em variavel local
   Shared memory reduction -> atomicAdd(d_inside, sdata[0])
═══════════════════════════════════════════════════════ */
__global__ void kernel_montecarlo(long n, unsigned long long *d_inside) {
    extern __shared__ unsigned long long sdata[];

    int gid = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = gridDim.x * blockDim.x;
    int tid = threadIdx.x;

    /* semente por thread, derivada do gid global */
    unsigned int state = (unsigned int)(gid * 1664525u + 1013904223u) ^ 0xDEADBEEFu;

    unsigned long long local_inside = 0;
    /* grid-stride loop: cada thread processa multiplos elementos */
    for (long i = (long)gid; i < n; i += (long)stride) {
        float x = lcg32_float(&state);
        float y = lcg32_float(&state);
        if (x * x + y * y <= 1.0f) local_inside++;
    }

    /* armazena na shared memory */
    sdata[tid] = local_inside;
    __syncthreads();

    /* reducao em arvore na shared memory */
    for (int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (tid < s) sdata[tid] += sdata[tid + s];
        __syncthreads();
    }

    /* thread 0 de cada bloco faz atomicAdd no resultado global */
    /* atomicAdd suporta unsigned long long * (CC >= 1.1) */
    if (tid == 0) atomicAdd(d_inside, sdata[0]);
}

/* ═══════════════════════════════════════════════════════
   KERNEL MANDELBROT
   1 thread por pixel, float, max_iter=256
   atomicAdd(d_total, (long long)iter) — CC 7.5 suporta int64 atomicAdd
═══════════════════════════════════════════════════════ */
__global__ void kernel_mandelbrot(int side, unsigned long long *d_total) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total_pixels = side * side;
    if (idx >= total_pixels) return;

    int py = idx / side;
    int px = idx % side;

    float c_re = -2.0f + (float)px * (3.0f / (float)side);
    float c_im = -1.5f + (float)py * (3.0f / (float)side);
    float z_re = 0.0f, z_im = 0.0f;
    int iter = 0;
    const int MAX_ITER = 256;

    while (z_re * z_re + z_im * z_im <= 4.0f && iter < MAX_ITER) {
        float tmp = z_re * z_re - z_im * z_im + c_re;
        z_im = 2.0f * z_re * z_im + c_im;
        z_re = tmp;
        iter++;
    }
    /* atomicAdd com unsigned long long — suportado em CC >= 1.1 */
    atomicAdd(d_total, (unsigned long long)iter);
}

/* ═══════════════════════════════════════════════════════
   WRAPPER — montecarlo_cuda
═══════════════════════════════════════════════════════ */
extern "C"
double montecarlo_cuda(long n, double *t_kernel_s) {
    unsigned long long *d_inside = NULL;
    unsigned long long h_inside = 0;
    cudaError_t err;

    err = cudaMalloc(&d_inside, sizeof(unsigned long long));
    if (err != cudaSuccess) {
        fprintf(stderr, "[CUDA] montecarlo_cuda: cudaMalloc falhou: %s\n",
                cudaGetErrorString(err));
        if (t_kernel_s) *t_kernel_s = 0.0;
        return 3.14159265358979;
    }
    cudaMemset(d_inside, 0, sizeof(unsigned long long));

    const int BLOCK = 256;
    int grid = (int)((n + BLOCK - 1) / BLOCK);
    /* limita grid para evitar overhead excessivo em GPUs com poucos SMs */
    if (grid > 65535) grid = 65535;

    cudaEvent_t t_start, t_stop;
    cudaEventCreate(&t_start);
    cudaEventCreate(&t_stop);

    cudaEventRecord(t_start);
    kernel_montecarlo<<<grid, BLOCK, (size_t)BLOCK * sizeof(unsigned long long)>>>(n, d_inside);
    cudaEventRecord(t_stop);
    cudaEventSynchronize(t_stop);

    float ms = 0.0f;
    cudaEventElapsedTime(&ms, t_start, t_stop);
    if (t_kernel_s) *t_kernel_s = (double)ms / 1000.0;

    cudaEventDestroy(t_start);
    cudaEventDestroy(t_stop);

    err = cudaMemcpy(&h_inside, d_inside, sizeof(unsigned long long), cudaMemcpyDeviceToHost);
    cudaFree(d_inside);

    if (err != cudaSuccess) {
        fprintf(stderr, "[CUDA] montecarlo_cuda: cudaMemcpy falhou: %s\n",
                cudaGetErrorString(err));
        return 3.14159265358979;
    }

    return 4.0 * (double)h_inside / (double)n;
}

/* ═══════════════════════════════════════════════════════
   WRAPPER — mandelbrot_cuda
═══════════════════════════════════════════════════════ */
extern "C"
double mandelbrot_cuda(int n, double *t_kernel_s) {
    int side = (int)sqrt((double)n);
    if (side < 1) side = 1;
    int total_pixels = side * side;

    unsigned long long *d_total = NULL;
    unsigned long long  h_total = 0;
    cudaError_t err;

    err = cudaMalloc(&d_total, sizeof(unsigned long long));
    if (err != cudaSuccess) {
        fprintf(stderr, "[CUDA] mandelbrot_cuda: cudaMalloc falhou: %s\n",
                cudaGetErrorString(err));
        if (t_kernel_s) *t_kernel_s = 0.0;
        return 0.0;
    }
    cudaMemset(d_total, 0, sizeof(unsigned long long));

    const int BLOCK = 256;
    int grid = (total_pixels + BLOCK - 1) / BLOCK;

    cudaEvent_t t_start, t_stop;
    cudaEventCreate(&t_start);
    cudaEventCreate(&t_stop);

    cudaEventRecord(t_start);
    kernel_mandelbrot<<<grid, BLOCK>>>(side, d_total);
    cudaEventRecord(t_stop);
    cudaEventSynchronize(t_stop);

    float ms = 0.0f;
    cudaEventElapsedTime(&ms, t_start, t_stop);
    if (t_kernel_s) *t_kernel_s = (double)ms / 1000.0;

    cudaEventDestroy(t_start);
    cudaEventDestroy(t_stop);

    err = cudaMemcpy(&h_total, d_total, sizeof(unsigned long long), cudaMemcpyDeviceToHost);
    cudaFree(d_total);

    if (err != cudaSuccess) {
        fprintf(stderr, "[CUDA] mandelbrot_cuda: cudaMemcpy falhou: %s\n",
                cudaGetErrorString(err));
        return 0.0;
    }

    return (double)h_total;
}
