#ifndef _MSC_VER
#  define _POSIX_C_SOURCE 199309L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _OPENMP
  #include <omp.h>
#endif

#include "event.h"
#include "search.h"

/* ═══════════════════════════════════════════════════════
   UTILITÁRIO — liberar resultado de busca
═══════════════════════════════════════════════════════ */
void search_result_free(SearchResult *r) {
    if (r && r->itens) {
        free(r->itens);
        r->itens = NULL;
        r->count = 0;
    }
}

/* ═══════════════════════════════════════════════════════
   BUSCA LINEAR SERIAL
   Loop simples — baseline de todos os benchmarks
   fp ≈ 100%: cada elemento é independente dos outros
═══════════════════════════════════════════════════════ */
SearchResult busca_linear_serial(Event *dados, int n, float min, float max) {
    /* aloca no pior caso — todos os n elementos */
    Event **encontrados = (Event **)malloc((size_t)n * sizeof(Event *));
    int count = 0;

    for (int i = 0; i < n; i++) {
        if (dados[i].valor >= min && dados[i].valor <= max) {
            encontrados[count++] = &dados[i];
        }
    }

    SearchResult r;
    r.itens = encontrados;
    r.count = count;
    return r;
}

/* ═══════════════════════════════════════════════════════
   BUSCA LINEAR — OpenMP
   Divide o array entre N threads com reduction no count
   Cada thread trabalha num subarray independente
═══════════════════════════════════════════════════════ */
SearchResult busca_linear_openmp(Event *dados, int n, float min, float max, int threads) {
    Event **encontrados = (Event **)malloc((size_t)n * sizeof(Event *));

    /*
     * Cada thread usa um índice local para evitar race condition.
     * Usamos array de contadores por thread e depois juntamos.
     */
    int  *counts   = (int *)calloc((size_t)threads, sizeof(int));
    /* índice de início de cada thread no array de resultado */
    int  *offsets  = (int *)calloc((size_t)threads, sizeof(int));
    /* cada thread armazena seus resultados aqui temporariamente */
    Event ***temp  = (Event ***)malloc((size_t)threads * sizeof(Event **));
    for (int t = 0; t < threads; t++) {
        temp[t] = (Event **)malloc((size_t)n * sizeof(Event *));
    }

    /* MSVC OpenMP 2.0 exige que a variavel do for seja declarada antes do pragma */
    int i;
#ifdef _OPENMP
    omp_set_num_threads(threads);
    #pragma omp parallel for schedule(static)
#endif
    for (i = 0; i < n; i++) {
        if (dados[i].valor >= min && dados[i].valor <= max) {
#ifdef _OPENMP
            int tid = omp_get_thread_num();
#else
            int tid = 0;
#endif
            temp[tid][counts[tid]++] = &dados[i];
        }
    }

    /* junta os resultados parciais em ordem */
    int total = 0;
    for (int t = 0; t < threads; t++) {
        offsets[t] = total;
        total += counts[t];
    }
    for (int t = 0; t < threads; t++) {
        memcpy(&encontrados[offsets[t]], temp[t],
               (size_t)counts[t] * sizeof(Event *));
        free(temp[t]);
    }
    free(temp);
    free(counts);
    free(offsets);

    SearchResult r;
    r.itens = encontrados;
    r.count = total;
    return r;
}

/* ═══════════════════════════════════════════════════════
   BUSCA LINEAR — GPU simulada (CPU com comportamento GPU)
   Simula o modelo SIMD: todos os elementos processados
   simultaneamente com um único predicado vetorizado.
   Em CUDA real isso seria um kernel com threadIdx/blockIdx.
═══════════════════════════════════════════════════════ */
SearchResult busca_linear_gpu_sim(Event *dados, int n, float min, float max) {
    /*
     * Simulação do kernel CUDA:
     *   __global__ void kernel(Event *d, int n, float min, float max,
     *                          int *flags) {
     *     int i = blockIdx.x * blockDim.x + threadIdx.x;
     *     if (i < n)
     *       flags[i] = (d[i].valor >= min && d[i].valor <= max);
     *   }
     *
     * Aqui fazemos o equivalente em CPU com loop simples,
     * mas a estrutura (flags → compactação) é idêntica ao CUDA.
     */
    int *flags = (int *)calloc((size_t)n, sizeof(int));

    /* fase 1: predicado (paralela na GPU — 1 thread por elemento) */
    for (int i = 0; i < n; i++) {
        flags[i] = (dados[i].valor >= min && dados[i].valor <= max) ? 1 : 0;
    }

    /* fase 2: prefix sum para calcular posições (scan paralelo na GPU) */
    int total = 0;
    for (int i = 0; i < n; i++) total += flags[i];

    Event **encontrados = (Event **)malloc((size_t)(total + 1) * sizeof(Event *));
    int idx = 0;
    for (int i = 0; i < n; i++) {
        if (flags[i]) encontrados[idx++] = &dados[i];
    }

    free(flags);

    SearchResult r;
    r.itens = encontrados;
    r.count = total;
    return r;
}

/* ═══════════════════════════════════════════════════════
   BUSCA BINÁRIA SERIAL
   Requer array ordenado por valor.
   Encontra TODOS os registros no intervalo [alvo-eps, alvo+eps]
   O(log n) para localizar + O(k) para coletar k resultados
═══════════════════════════════════════════════════════ */
static int cmp_evento_valor(const void *a, const void *b) {
    const Event *ea = *(const Event **)a;
    const Event *eb = *(const Event **)b;
    if (ea->valor < eb->valor) return -1;
    if (ea->valor > eb->valor) return  1;
    return 0;
}

/* Ordena o array in-place por valor — chame uma vez antes das buscas */
void ordenar_por_valor(Event *dados, int n) {
    /* cria array de ponteiros para não mover structs grandes */
    Event **ptrs = (Event **)malloc((size_t)n * sizeof(Event *));
    for (int i = 0; i < n; i++) ptrs[i] = &dados[i];
    qsort(ptrs, (size_t)n, sizeof(Event *), cmp_evento_valor);
    /* reordena o array original */
    Event *tmp = (Event *)malloc((size_t)n * sizeof(Event));
    for (int i = 0; i < n; i++) tmp[i] = *ptrs[i];
    memcpy(dados, tmp, (size_t)n * sizeof(Event));
    free(ptrs);
    free(tmp);
}

SearchResult busca_binaria_serial(Event *dados, int n, float alvo) {
    const float EPS = 0.5f; /* tolerância: aceita [alvo-0.5, alvo+0.5] */
    float min = alvo - EPS;
    float max = alvo + EPS;

    /* encontra o limite inferior com busca binária */
    int esq = 0, dir = n - 1, inicio = -1;
    while (esq <= dir) {
        int mid = esq + (dir - esq) / 2;
        if (dados[mid].valor >= min) {
            inicio = mid;
            dir = mid - 1;
        } else {
            esq = mid + 1;
        }
    }

    if (inicio == -1) {
        SearchResult r = {NULL, 0};
        return r;
    }

    /* coleta todos os elementos no intervalo a partir de 'inicio' */
    Event **encontrados = (Event **)malloc((size_t)n * sizeof(Event *));
    int count = 0;
    for (int i = inicio; i < n && dados[i].valor <= max; i++) {
        encontrados[count++] = &dados[i];
    }

    SearchResult r;
    r.itens = encontrados;
    r.count = count;
    return r;
}

/* ═══════════════════════════════════════════════════════
   BUSCA BINÁRIA — OpenMP
   Divide o array em N blocos, cada thread faz busca binária
   no seu bloco. Ilustra o limite de Amdahl: a fase de
   junção dos resultados é serial e limita o speedup.
═══════════════════════════════════════════════════════ */
SearchResult busca_binaria_openmp(Event *dados, int n, float alvo, int threads) {
    const float EPS = 0.5f;
    float min = alvo - EPS;
    float max = alvo + EPS;

    int bloco = n / threads;

    /* cada thread coleta seus resultados localmente */
    Event ***temp   = (Event ***)malloc((size_t)threads * sizeof(Event **));
    int    *counts  = (int *)calloc((size_t)threads, sizeof(int));
    for (int t = 0; t < threads; t++) {
        temp[t] = (Event **)malloc((size_t)(bloco + 1) * sizeof(Event *));
    }

    int t;
#ifdef _OPENMP
    omp_set_num_threads(threads);
    #pragma omp parallel for schedule(static)
#endif
    for (t = 0; t < threads; t++) {
        int inicio_bloco = t * bloco;
        int fim_bloco    = (t == threads - 1) ? n : inicio_bloco + bloco;
        int tamanho      = fim_bloco - inicio_bloco;
        Event *sub       = &dados[inicio_bloco];

        /* busca binária no subarray */
        int esq = 0, dir = tamanho - 1, inicio = -1;
        while (esq <= dir) {
            int mid = esq + (dir - esq) / 2;
            if (sub[mid].valor >= min) { inicio = mid; dir = mid - 1; }
            else esq = mid + 1;
        }
        if (inicio >= 0) {
            for (int i = inicio; i < tamanho && sub[i].valor <= max; i++) {
                temp[t][counts[t]++] = &sub[i];
            }
        }
    }

    /* fase serial: junta resultados — este é o fs de Amdahl */
    int total = 0;
    for (int t = 0; t < threads; t++) total += counts[t];

    Event **encontrados = (Event **)malloc((size_t)(total + 1) * sizeof(Event *));
    int idx = 0;
    for (int t = 0; t < threads; t++) {
        memcpy(&encontrados[idx], temp[t], (size_t)counts[t] * sizeof(Event *));
        idx += counts[t];
        free(temp[t]);
    }
    free(temp);
    free(counts);

    SearchResult r;
    r.itens = encontrados;
    r.count = total;
    return r;
}

/* ═══════════════════════════════════════════════════════
   BUSCA BINÁRIA — GPU simulada
   Lança N threads (uma por bloco), cada uma faz busca binária
   no seu bloco — modelo CUDA de grid/block.
═══════════════════════════════════════════════════════ */
SearchResult busca_binaria_gpu_sim(Event *dados, int n, float alvo) {
    /* GTX 1650: 14 SMs × 4 blocos residentes = 56 blocos ativos (Turing) */
    int n_blocos = 56;
    int bloco    = n / n_blocos;
    const float EPS = 0.5f;
    float min = alvo - EPS, max = alvo + EPS;

    Event **encontrados = (Event **)malloc((size_t)n * sizeof(Event *));
    int total = 0;

    for (int b = 0; b < n_blocos; b++) {
        int ini = b * bloco;
        int fim = (b == n_blocos - 1) ? n : ini + bloco;
        Event *sub = &dados[ini];
        int tam = fim - ini;

        int esq = 0, dir = tam - 1, inicio = -1;
        while (esq <= dir) {
            int mid = esq + (dir - esq) / 2;
            if (sub[mid].valor >= min) { inicio = mid; dir = mid - 1; }
            else esq = mid + 1;
        }
        if (inicio >= 0) {
            for (int i = inicio; i < tam && sub[i].valor <= max; i++) {
                encontrados[total++] = &sub[i];
            }
        }
    }

    SearchResult r;
    r.itens = encontrados;
    r.count = total;
    return r;
}

/* ═══════════════════════════════════════════════════════
   HASH TABLE — criação
   Tabela de dispersão com encadeamento externo
   Tamanho primo para minimizar colisões
═══════════════════════════════════════════════════════ */
#define HASH_TAMANHO 131101  /* primo próximo de 128k */

static inline int hash_fn(uint32_t key, int tamanho) {
    /* multiplicação de Knuth — distribui bem IDs sequenciais */
    return (int)((key * 2654435761u) % (uint32_t)tamanho);
}

HashTable *hash_criar(Event *dados, int n) {
    HashTable *ht = (HashTable *)malloc(sizeof(HashTable));
    ht->tamanho   = HASH_TAMANHO;
    ht->buckets   = (HashNode **)calloc((size_t)HASH_TAMANHO, sizeof(HashNode *));

    for (int i = 0; i < n; i++) {
        int idx = hash_fn(dados[i].id, HASH_TAMANHO);
        HashNode *node = (HashNode *)malloc(sizeof(HashNode));
        node->key    = dados[i].id;
        node->evento = &dados[i];
        node->prox   = ht->buckets[idx];
        ht->buckets[idx] = node;
    }

    return ht;
}

void hash_destruir(HashTable *ht) {
    for (int i = 0; i < ht->tamanho; i++) {
        HashNode *cur = ht->buckets[i];
        while (cur) {
            HashNode *prox = cur->prox;
            free(cur);
            cur = prox;
        }
    }
    free(ht->buckets);
    free(ht);
}

/* ═══════════════════════════════════════════════════════
   HASH LOOKUP SERIAL — O(1) amortizado
   Demonstra que paralelismo não ajuda operações pontuais
═══════════════════════════════════════════════════════ */
SearchResult hash_lookup_serial(HashTable *ht, uint32_t id) {
    int idx = hash_fn(id, ht->tamanho);
    HashNode *cur = ht->buckets[idx];
    while (cur) {
        if (cur->key == id) {
            Event **res = (Event **)malloc(sizeof(Event *));
            res[0] = cur->evento;
            SearchResult r = {res, 1};
            return r;
        }
        cur = cur->prox;
    }
    SearchResult r = {NULL, 0};
    return r;
}

/* ═══════════════════════════════════════════════════════
   HASH LOOKUP — OpenMP (múltiplos IDs em paralelo)
   Cada thread busca um ID diferente — mostra que o ganho
   é marginal porque cada lookup já é O(1)
═══════════════════════════════════════════════════════ */
SearchResult hash_lookup_openmp(HashTable *ht, uint32_t *ids,
                                 int n_ids, int threads) {
    Event **encontrados = (Event **)malloc((size_t)n_ids * sizeof(Event *));
    int    *counts      = (int *)calloc((size_t)n_ids, sizeof(int));

    int i;
#ifdef _OPENMP
    omp_set_num_threads(threads);
    #pragma omp parallel for schedule(static)
#endif
    for (i = 0; i < n_ids; i++) {
        int idx = hash_fn(ids[i], ht->tamanho);
        HashNode *cur = ht->buckets[idx];
        while (cur) {
            if (cur->key == ids[i]) {
                encontrados[i] = cur->evento;
                counts[i] = 1;
                break;
            }
            cur = cur->prox;
        }
    }

    int total = 0;
    for (int i = 0; i < n_ids; i++) total += counts[i];
    free(counts);

    SearchResult r;
    r.itens = encontrados;
    r.count = total;
    return r;
}

/* ═══════════════════════════════════════════════════════
   HASH LOOKUP — GPU simulada
   Mesmo modelo: N lookups independentes em paralelo
═══════════════════════════════════════════════════════ */
SearchResult hash_lookup_gpu_sim(HashTable *ht, uint32_t *ids, int n_ids) {
    Event **encontrados = (Event **)malloc((size_t)n_ids * sizeof(Event *));
    int total = 0;

    /* cada "thread GPU" processa um ID */
    for (int i = 0; i < n_ids; i++) {
        int idx = hash_fn(ids[i], ht->tamanho);
        HashNode *cur = ht->buckets[idx];
        while (cur) {
            if (cur->key == ids[i]) {
                encontrados[total++] = cur->evento;
                break;
            }
            cur = cur->prox;
        }
    }

    SearchResult r;
    r.itens = encontrados;
    r.count = total;
    return r;
}

/* ═══════════════════════════════════════════════════════
   REDUCE PARALELO SERIAL
   Conta quantos eventos têm valor em [min, max].
   Não coleta ponteiros — retorna apenas a contagem.
   Baseline para o speedup dos modos paralelos.
═══════════════════════════════════════════════════════ */
SearchResult reduce_paralelo_serial(Event *dados, int n, float min, float max) {
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (dados[i].valor >= min && dados[i].valor <= max)
            count++;
    }
    SearchResult r = {NULL, count};
    return r;
}

/* ═══════════════════════════════════════════════════════
   REDUCE PARALELO — OpenMP
   Redução canônica: cada thread conta seu trecho e a
   runtime OpenMP soma os parciais com árvore interna.
   fp ≈ 100%: sem dependência, sem fase serial de junção.
   Deve demonstrar speedup próximo do linear com volume grande.
═══════════════════════════════════════════════════════ */
SearchResult reduce_paralelo_openmp(Event *dados, int n, float min, float max, int threads) {
    int count = 0;
    int i;
#ifdef _OPENMP
    omp_set_num_threads(threads);
    #pragma omp parallel for reduction(+:count) schedule(static)
#endif
    for (i = 0; i < n; i++) {
        if (dados[i].valor >= min && dados[i].valor <= max)
            count++;
    }
    SearchResult r = {NULL, count};
    return r;
}

/* ═══════════════════════════════════════════════════════
   REDUCE PARALELO — GPU simulada
   Simula a redução em árvore do CUDA com memória compartilhada:
     fase 1: cada bloco de 256 "threads" conta sua fatia
     fase 2: acumula contadores de bloco (log2(N_BLOCOS) na GPU real)
   Demonstra por que a redução é ideal para GPU: fs mínimo,
   sem output array grande, shared memory elimina global writes.
═══════════════════════════════════════════════════════ */
SearchResult reduce_paralelo_gpu_sim(Event *dados, int n, float min, float max) {
    const int THREADS_POR_BLOCO = 256;
    const int N_BLOCOS = (n + THREADS_POR_BLOCO - 1) / THREADS_POR_BLOCO;

    int *contadores = (int *)calloc((size_t)N_BLOCOS, sizeof(int));

    /* fase 1: cada "bloco GPU" conta sua fatia independentemente */
    for (int b = 0; b < N_BLOCOS; b++) {
        int ini = b * THREADS_POR_BLOCO;
        int fim = ini + THREADS_POR_BLOCO;
        if (fim > n) fim = n;
        for (int i = ini; i < fim; i++) {
            if (dados[i].valor >= min && dados[i].valor <= max)
                contadores[b]++;
        }
    }

    /* fase 2: redução dos parciais (árvore na GPU real = log2 passos) */
    int total = 0;
    for (int b = 0; b < N_BLOCOS; b++) total += contadores[b];
    free(contadores);

    SearchResult r = {NULL, total};
    return r;
}
