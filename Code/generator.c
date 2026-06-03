#define _POSIX_C_SOURCE 199309L
#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <time.h>

#include "event.h"

/* ═══════════════════════════════════════════════════════════════
   CONSTANTES DO GERADOR
   Ajuste aqui para mudar o "perfil" dos dados sem tocar no código
═══════════════════════════════════════════════════════════════ */
#define SEMENTE_FIXA        42          /* reproduzibilidade obrigatória  */
#define VALOR_MEDIA         50.0f       /* média da distribuição Gaussiana*/
#define VALOR_DESVIO        15.0f       /* desvio padrão                  */
#define VALOR2_CORRELACAO   0.7f        /* correlação com valor principal  */
#define TIMESTAMP_BASE      1700000000000LL  /* nov/2023 em milissegundos  */
#define TIMESTAMP_INTERVALO 500LL       /* ~500ms entre eventos (Poisson)  */
#define NUM_SENSORES        12          /* quantidade de origens distintas */

/* ═══════════════════════════════════════════════════════════════
   DISTRIBUIÇÕES ESTATÍSTICAS
═══════════════════════════════════════════════════════════════ */

/* float uniforme em [0, 1) */
static inline float rand_float(void) {
    return (float)rand() / ((float)RAND_MAX + 1.0f);
}

/*
 * Distribuição Gaussiana — método Box-Muller
 * Transforma 2 uniformes em 2 normais padrão N(0,1)
 * Retorna N(media, desvio)
 *
 * Por quê? Valores de sensores reais seguem distribuição normal.
 * rand() % 100 daria uniforme — artificial demais para o benchmark.
 */
static float gaussiana(float media, float desvio) {
    float u, v, s;
    /* garante que u != 0 (log(0) = -inf) */
    do {
        u = rand_float() * 2.0f - 1.0f;
        v = rand_float() * 2.0f - 1.0f;
        s = u * u + v * v;
    } while (s >= 1.0f || s == 0.0f);

    float fator = sqrtf(-2.0f * logf(s) / s);
    return media + desvio * (u * fator);
}

/*
 * Distribuição Poisson via método exponencial
 * Simula intervalos entre chegadas de eventos
 *
 * Por quê? Logs de servidor, transações e leituras de sensores
 * chegam em intervalos que seguem distribuição de Poisson —
 * não chegam perfeitamente espaçados nem todos de uma vez.
 */
static int64_t intervalo_poisson(double taxa_ms) {
    /* intervalo exponencial: -lambda * ln(U) */
    double u = (double)rand() / (double)RAND_MAX;
    if (u < 1e-10) u = 1e-10; /* evita log(0) */
    return (int64_t)(-taxa_ms * log(u));
}

/*
 * Categoria com distribuição não uniforme
 * 70% NORMAL, 20% ALERTA, 8% CRITICO, 2% INATIVO
 *
 * Por quê? Sistemas reais têm muito mais eventos normais
 * do que críticos. Distribuição uniforme seria irreal.
 */
static Categoria categoria_realista(void) {
    int r = rand() % 100;
    if (r < 70) return CAT_NORMAL;
    if (r < 90) return CAT_ALERTA;
    if (r < 98) return CAT_CRITICO;
    return CAT_INATIVO;
}

/* Checksum XOR simples para integridade */
static uint32_t calcular_checksum(const Event *e) {
    return e->id ^ (uint32_t)e->timestamp ^ *(uint32_t*)&e->valor
           ^ (uint32_t)e->categoria ^ (uint32_t)e->status;
}

/* ═══════════════════════════════════════════════════════════════
   FUNÇÃO PRINCIPAL DE GERAÇÃO
═══════════════════════════════════════════════════════════════ */

/*
 * Gera 'n' eventos sintéticos realistas e grava no array alocado.
 *
 * Garante:
 *   - IDs únicos e sequenciais (busca binária e hash funcionam)
 *   - Timestamps crescentes com jitter Poisson
 *   - Valores com distribuição Gaussiana
 *   - Categorias com proporção realista
 *   - Reproduzibilidade total via srand(SEMENTE_FIXA)
 *
 * Retorna ponteiro para o array alocado, ou NULL em falha.
 * Caller é responsável por free().
 */
Event *gerar_eventos(int n) {
    if (n <= 0) return NULL;

    Event *eventos = (Event *)malloc((size_t)n * sizeof(Event));
    if (!eventos) {
        fprintf(stderr, "[ERRO] malloc falhou para %d eventos\n", n);
        return NULL;
    }

    /* ─── SEMENTE FIXA: resultados idênticos a cada execução ─── */
    srand(SEMENTE_FIXA);

    int64_t ts = TIMESTAMP_BASE;

    for (int i = 0; i < n; i++) {
        Event *e = &eventos[i];

        /* ID sequencial — necessário para busca binária funcionar */
        e->id = (uint32_t)(i + 1);

        /* Timestamp com jitter Poisson — simula chegada real */
        ts += intervalo_poisson((double)TIMESTAMP_INTERVALO);
        e->timestamp = ts;

        /* Valor principal — Gaussiana N(50, 15) */
        e->valor = gaussiana(VALOR_MEDIA, VALOR_DESVIO);

        /* Valor secundário — correlacionado com o principal
         * v2 = r*v1 + sqrt(1-r²)*ruido
         * Simula segunda leitura do mesmo sensor (temperatura + pressão) */
        float ruido = gaussiana(0.0f, VALOR_DESVIO);
        e->valor_secundario = VALOR2_CORRELACAO * e->valor
                            + sqrtf(1.0f - VALOR2_CORRELACAO * VALOR2_CORRELACAO) * ruido;

        /* Categoria e status com proporções realistas */
        e->categoria = categoria_realista();
        e->status    = (e->categoria == CAT_CRITICO) ? 2
                     : (e->categoria == CAT_ALERTA)  ? 1
                     : 0;

        /* Tag: "EVT-NNNNNN" — padrão para busca por prefixo */
        snprintf(e->tag, sizeof(e->tag), "EVT-%06u", e->id);

        /* Origem: "SENSOR_NN" — rotação entre NUM_SENSORES origens */
        snprintf(e->origem, sizeof(e->origem), "SENSOR_%02d",
                 (i % NUM_SENSORES) + 1);

        /* Checksum para integridade */
        e->checksum = calcular_checksum(e);

        /* Zera padding explicitamente (evita lixo de memória) */
        memset(e->_pad, 0, sizeof(e->_pad));
    }

    return eventos;
}

/* ═══════════════════════════════════════════════════════════════
   UTILITÁRIOS DE INSPEÇÃO
═══════════════════════════════════════════════════════════════ */

/* Imprime os primeiros 'n' eventos — útil para validar */
void inspecionar_eventos(const Event *eventos, int n, int max_print) {
    if (max_print > n) max_print = n;

    printf("\n═══ Amostra de %d/%d eventos gerados ═══\n", max_print, n);
    printf("%-8s %-16s %-8s %-8s %-10s %-6s %-10s %-12s\n",
           "ID", "TIMESTAMP", "VALOR", "VAL2", "CATEGORIA", "STATUS", "TAG", "ORIGEM");
    printf("─────────────────────────────────────────────────────────────────────\n");

    for (int i = 0; i < max_print; i++) {
        const Event *e = &eventos[i];
        printf("%-8u %-16lld %-8.2f %-8.2f %-10s %-6u %-10s %-12s\n",
               e->id,
               (long long)e->timestamp,
               e->valor,
               e->valor_secundario,
               CATEGORIA_NOME[e->categoria],
               e->status,
               e->tag,
               e->origem);
    }

    /* Estatísticas básicas para validar distribuição */
    float soma = 0.0f, min_v = eventos[0].valor, max_v = eventos[0].valor;
    int contagem[4] = {0};

    for (int i = 0; i < n; i++) {
        soma += eventos[i].valor;
        if (eventos[i].valor < min_v) min_v = eventos[i].valor;
        if (eventos[i].valor > max_v) max_v = eventos[i].valor;
        contagem[eventos[i].categoria]++;
    }

    float media = soma / (float)n;
    printf("\n─── Estatísticas (%d eventos) ───\n", n);
    printf("  Valor: média=%.2f  min=%.2f  max=%.2f\n", media, min_v, max_v);
    printf("  NORMAL=%d(%.0f%%)  ALERTA=%d(%.0f%%)  CRITICO=%d(%.0f%%)  INATIVO=%d(%.0f%%)\n",
           contagem[0], 100.0f*contagem[0]/n,
           contagem[1], 100.0f*contagem[1]/n,
           contagem[2], 100.0f*contagem[2]/n,
           contagem[3], 100.0f*contagem[3]/n);
    printf("  Tamanho por evento: %zu bytes\n", sizeof(Event));
    printf("  Total em memória:   %.2f MB\n\n",
           (double)n * sizeof(Event) / (1024.0 * 1024.0));
}
