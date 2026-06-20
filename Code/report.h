#ifndef REPORT_H
#define REPORT_H

#include "event.h"     /* BenchmarkResult */
#include "sim_math.h"  /* MathResult     */

/* ═══════════════════════════════════════════════════════
   REPORT — toda a saída do programa (a "interface").

   Banner, tabelas no terminal, legendas e os arquivos JSON por run.
   Não mede nem calcula nada: recebe resultados prontos, apresenta na
   tela e acumula para persistir no fim. Isola a apresentação da lógica
   de orquestração (benchmark.c) e dos kernels (search.c / sim_math.c).
═══════════════════════════════════════════════════════ */

/* Melhor speedup observado para um algoritmo num dado volume, com rótulo
   legível do modo que o atingiu (ex.: "openmp 8t", "cuda"). */
typedef struct {
    double speedup;
    char   rotulo[24];
} MelhorSpeedup;

/* Formata inteiro com separador de milhar: 1000000 -> "1.000.000".
   `buf` deve ter espaço suficiente (>= 16 bytes para int de 32 bits). */
void fmt_int(char *buf, int valor);

/* ─── Banner e textos de seção ─── */
void imprimir_banner(int cuda_ok);
void imprimir_legenda_busca(void);
void imprimir_titulo_math(void);

/* ─── Tabela de busca ─── */
void tabela_busca_cabecalho(void);
void tabela_busca_separador(void);          /* linha fina entre algoritmos  */
void tabela_busca_rodape(void);             /* linha grossa no fim do volume */
/* Imprime a linha na tela E acumula o resultado para o JSON final. */
void registrar_resultado_busca(BenchmarkResult *r);
/* Resumo dos melhores speedups dos 3 algoritmos de busca no volume. */
void imprimir_resumo_busca(MelhorSpeedup linear, MelhorSpeedup binaria, MelhorSpeedup hash);

/* ─── Tabela de matemática ─── */
void tabela_math_cabecalho(void);
void tabela_math_separador(void);           /* linha fina entre algoritmos  */
void tabela_math_rodape(void);              /* linha grossa no fim do volume */
void registrar_resultado_math(MathResult *r);

/* ─── Persistência (um JSON por run, em runs/<sim>_<build>/<run_id>.json) ─── */
void salvar_run_busca(const char *timestamp_iso);
void salvar_run_matematica(const char *timestamp_iso);

#endif /* REPORT_H */
