#ifndef SEARCH_H
#define SEARCH_H

#include "event.h"

/* ─────────────────────────────────────────────
   Resultado de uma busca
───────────────────────────────────────────── */
typedef struct {
    Event  **itens;     /* ponteiros para os eventos encontrados */
    int      count;     /* quantos foram encontrados             */
} SearchResult;

void search_result_free(SearchResult *r);

/* ─────────────────────────────────────────────
   BUSCA LINEAR — O(n)
   Varre todos os registros aplicando filtro de intervalo
   Ideal para paralelismo — sem dependência entre elementos
───────────────────────────────────────────── */
SearchResult busca_linear_serial  (Event *dados, int n, float min, float max);
SearchResult busca_linear_openmp  (Event *dados, int n, float min, float max, int threads);

/* ─────────────────────────────────────────────
   BUSCA BINÁRIA — O(log n)
   Exige dados ordenados por valor
   Paralelismo limitado — cada passo depende do anterior
───────────────────────────────────────────── */
SearchResult busca_binaria_serial  (Event *dados, int n, float alvo);
SearchResult busca_binaria_openmp  (Event *dados, int n, float alvo, int threads);

/* ─────────────────────────────────────────────
   HASH LOOKUP — O(1) amortizado
   Acesso direto por ID via tabela hash
   Essencialmente serial — overhead cancela ganho paralelo
───────────────────────────────────────────── */
typedef struct HashNode {
    uint32_t       key;
    Event         *evento;
    struct HashNode *prox;
} HashNode;

typedef struct {
    HashNode **buckets;
    int        tamanho;
} HashTable;

HashTable   *hash_criar    (Event *dados, int n);
void         hash_destruir (HashTable *ht);

SearchResult hash_lookup_serial  (HashTable *ht, uint32_t id);
SearchResult hash_lookup_openmp  (HashTable *ht, uint32_t *ids, int n_ids, int threads);

#endif /* SEARCH_H */
