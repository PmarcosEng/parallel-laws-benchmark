#ifndef CONFIG_H
#define CONFIG_H

/* ═══════════════════════════════════════════════════════
   CONFIG — parâmetros do experimento e descrição do hardware.

   Lê hardware.cfg (obrigatório) do diretório atual e monta as grades
   de volumes e de níveis de thread que os loops de benchmark percorrem.
   É a única "entrada" do programa; os outros módulos só consomem.
═══════════════════════════════════════════════════════ */

/* Parâmetros fixos da busca — iguais para todos os volumes. */
#define BUSCA_MIN   45.0f
#define BUSCA_MAX   55.0f
#define BUSCA_ALVO  50.0f
#define N_IDS_HASH  10000

/* Capacidade máxima das grades (volumes e níveis de thread). */
#define MAX_GRADE 16

/* Descrição do hardware + parâmetros lidos de hardware.cfg.
   Os campos csv_* guardam a string crua do arquivo; os valores já
   parseados ficam nas grades globais abaixo. */
typedef struct {
    char cpu_nome[64];
    int  cpu_nucleos;            /* núcleos físicos                       */
    int  cpu_threads;            /* threads lógicas (com SMT/HT)          */
    char gpu_nome[64];
    int  gpu_cuda_cores;         /* core count da GPU (threads dos modos cuda) */
    int  ram_gb;
    int  n_repeticoes;           /* repetições cronometradas por medição  */
    char csv_volumes_busca[128]; /* lista CSV de volumes de busca         */
    char csv_volumes_math[128];  /* lista CSV de volumes matemáticos      */
} HardwareConfig;

/* ─── Estado global do experimento (definido em config.c) ─── */

extern HardwareConfig g_hw;

/* Volumes de busca a testar (parseados de csv_volumes_busca). */
extern int volumes_busca[MAX_GRADE];
extern int n_volumes_busca;

/* Volumes da simulação matemática (parseados de csv_volumes_math). */
extern int volumes_math[MAX_GRADE];
extern int n_volumes_math;

/* Níveis de paralelismo a testar: {1, 2, 4, ..., cpu_threads}. */
extern int threads_teste[MAX_GRADE];
extern int n_threads_teste;

/* Lê hardware.cfg (obrigatório — imprime instrução e encerra se ausente),
   aplica defaults para campos opcionais e preenche as grades acima. */
void config_inicializar(void);

#endif /* CONFIG_H */
