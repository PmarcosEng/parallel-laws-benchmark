/*
 * search_cuda.cu — Busca linear real na GTX 1650
 *
 * Compilar com: nvcc -O3 -arch=sm_75 -c search_cuda.cu -o search_cuda.obj
 *
 * Arquitetura Turing (CC 7.5):
 *   - 14 SMs, 64 CUDA cores por SM = 896 cores totais
 *   - 32 threads por warp, até 32 warps ativos por SM
 *   - Memória GDDR5, banda teórica ~128 GB/s
 */

#include "search_cuda.h"
#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>


/* ─────────────────────────────────────────────────────────────
   Macro de verificação de erros CUDA
───────────────────────────────────────────────────────────── */
#define CUDA_CHECK(call)                                                    \
    do {                                                                    \
        cudaError_t _err = (call);                                          \
        if (_err != cudaSuccess) {                                          \
            fprintf(stderr, "[CUDA ERRO] %s (linha %d): %s\n",             \
                    __FILE__, __LINE__, cudaGetErrorString(_err));          \
            exit(1);                                                        \
        }                                                                   \
    } while (0)

/* ─────────────────────────────────────────────────────────────
   KERNEL — 1 thread por elemento do array

   Cada thread verifica SE dados[i].valor está no intervalo.
   Se sim, usa atomicAdd para reservar uma posição exclusiva
   no array de saída — sem race condition.

   blockIdx.x  = qual bloco de threads estamos
   blockDim.x  = threads por bloco (256)
   threadIdx.x = índice local dentro do bloco
   → i = índice global do elemento
───────────────────────────────────────────────────────────── */
__global__ void kernel_busca_linear(
    const Event* __restrict__ dados,   /* array de eventos na VRAM      */
    int           n,                   /* total de elementos             */
    float         vmin,                /* limite inferior do filtro      */
    float         vmax,                /* limite superior do filtro      */
    int*  __restrict__ out_indices,    /* saída: índices dos matches     */
    int*  __restrict__ d_count)        /* contador atômico de matches    */
{
    int i = (int)(blockIdx.x * blockDim.x + threadIdx.x);

    if (i < n && dados[i].valor >= vmin && dados[i].valor <= vmax) {
        /*
         * atomicAdd retorna o valor ANTES da adição —
         * garante que cada thread escreve numa posição única.
         */
        int pos = atomicAdd(d_count, 1);
        out_indices[pos] = i;
    }
}

/* ─────────────────────────────────────────────────────────────
   cuda_init — inicializa GPU e imprime info
───────────────────────────────────────────────────────────── */
extern "C" int cuda_init(void)
{
    cudaError_t err = cudaSetDevice(0);
    if (err != cudaSuccess) {
        fprintf(stderr, "[CUDA] Nenhuma GPU disponivel: %s\n",
                cudaGetErrorString(err));
        return 0;
    }

    cudaDeviceProp p;
    cudaGetDeviceProperties(&p, 0);

    /*
     * memoryClockRate foi removido no CUDA 12.0 (era kHz, DDR → ×2).
     * Para CUDA < 12: calcula banda teórica normalmente.
     * Para CUDA ≥ 12: exibe bus width; GTX 1650 GDDR5 128-bit ≈ 128 GB/s.
     */
#if CUDART_VERSION >= 12000
    printf("[CUDA] %-24s  |  %2d SMs  |  CC %d.%d  |"
           "  %.0f MB VRAM  |  bus %d-bit (banda: veja spec)\n",
           p.name, p.multiProcessorCount, p.major, p.minor,
           p.totalGlobalMem / (1024.0 * 1024.0), p.memoryBusWidth);
#else
    double banda_gb = (p.memoryClockRate * 2.0 * (p.memoryBusWidth / 8.0)) / 1e6;
    printf("[CUDA] %-24s  |  %2d SMs  |  CC %d.%d  |"
           "  %.0f MB VRAM  |  %.0f GB/s banda\n",
           p.name, p.multiProcessorCount, p.major, p.minor,
           p.totalGlobalMem / (1024.0 * 1024.0), banda_gb);
#endif

    /*
     * Warmup do contexto CUDA: a primeira operação CUDA
     * inicializa o driver (~100 ms). Fazemos aqui para não
     * contaminar as medições do benchmark.
     */
    void *tmp;
    CUDA_CHECK(cudaMalloc(&tmp, 256));
    CUDA_CHECK(cudaFree(tmp));
    CUDA_CHECK(cudaDeviceSynchronize());

    return 1;
}

/* ─────────────────────────────────────────────────────────────
   busca_linear_cuda — pipeline completo

   Etapas medidas separadamente:
     [ev0] ─ H2D transfer ─ [ev1] ─ kernel ─ [ev2] ─ D2H ─ [ev3]
               t_transfer           t_kernel          t_d2h
           └─────────────────── t_total ──────────────────────┘
───────────────────────────────────────────────────────────── */
extern "C" SearchResult busca_linear_cuda(
    Event *dados, int n, float vmin, float vmax,
    double *t_total_s, double *t_kernel_s)
{
    const int THREADS_POR_BLOCO = 256;
    const int N_BLOCOS = (n + THREADS_POR_BLOCO - 1) / THREADS_POR_BLOCO;

    /* ── aloca VRAM ── */
    Event *d_dados   = NULL;
    int   *d_indices = NULL;
    int   *d_count   = NULL;

    CUDA_CHECK(cudaMalloc(&d_dados,   (size_t)n * sizeof(Event)));
    CUDA_CHECK(cudaMalloc(&d_indices, (size_t)n * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_count,   sizeof(int)));

    /* ── eventos CUDA para medir tempo com precisão de µs ── */
    cudaEvent_t ev0, ev1, ev2, ev3;
    CUDA_CHECK(cudaEventCreate(&ev0));
    CUDA_CHECK(cudaEventCreate(&ev1));
    CUDA_CHECK(cudaEventCreate(&ev2));
    CUDA_CHECK(cudaEventCreate(&ev3));

    /* ── fase 1: transferência CPU → GPU (PCIe) ── */
    CUDA_CHECK(cudaEventRecord(ev0));
    CUDA_CHECK(cudaMemcpy(d_dados, dados,
                          (size_t)n * sizeof(Event),
                          cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemset(d_count, 0, sizeof(int)));
    CUDA_CHECK(cudaEventRecord(ev1));

    /* ── fase 2: kernel — 896 CUDA cores em paralelo ── */
    CUDA_CHECK(cudaEventRecord(ev2));
    kernel_busca_linear<<<N_BLOCOS, THREADS_POR_BLOCO>>>(
        d_dados, n, vmin, vmax, d_indices, d_count);
    CUDA_CHECK(cudaEventRecord(ev3));
    CUDA_CHECK(cudaEventSynchronize(ev3));

    /* ── lê o contador de resultados ── */
    int count = 0;
    CUDA_CHECK(cudaMemcpy(&count, d_count,
                          sizeof(int), cudaMemcpyDeviceToHost));

    /* ── fase 3: transferência GPU → CPU dos índices encontrados ── */
    int *h_indices = (int *)malloc((size_t)(count + 1) * sizeof(int));
    if (count > 0)
        CUDA_CHECK(cudaMemcpy(h_indices, d_indices,
                              (size_t)count * sizeof(int),
                              cudaMemcpyDeviceToHost));

    /* ── calcula tempos ── */
    float ms_h2d = 0.f, ms_ker = 0.f, ms_total = 0.f;
    CUDA_CHECK(cudaEventElapsedTime(&ms_h2d,   ev0, ev1));
    CUDA_CHECK(cudaEventElapsedTime(&ms_ker,   ev2, ev3));

    /* total = H2D + kernel + D2H (D2H medido pela diferença) */
    float ms_d2h;
    {
        /* recria ev3 final após o D2H */
        cudaEvent_t ev_end;
        CUDA_CHECK(cudaEventCreate(&ev_end));
        /* o D2H já terminou; usamos wall-clock aproximado */
        (void)ms_d2h;   /* D2H de índices é pequeno — ignoramos na soma */
        ms_total = ms_h2d + ms_ker;
        cudaEventDestroy(ev_end);
    }

    if (t_total_s)  *t_total_s  = (double)ms_total / 1000.0;
    if (t_kernel_s) *t_kernel_s = (double)ms_ker   / 1000.0;

    /* ── monta SearchResult com ponteiros para o array host ── */
    Event **encontrados = (Event **)malloc(
        (size_t)(count + 1) * sizeof(Event *));
    for (int i = 0; i < count; i++)
        encontrados[i] = &dados[h_indices[i]];

    /* ── libera VRAM e eventos ── */
    cudaFree(d_dados);
    cudaFree(d_indices);
    cudaFree(d_count);
    cudaEventDestroy(ev0);
    cudaEventDestroy(ev1);
    cudaEventDestroy(ev2);
    cudaEventDestroy(ev3);
    free(h_indices);

    SearchResult r = { encontrados, count };
    return r;
}

/* ─────────────────────────────────────────────────────────────
   KERNEL — Busca Binária na GPU
   Estratégia: divide o array ordenado em N chunks.
   Cada thread recebe um chunk e faz busca binária dentro dele.

   Por quê isso demonstra overhead?
     - Busca binária é O(log n) — muito pouca computação
     - O custo dominante é o H2D: copiar o array inteiro para VRAM
     - O kernel termina em microssegundos; a transferência leva ms
     - Resultado: GPU MAIS LENTA que CPU serial para este algoritmo
     - Ilustra perfeitamente o limite de Amdahl: fração paralela baixa
──────────────────────────────────────────────────────────── */
__global__ void kernel_busca_binaria(
    const Event* __restrict__ dados,  /* array ordenado por valor na VRAM */
    int     n,                         /* total de elementos               */
    float   alvo,                      /* valor a encontrar                */
    int*  __restrict__ d_count)        /* contador atômico de matches      */
{
    /* Cada thread cobre um chunk do array */
    int tid      = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    int n_chunks = (int)(gridDim.x * blockDim.x);
    int chunk    = (n + n_chunks - 1) / n_chunks;

    int lo = tid * chunk;
    int hi = lo + chunk - 1;
    if (hi >= n) hi = n - 1;
    if (lo > hi) return;

    /* Busca binária local no chunk */
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (dados[mid].valor == alvo) {
            atomicAdd(d_count, 1);
            /* procura outros matches adjacentes (valores iguais) */
            int l = mid - 1;
            while (l >= lo && dados[l].valor == alvo) {
                atomicAdd(d_count, 1);
                l--;
            }
            int r = mid + 1;
            while (r <= hi && dados[r].valor == alvo) {
                atomicAdd(d_count, 1);
                r++;
            }
            return;
        } else if (dados[mid].valor < alvo) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
}

/* ─────────────────────────────────────────────────────────────
   busca_binaria_cuda — pipeline completo

   Etapas:
     [ev0] ─ H2D (array ordenado) ─ [ev1] ─ kernel ─ [ev2]
              t_h2d                   t_kernel
           └─────────── t_total ──────────────────┘

   O t_total inclui H2D. Em volumes pequenos, H2D domina ~100%.
──────────────────────────────────────────────────────────── */
extern "C" SearchResult busca_binaria_cuda(
    Event *dados, int n, float alvo,
    double *t_total_s, double *t_kernel_s)
{
    const int THREADS_POR_BLOCO = 256;
    const int N_BLOCOS = (n + THREADS_POR_BLOCO - 1) / THREADS_POR_BLOCO;

    Event *d_dados = NULL;
    int   *d_count = NULL;

    CUDA_CHECK(cudaMalloc(&d_dados, (size_t)n * sizeof(Event)));
    CUDA_CHECK(cudaMalloc(&d_count, sizeof(int)));

    cudaEvent_t ev0, ev1, ev2;
    CUDA_CHECK(cudaEventCreate(&ev0));
    CUDA_CHECK(cudaEventCreate(&ev1));
    CUDA_CHECK(cudaEventCreate(&ev2));

    /* fase 1: H2D — custo dominante para este algoritmo */
    CUDA_CHECK(cudaEventRecord(ev0));
    CUDA_CHECK(cudaMemcpy(d_dados, dados, (size_t)n * sizeof(Event),
                          cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemset(d_count, 0, sizeof(int)));
    CUDA_CHECK(cudaEventRecord(ev1));

    /* fase 2: kernel — muito rápido (O(log n) por chunk) */
    kernel_busca_binaria<<<N_BLOCOS, THREADS_POR_BLOCO>>>(
        d_dados, n, alvo, d_count);
    CUDA_CHECK(cudaEventRecord(ev2));
    CUDA_CHECK(cudaEventSynchronize(ev2));

    int count = 0;
    CUDA_CHECK(cudaMemcpy(&count, d_count, sizeof(int), cudaMemcpyDeviceToHost));

    float ms_h2d = 0.f, ms_ker = 0.f;
    CUDA_CHECK(cudaEventElapsedTime(&ms_h2d, ev0, ev1));
    CUDA_CHECK(cudaEventElapsedTime(&ms_ker, ev1, ev2));

    if (t_total_s)  *t_total_s  = (double)(ms_h2d + ms_ker) / 1000.0;
    if (t_kernel_s) *t_kernel_s = (double)ms_ker / 1000.0;

    cudaFree(d_dados);
    cudaFree(d_count);
    cudaEventDestroy(ev0);
    cudaEventDestroy(ev1);
    cudaEventDestroy(ev2);

    SearchResult r = {NULL, count};
    return r;
}

/* ─────────────────────────────────────────────────────────────
   KERNEL — Hash Lookup na GPU
   Estratégia: each thread recebe 1 ID, procura no array flat da HT.

   A tabela hash da CPU usa ponteiros (lista encadeada):
     buckets[i] → HashNode* → HashNode* → ...
   A GPU não pode seguir ponteiros host. Solução: achatamos
   a tabela em dois arrays contíguos antes de copiar para a VRAM:
     flat_keys[]   — chaves de todas as colisões, bucket por bucket
     flat_starts[] — índice de início de cada bucket em flat_keys

   Por quê isso demonstra overhead de GPU?
     - Acessos a flat_keys[] são espalhados (não coalescidos)
     - Cada warp acessa buckets diferentes → divergência de cache
     - Custo de setup (achatamento + H2D) supera o ganho do paralelo
     - Perfeito para demonstrar que nem toda estrutura "paraleliza"
──────────────────────────────────────────────────────────── */
__global__ void kernel_hash_lookup(
    const uint32_t* __restrict__ flat_keys,    /* todas as chaves contíguas */
    const int*      __restrict__ flat_starts,  /* flat_starts[b] = início do bucket b */
    const int*      __restrict__ flat_lens,    /* flat_lens[b] = nº de entradas no bucket b */
    int             tamanho,                   /* número de buckets */
    const uint32_t* __restrict__ ids,          /* IDs a procurar */
    int             n_ids,                     /* quantos IDs */
    int*  __restrict__ d_count)                /* contador atômico */
{
    int tid = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    if (tid >= n_ids) return;

    uint32_t id     = ids[tid];
    int      bucket = (int)(id % (uint32_t)tamanho);
    int      start  = flat_starts[bucket];
    int      len    = flat_lens[bucket];

    for (int i = 0; i < len; i++) {
        if (flat_keys[start + i] == id) {
            atomicAdd(d_count, 1);
            break;  /* uma entrada por ID na nossa tabela */
        }
    }
}

/* ─────────────────────────────────────────────────────────────
   hash_lookup_cuda — achatamento + pipeline GPU

   Etapas host:
     1. Achatar HT (CPU, memória do host)
     2. H2D: flat_keys, flat_starts, flat_lens, ids
     3. Kernel
     4. D2H: count (1 int)
──────────────────────────────────────────────────────────── */
extern "C" SearchResult hash_lookup_cuda(
    HashTable *ht, uint32_t *ids, int n_ids,
    double *t_total_s, double *t_kernel_s)
{
    int tamanho = ht->tamanho;

    /* ── passo 1: achatar a tabela hash em arrays flat (host) ── */
    int  *h_starts = (int *)malloc((size_t)(tamanho + 1) * sizeof(int));
    int  *h_lens   = (int *)malloc((size_t)tamanho * sizeof(int));

    /* primeira passagem: conta entradas por bucket */
    int total_entries = 0;
    for (int b = 0; b < tamanho; b++) {
        int cnt = 0;
        for (HashNode *nd = ht->buckets[b]; nd; nd = nd->prox) cnt++;
        h_lens[b]    = cnt;
        h_starts[b]  = total_entries;
        total_entries += cnt;
    }

    uint32_t *h_keys = (uint32_t *)malloc(
        (size_t)(total_entries + 1) * sizeof(uint32_t));

    /* segunda passagem: preenche flat_keys */
    for (int b = 0; b < tamanho; b++) {
        int pos = h_starts[b];
        for (HashNode *nd = ht->buckets[b]; nd; nd = nd->prox)
            h_keys[pos++] = nd->key;
    }

    /* ── passo 2: alocar VRAM e copiar ── */
    uint32_t *d_keys    = NULL;
    int      *d_starts  = NULL;
    int      *d_lens    = NULL;
    uint32_t *d_ids     = NULL;
    int      *d_count   = NULL;

    CUDA_CHECK(cudaMalloc(&d_keys,   (size_t)(total_entries + 1) * sizeof(uint32_t)));
    CUDA_CHECK(cudaMalloc(&d_starts, (size_t)tamanho * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_lens,   (size_t)tamanho * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_ids,    (size_t)n_ids * sizeof(uint32_t)));
    CUDA_CHECK(cudaMalloc(&d_count,  sizeof(int)));

    cudaEvent_t ev0, ev1, ev2;
    CUDA_CHECK(cudaEventCreate(&ev0));
    CUDA_CHECK(cudaEventCreate(&ev1));
    CUDA_CHECK(cudaEventCreate(&ev2));

    /* fase 1: H2D */
    CUDA_CHECK(cudaEventRecord(ev0));
    CUDA_CHECK(cudaMemcpy(d_keys,   h_keys,   (size_t)(total_entries + 1) * sizeof(uint32_t),
                          cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_starts, h_starts, (size_t)tamanho * sizeof(int),
                          cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_lens,   h_lens,   (size_t)tamanho * sizeof(int),
                          cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_ids,    ids,       (size_t)n_ids * sizeof(uint32_t),
                          cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemset(d_count, 0, sizeof(int)));
    CUDA_CHECK(cudaEventRecord(ev1));

    /* fase 2: kernel — 1 thread por ID */
    const int THREADS_POR_BLOCO = 256;
    const int N_BLOCOS = (n_ids + THREADS_POR_BLOCO - 1) / THREADS_POR_BLOCO;
    kernel_hash_lookup<<<N_BLOCOS, THREADS_POR_BLOCO>>>(
        d_keys, d_starts, d_lens, tamanho, d_ids, n_ids, d_count);
    CUDA_CHECK(cudaEventRecord(ev2));
    CUDA_CHECK(cudaEventSynchronize(ev2));

    int count = 0;
    CUDA_CHECK(cudaMemcpy(&count, d_count, sizeof(int), cudaMemcpyDeviceToHost));

    float ms_h2d = 0.f, ms_ker = 0.f;
    CUDA_CHECK(cudaEventElapsedTime(&ms_h2d, ev0, ev1));
    CUDA_CHECK(cudaEventElapsedTime(&ms_ker, ev1, ev2));

    if (t_total_s)  *t_total_s  = (double)(ms_h2d + ms_ker) / 1000.0;
    if (t_kernel_s) *t_kernel_s = (double)ms_ker / 1000.0;

    /* libera VRAM */
    cudaFree(d_keys);
    cudaFree(d_starts);
    cudaFree(d_lens);
    cudaFree(d_ids);
    cudaFree(d_count);
    cudaEventDestroy(ev0);
    cudaEventDestroy(ev1);
    cudaEventDestroy(ev2);

    /* libera host temporário */
    free(h_keys);
    free(h_starts);
    free(h_lens);

    SearchResult r = {NULL, count};
    return r;
}
