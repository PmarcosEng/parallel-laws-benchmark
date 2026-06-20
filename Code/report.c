/* Suprime avisos de funções "inseguras" do MSVC (sscanf, etc.). */
#ifdef _MSC_VER
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include "report.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#  include <direct.h>   /* _mkdir */
#else
#  include <sys/stat.h> /* mkdir  */
#endif

/* ═══════════════════════════════════════════════════════
   UTILITÁRIOS DE APRESENTAÇÃO
═══════════════════════════════════════════════════════ */

void fmt_int(char *buf, int valor) {
    char tmp[32];
    int len = snprintf(tmp, sizeof(tmp), "%d", valor);
    int out = 0;
    for (int i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0)
            buf[out++] = '.';
        buf[out++] = tmp[i];
    }
    buf[out] = '\0';
}

/* Cria diretório (ignora erro se já existir). */
static void plat_mkdir(const char *path) {
#ifdef _MSC_VER
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

/* ═══════════════════════════════════════════════════════
   BANNER E TEXTOS DE SEÇÃO
═══════════════════════════════════════════════════════ */

void imprimir_banner(int cuda_ok) {
    (void)cuda_ok;
    printf("\n");
    printf("==================================================================\n");
    printf("  BENCHMARK — Leis de Amdahl e Gustafson na Pratica               \n");
    printf("==================================================================\n");
    printf("  CPU : %-40s  (%d nucleos / %d threads)\n",
           g_hw.cpu_nome, g_hw.cpu_nucleos, g_hw.cpu_threads);
#ifdef HAS_CUDA
    if (cuda_ok)
        printf("  GPU : %-40s  CUDA REAL ativo (%d cores)\n",
               g_hw.gpu_nome, g_hw.gpu_cuda_cores);
    else
        printf("  GPU : %-40s  (nenhuma GPU CUDA encontrada — somente CPU)\n",
               g_hw.gpu_nome);
#else
    printf("  GPU : %-40s  (build CPU — sem GPU)\n", g_hw.gpu_nome);
#endif
    printf("  RAM : %d GB livres para benchmark\n", g_hw.ram_gb);
    printf("------------------------------------------------------------------\n");
    printf("  Threads OpenMP : ");
    for (int i = 0; i < n_threads_teste; i++)
        printf("%d%s", threads_teste[i], i < n_threads_teste - 1 ? ", " : "\n");
    printf("  Repeticoes    : 1 warmup + %d cronometrados (mediana)\n",
           g_hw.n_repeticoes);
    printf("  Volumes busca : ");
    for (int i = 0; i < n_volumes_busca; i++) {
        char buf[16]; fmt_int(buf, volumes_busca[i]);
        double mb = (double)volumes_busca[i] * (double)sizeof(Event) / (1024.0 * 1024.0);
        printf("%s (~%.0f MB)%s", buf, mb, i < n_volumes_busca - 1 ? ", " : "\n");
    }
    printf("  Volumes math  : ");
    for (int i = 0; i < n_volumes_math; i++) {
        char buf[16]; fmt_int(buf, volumes_math[i]);
        printf("%s%s", buf, i < n_volumes_math - 1 ? ", " : "\n");
    }
    printf("  busca_linear [%.1f, %.1f]  |  busca_binaria alvo=%.1f  |  hash n_ids=%d\n",
           (double)BUSCA_MIN, (double)BUSCA_MAX, (double)BUSCA_ALVO, N_IDS_HASH);
    printf("==================================================================\n\n");
}

void imprimir_legenda_busca(void) {
    printf("\nLegenda (busca):\n");
    printf("  Speedup S(N) = T_serial / T_paralelo\n");
    printf("  Amdahl:    S(N) = 1 / (fs + fp/N)\n");
    printf("  Gustafson: S(N) = N - fs*(N-1)\n");
    printf("  cuda:    tempo_s = H2D(PCIe) + kernel;  transferencia = tempo_s - kernel\n\n");
}

void imprimir_titulo_math(void) {
    printf("==================================================================\n");
    printf("  SIMULACAO MATEMATICA DENSA (Monte Carlo Pi + Mandelbrot)\n");
    printf("==================================================================\n\n");
}

/* ═══════════════════════════════════════════════════════
   TABELA DE BUSCA + acúmulo de resultados
═══════════════════════════════════════════════════════ */
#define MAX_RESULTADOS 200
static BenchmarkResult todos_resultados[MAX_RESULTADOS];
static int n_resultados = 0;

void tabela_busca_separador(void) {
    printf("|%s|%s|%s|%s|%s|%s|%s|\n",
           "--------------", "----------", "--------------",
           "---------", "------------", "----------", "------------");
}

void tabela_busca_cabecalho(void) {
    printf("| %-12s | %-8s | %12s | %7s | %10s | %8s | %10s |\n",
           "Algoritmo", "Modo", "Volume", "Threads",
           "Tempo(s)", "Speedup", "Resultados");
    tabela_busca_separador();
}

void tabela_busca_rodape(void) {
    printf("|%s|%s|%s|%s|%s|%s|%s|\n",
           "==============", "==========", "==============",
           "=========", "============", "==========", "============");
}

void registrar_resultado_busca(BenchmarkResult *r) {
    if (n_resultados < MAX_RESULTADOS)
        todos_resultados[n_resultados++] = *r;
    char vol_str[16];
    fmt_int(vol_str, r->volume);
    printf("| %-12s | %-8s | %12s | %7d | %10.4f | %8.2fx | %10d |\n",
           r->algoritmo, r->modo, vol_str, r->threads,
           r->t_segundos, r->speedup, r->resultados);
}

void imprimir_resumo_busca(MelhorSpeedup linear, MelhorSpeedup binaria, MelhorSpeedup hash) {
    printf("  Melhor speedup:  linear=%.2fx (%s)  binaria=%.2fx (%s)"
           "  hash=%.2fx (%s)\n\n",
           linear.speedup,  linear.rotulo,
           binaria.speedup, binaria.rotulo,
           hash.speedup,    hash.rotulo);
}

/* ═══════════════════════════════════════════════════════
   TABELA DE MATEMÁTICA + acúmulo de resultados
═══════════════════════════════════════════════════════ */
#define MAX_MATH_RESULTADOS 100
static MathResult todos_math_resultados[MAX_MATH_RESULTADOS];
static int n_math_resultados = 0;

void tabela_math_cabecalho(void) {
    printf("| %-12s | %-8s | %12s | %7s | %10s | %8s | %14s |\n",
           "Algoritmo", "Modo", "Volume", "Threads", "Tempo(s)", "Speedup", "Valor");
    printf("|%s|%s|%s|%s|%s|%s|%s|\n",
           "--------------", "----------", "--------------",
           "---------", "------------", "----------", "----------------");
}

void tabela_math_separador(void) {
    printf("|%s|%s|%s|%s|%s|%s|%s|\n",
           "--------------", "----------", "--------------",
           "---------", "------------", "----------", "----------------");
}

void tabela_math_rodape(void) {
    printf("|%s|%s|%s|%s|%s|%s|%s|\n",
           "==============", "==========", "==============",
           "=========", "============", "==========", "================");
}

void registrar_resultado_math(MathResult *r) {
    if (n_math_resultados < MAX_MATH_RESULTADOS)
        todos_math_resultados[n_math_resultados++] = *r;
    char vol_str[16];
    fmt_int(vol_str, r->volume);
    printf("| %-12s | %-8s | %12s | %7d | %10.4f | %8.2fx | %14.4f |\n",
           r->algoritmo, r->modo, vol_str, r->threads,
           r->t_segundos, r->speedup, r->valor);
}

/* ═══════════════════════════════════════════════════════
   PERSISTÊNCIA — um JSON por run
═══════════════════════════════════════════════════════ */

/* Extrai run_id do timestamp ISO "YYYY-MM-DDTHH:MM:SS" -> "YYYYMMDD_HHMMSS". */
static void ts_to_run_id(const char *ts, char *run_id, size_t sz) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    sscanf(ts, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s);
    snprintf(run_id, sz, "%04d%02d%02d_%02d%02d%02d", y, mo, d, h, mi, s);
}

/* Escreve o bloco "hardware" comum aos dois tipos de run. */
static void escrever_hw(FILE *f) {
    fprintf(f, "  \"hardware\": {\n");
    fprintf(f, "    \"cpu_nome\": \"%s\",\n",   g_hw.cpu_nome);
    fprintf(f, "    \"cpu_nucleos\": %d,\n",    g_hw.cpu_nucleos);
    fprintf(f, "    \"cpu_threads\": %d,\n",    g_hw.cpu_threads);
    fprintf(f, "    \"gpu_nome\": \"%s\",\n",   g_hw.gpu_nome);
    fprintf(f, "    \"gpu_cuda_cores\": %d,\n", g_hw.gpu_cuda_cores);
    fprintf(f, "    \"ram_gb\": %d\n",          g_hw.ram_gb);
    fprintf(f, "  },\n");
}

void salvar_run_busca(const char *timestamp_iso) {
    char run_id[32];
    ts_to_run_id(timestamp_iso, run_id, sizeof(run_id));

#ifdef HAS_CUDA
    const char *build = "cuda";
    plat_mkdir("runs");
    plat_mkdir("runs/busca_cuda");
    char path[128];
    snprintf(path, sizeof(path), "runs/busca_cuda/%s.json", run_id);
#else
    const char *build = "cpu";
    plat_mkdir("runs");
    plat_mkdir("runs/busca_cpu");
    char path[128];
    snprintf(path, sizeof(path), "runs/busca_cpu/%s.json", run_id);
#endif

    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "[ERRO] Nao foi possivel criar %s\n", path); return; }

    fprintf(f, "{\n");
    fprintf(f, "  \"run_id\": \"%s\",\n", run_id);
    fprintf(f, "  \"timestamp\": \"%s\",\n", timestamp_iso);
    fprintf(f, "  \"simulacao\": \"busca\",\n");
    fprintf(f, "  \"build\": \"%s\",\n", build);
    escrever_hw(f);
    fprintf(f, "  \"parametros\": {\n");
    fprintf(f, "    \"n_repeticoes\": %d,\n", g_hw.n_repeticoes);
    fprintf(f, "    \"volumes\": [");
    for (int i = 0; i < n_volumes_busca; i++)
        fprintf(f, "%d%s", volumes_busca[i], i < n_volumes_busca - 1 ? "," : "");
    fprintf(f, "],\n");
    fprintf(f, "    \"threads\": [");
    for (int i = 0; i < n_threads_teste; i++)
        fprintf(f, "%d%s", threads_teste[i], i < n_threads_teste - 1 ? "," : "");
    fprintf(f, "],\n");
    fprintf(f, "    \"evento_bytes\": %zu,\n", sizeof(Event));
    fprintf(f, "    \"busca_min\": %.1f,\n",   (double)BUSCA_MIN);
    fprintf(f, "    \"busca_max\": %.1f,\n",   (double)BUSCA_MAX);
    fprintf(f, "    \"busca_alvo\": %.1f,\n",  (double)BUSCA_ALVO);
    fprintf(f, "    \"n_ids_hash\": %d\n",     N_IDS_HASH);
    fprintf(f, "  },\n");
    fprintf(f, "  \"resultados\": [\n");
    for (int i = 0; i < n_resultados; i++) {
        BenchmarkResult *r = &todos_resultados[i];
        fprintf(f,
            "    {\"algoritmo\":\"%s\",\"modo\":\"%s\",\"volume\":%d,"
            "\"threads\":%d,\"tempo_s\":%.8f,\"speedup\":%.6f,\"count\":%d",
            r->algoritmo, r->modo, r->volume, r->threads,
            r->t_segundos, r->speedup, r->resultados);
        /* Decomposicao PCIe — so nas linhas CUDA, onde medimos o kernel.
           transferencia (H2D) = tempo total - tempo de kernel. */
        if (strcmp(r->modo, "cuda") == 0 && r->t_kernel_s > 0.0) {
            double t_transfer = r->t_segundos - r->t_kernel_s;
            if (t_transfer < 0.0) t_transfer = 0.0;
            fprintf(f, ",\"tempo_kernel_s\":%.8f,\"tempo_transfer_s\":%.8f",
                    r->t_kernel_s, t_transfer);
        }
        fprintf(f, "}%s\n", (i < n_resultados - 1) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    printf(">>> Run busca salvo: %s\n", path);
}

void salvar_run_matematica(const char *timestamp_iso) {
    char run_id[32];
    ts_to_run_id(timestamp_iso, run_id, sizeof(run_id));

#ifdef HAS_CUDA
    const char *build = "cuda";
    plat_mkdir("runs");
    plat_mkdir("runs/matematica_cuda");
    char path[128];
    snprintf(path, sizeof(path), "runs/matematica_cuda/%s.json", run_id);
#else
    const char *build = "cpu";
    plat_mkdir("runs");
    plat_mkdir("runs/matematica_cpu");
    char path[128];
    snprintf(path, sizeof(path), "runs/matematica_cpu/%s.json", run_id);
#endif

    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "[ERRO] Nao foi possivel criar %s\n", path); return; }

    fprintf(f, "{\n");
    fprintf(f, "  \"run_id\": \"%s\",\n", run_id);
    fprintf(f, "  \"timestamp\": \"%s\",\n", timestamp_iso);
    fprintf(f, "  \"simulacao\": \"matematica\",\n");
    fprintf(f, "  \"build\": \"%s\",\n", build);
    escrever_hw(f);
    fprintf(f, "  \"parametros\": {\n");
    fprintf(f, "    \"n_repeticoes\": %d,\n", g_hw.n_repeticoes);
    fprintf(f, "    \"math_volumes\": [");
    for (int i = 0; i < n_volumes_math; i++)
        fprintf(f, "%d%s", volumes_math[i], i < n_volumes_math - 1 ? "," : "");
    fprintf(f, "],\n");
    fprintf(f, "    \"threads\": [");
    for (int i = 0; i < n_threads_teste; i++)
        fprintf(f, "%d%s", threads_teste[i], i < n_threads_teste - 1 ? "," : "");
    fprintf(f, "],\n");
    fprintf(f, "    \"mandelbrot_max_iter\": 256\n");
    fprintf(f, "  },\n");
    fprintf(f, "  \"resultados\": [\n");
    for (int i = 0; i < n_math_resultados; i++) {
        MathResult *r = &todos_math_resultados[i];
        fprintf(f,
            "    {\"algoritmo\":\"%s\",\"modo\":\"%s\",\"volume\":%d,"
            "\"threads\":%d,\"tempo_s\":%.8f,\"speedup\":%.6f,\"valor\":%.6f",
            r->algoritmo, r->modo, r->volume, r->threads,
            r->t_segundos, r->speedup, r->valor);
        /* Decomposicao na GPU — so nas linhas CUDA. Em Monte Carlo/Mandelbrot os
           dados nascem no device: "transferencia" = overhead de setup (malloc/
           memcpy/free) = tempo_total - kernel. Mesma convencao da busca. */
        if (strcmp(r->modo, "cuda") == 0 && r->t_kernel_s > 0.0) {
            double t_transfer = r->t_segundos - r->t_kernel_s;
            if (t_transfer < 0.0) t_transfer = 0.0;
            fprintf(f, ",\"tempo_kernel_s\":%.8f,\"tempo_transfer_s\":%.8f",
                    r->t_kernel_s, t_transfer);
        }
        fprintf(f, "}%s\n", (i < n_math_resultados - 1) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    printf(">>> Run matematica salvo: %s\n", path);
}
