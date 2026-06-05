#ifndef EVENT_H
#define EVENT_H

#include <stdint.h>

/* ─────────────────────────────────────────────
   Categorias possíveis de um evento
   4 valores → distribuição não uniforme no gerador
───────────────────────────────────────────── */
typedef enum {
    CAT_NORMAL   = 0,   /* 70% dos registros */
    CAT_ALERTA   = 1,   /* 20% dos registros */
    CAT_CRITICO  = 2,   /* 8%  dos registros */
    CAT_INATIVO  = 3    /* 2%  dos registros */
} Categoria;

static const char *CATEGORIA_NOME[] = {
    "NORMAL", "ALERTA", "CRITICO", "INATIVO"
};

/* ─────────────────────────────────────────────
   Registro principal — 96 bytes por evento
   Alinhado para acesso eficiente na GPU
───────────────────────────────────────────── */
typedef struct {
    uint32_t   id;              /* identificador único sequencial        */
    int64_t    timestamp;       /* Unix timestamp em milissegundos       */
    float      valor;           /* leitura do sensor — distribuição N    */
    float      valor_secundario;/* segunda leitura — correlacionada      */
    Categoria  categoria;       /* enum 0–3 com distribuição não uniforme*/
    uint8_t    status;          /* 0=ok 1=aviso 2=falha                  */
    char       tag[32];         /* ex: "EVT-004821" — string com padrão  */
    char       origem[16];      /* ex: "SENSOR_07"  — origem do evento   */
    uint32_t   checksum;        /* XOR simples dos campos — integridade  */
    uint8_t    _pad[8];         /* padding para alinhar em 96 bytes      */
} Event;

/* ─────────────────────────────────────────────
   Resultado de benchmark — 1 linha da tabela
───────────────────────────────────────────── */
typedef struct {
    char   algoritmo[32];   /* "linear", "binaria", "hash"        */
    char   modo[16];        /* "serial", "openmp", "cuda"         */
    int    volume;          /* número de registros                */
    int    threads;         /* 1, 2, 4, 8 (ou núcleos GPU)       */
    double t_segundos;      /* tempo medido em segundos (total)   */
    double speedup;         /* Tserial / T_este                   */
    int    resultados;      /* quantos registros a busca retornou */
    double t_kernel_s;      /* só CUDA: tempo do kernel puro.
                               Transferência PCIe = t_segundos - t_kernel_s.
                               0 nos modos serial/openmp.   */
} BenchmarkResult;

#endif /* EVENT_H */
