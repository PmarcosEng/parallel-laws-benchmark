#ifndef SEARCH_CUDA_H
#define SEARCH_CUDA_H

#include "event.h"
#include "search.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Inicializa a GPU e imprime informações (nome, SMs, VRAM, banda).
 * Deve ser chamada UMA vez no início do main(), antes de qualquer medição.
 * Retorna 1 se a GPU está disponível, 0 em caso de falha.
 */
int cuda_init(void);

/*
 * Busca linear real na GTX 1650 usando 896 CUDA cores.
 *
 *   dados       — array de eventos na memória do host (CPU)
 *   n           — número de eventos
 *   vmin/vmax   — intervalo de filtro
 *   t_total_s   — [saída] tempo completo: H2D + kernel + D2H  (pode ser NULL)
 *   t_kernel_s  — [saída] tempo só do kernel CUDA              (pode ser NULL)
 *
 * O resultado é alocado na memória do host; libere com search_result_free().
 */
SearchResult busca_linear_cuda(Event *dados, int n, float vmin, float vmax,
                               double *t_total_s, double *t_kernel_s);

/*
 * Reduce paralelo real na GTX 1650 — kernel com redução em shared memory.
 *
 * Cada bloco reduz sua fatia para um inteiro em SHMEM (on-chip),
 * depois um único atomicAdd por bloco acumula no resultado global.
 * Padrão ideal para GPU: sem array de saída grande, sem dependências.
 *
 *   t_total_s  — [saída] H2D + kernel  (pode ser NULL)
 *   t_kernel_s — [saída] kernel puro   (pode ser NULL)
 *
 * Retorna SearchResult com itens=NULL e count=número de matches.
 */
SearchResult reduce_paralelo_cuda(Event *dados, int n, float vmin, float vmax,
                                  double *t_total_s, double *t_kernel_s);

/*
 * Busca binária na GPU (1 thread por chunk do array).
 * Exige que dados esteja ordenado.
 */
SearchResult busca_binaria_cuda(Event *dados, int n, float alvo,
                                double *t_total_s, double *t_kernel_s);

/*
 * Hash lookup na GPU.
 * Achata a tabela hash da CPU em dois arrays e copia para a GPU,
 * onde múltiplas threads fazem a busca em O(1) simultaneamente.
 */
SearchResult hash_lookup_cuda(HashTable *ht, uint32_t *ids, int n_ids,
                              double *t_total_s, double *t_kernel_s);

#ifdef __cplusplus
}
#endif

#endif /* SEARCH_CUDA_H */
