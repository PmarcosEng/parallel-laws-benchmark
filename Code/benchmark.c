/* Suprime avisos de funcoes "inseguras" do MSVC (localtime, putenv, etc.) */
#ifdef _MSC_VER
#  define _CRT_SECURE_NO_WARNINGS
#endif
/* No GCC/MinGW precisamos destravar extensoes POSIX/GNU dos headers.
   _GNU_SOURCE e o superconjunto: cobre clock_gettime (precisava de
   _POSIX_C_SOURCE) E putenv (extensao XSI, exige nivel mais alto que
   199309L expunha). MSVC ignora a macro e usa o caminho proprio. */
#ifndef _MSC_VER
#  define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#ifdef _MSC_VER
#  include <direct.h>   /* _mkdir */
#else
#  include <sys/stat.h> /* mkdir */
#endif

#include "event.h"
#include "generator.h"
#include "search.h"
#include "sim_math.h"
#ifdef HAS_CUDA
#  include "search_cuda.h"
#  include "sim_math_cuda.h"
#endif

/* ═══════════════════════════════════════════════════════
   MEDIÇÃO DE TEMPO — segundos com precisão de nanosegundos
   MSVC/Windows : QueryPerformanceCounter  (sem dependencia POSIX)
   GCC/Linux    : clock_gettime(CLOCK_MONOTONIC)
═══════════════════════════════════════════════════════ */
#ifdef _MSC_VER
#  include <windows.h>
static double agora(void) {
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart / (double)freq.QuadPart;
}
#else
static double agora(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
#endif

/* ═══════════════════════════════════════════════════════
   CONFIGURAÇÃO DO EXPERIMENTO
═══════════════════════════════════════════════════════ */

static int g_volumes[16];
static int g_n_volumes = 0;
#define N_VOLUMES g_n_volumes
#define VOLUMES   g_volumes

static int g_math_volumes[16];
static int g_n_math_volumes = 0;

/* Parâmetros de busca fixos — mesmos para todos os volumes */
#define BUSCA_MIN   45.0f
#define BUSCA_MAX   55.0f
#define BUSCA_ALVO  50.0f
#define N_IDS_HASH  10000

/* ═══════════════════════════════════════════════════════
   CONFIGURAÇÃO DE HARDWARE — preenchida em tempo de execução
   Lida de hardware.cfg (obrigatorio).
═══════════════════════════════════════════════════════ */
typedef struct {
    char cpu_nome[64];
    int  cpu_nucleos;        /* nucleos fisicos */
    int  cpu_threads;        /* threads logicas (com SMT/HT) */
    char gpu_nome[64];
    int  gpu_cuda_cores;     /* core count da GPU (threads dos modos cuda) */
    int  ram_gb;
    int  n_repeticoes;       /* repeticoes cronometradas por medicao (default 3) */
    char volumes_busca[128]; /* lista CSV de volumes de busca */
    char volumes_math[128];  /* lista CSV de volumes matematicos */
} HardwareConfig;

static HardwareConfig g_hw = {
    "CPU", 4, 8, "GPU", 896, 8, 3,
    "1000,100000,1000000,10000000,20000000",
    "100000,500000,1000000,5000000,10000000"
};

/* Macros que redirecionam para variáveis runtime */
static int g_threads[16];
static int g_n_threads = 0;
static int g_gpu_cuda_cores = 896;
#define N_THREADS      g_n_threads
#define THREADS        g_threads
#define GPU_CUDA_CORES g_gpu_cuda_cores

/* ─── Construtores de arrays ─── */

/* Monta THREADS[] = {1, 2, 4, ..., max_t} (potências de 2 + max_t se nao for p2) */
static void construir_array_threads(int max_t) {
    if (max_t < 1) max_t = 1;
    g_n_threads = 0;
    g_threads[g_n_threads++] = 1;
    for (int t = 2; t < max_t && g_n_threads < 15; t *= 2)
        g_threads[g_n_threads++] = t;
    if (g_n_threads < 15)
        g_threads[g_n_threads++] = max_t;
}

/* Parseia string CSV de inteiros positivos ("1000,100000,1000000") em arr[].
   Retorna o numero de valores lidos (maximo: maxn). */
static int parse_int_csv(const char *csv, int *arr, int maxn) {
    int n = 0;
    char buf[256];
    strncpy(buf, csv, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *tok = strtok(buf, ", ");
    while (tok && n < maxn) {
        int v = atoi(tok);
        if (v > 0) arr[n++] = v;
        tok = strtok(NULL, ", ");
    }
    return n;
}

/* ─── I/O do arquivo de configuração hardware.cfg ─── */

#define HW_CFG_FILE "hardware.cfg"

static int config_carregar(HardwareConfig *hw) {
    FILE *f = fopen(HW_CFG_FILE, "r");
    if (!f) return 0;
    char linha[256];
    while (fgets(linha, sizeof(linha), f)) {
        if (linha[0] == '#' || linha[0] == '\n') continue;
        char *eq = strchr(linha, '=');
        if (!eq) continue;
        *eq = '\0';
        char *chave = linha, *valor = eq + 1;
        valor[strcspn(valor, "\r\n")] = '\0';
        /* remove espacos iniciais do valor */
        while (*valor == ' ' || *valor == '\t') valor++;
        if      (strcmp(chave, "cpu_nome")       == 0) strncpy(hw->cpu_nome,      valor, 63);
        else if (strcmp(chave, "cpu_nucleos")    == 0) hw->cpu_nucleos            = atoi(valor);
        else if (strcmp(chave, "cpu_threads")    == 0) hw->cpu_threads            = atoi(valor);
        else if (strcmp(chave, "gpu_nome")       == 0) strncpy(hw->gpu_nome,      valor, 63);
        else if (strcmp(chave, "gpu_cuda_cores") == 0) hw->gpu_cuda_cores         = atoi(valor);
        else if (strcmp(chave, "ram_gb")         == 0) hw->ram_gb                 = atoi(valor);
        else if (strcmp(chave, "n_repeticoes")   == 0) hw->n_repeticoes           = atoi(valor);
        else if (strcmp(chave, "volumes_busca")  == 0) strncpy(hw->volumes_busca, valor, 127);
        else if (strcmp(chave, "volumes_math")   == 0) strncpy(hw->volumes_math,  valor, 127);
    }
    fclose(f);
    return 1;
}

/* config_inicializar: le hardware.cfg obrigatoriamente.
   Se nao encontrar, imprime instrucao e encerra. */
static void config_inicializar(void) {
    if (!config_carregar(&g_hw)) {
        fprintf(stderr,
            "[ERRO] Arquivo '%s' nao encontrado.\n"
            "       Crie o arquivo com o seguinte conteudo minimo:\n"
            "\n"
            "         cpu_nome=MeuCPU\n"
            "         cpu_nucleos=4\n"
            "         cpu_threads=8\n"
            "         gpu_nome=MinhaGPU\n"
            "         gpu_cuda_cores=896\n"
            "         ram_gb=8\n"
            "         volumes_busca=1000,100000,1000000,10000000\n"
            "         volumes_math=100000,500000,1000000\n"
            "         n_repeticoes=3\n"
            "\n"
            "       Ajuste os valores para o seu hardware e rode novamente.\n",
            HW_CFG_FILE);
        exit(1);
    }
    /* defaults para campos opcionais ausentes */
    if (g_hw.n_repeticoes < 1) g_hw.n_repeticoes = 3;
    if (g_hw.volumes_busca[0] == '\0')
        strncpy(g_hw.volumes_busca, "1000,100000,1000000,10000000,20000000", 127);
    if (g_hw.volumes_math[0] == '\0')
        strncpy(g_hw.volumes_math, "100000,500000,1000000,5000000,10000000", 127);

    printf("[Config] Hardware carregado de %s\n", HW_CFG_FILE);
    g_gpu_cuda_cores = g_hw.gpu_cuda_cores;
    construir_array_threads(g_hw.cpu_threads);
    g_n_volumes = parse_int_csv(g_hw.volumes_busca, g_volumes, 16);
    if (g_n_volumes == 0) { g_volumes[0] = 1000; g_n_volumes = 1; }
    g_n_math_volumes = parse_int_csv(g_hw.volumes_math, g_math_volumes, 16);
    if (g_n_math_volumes == 0) { g_math_volumes[0] = 100000; g_n_math_volumes = 1; }
}

/* ═══════════════════════════════════════════════════════
   UTILITÁRIOS
═══════════════════════════════════════════════════════ */

/* Buffer e funcao de medicao com N repeticoes configuravel */
#define MAX_REP 9
static double g_rep_buf[MAX_REP]; /* scratch para loop de medicao */

/* Ordena v[0..n-1] in-place (insertion sort) e retorna o valor central */
static double medianaK(double *v, int n) {
    for (int i = 1; i < n; i++) {
        double key = v[i]; int j = i - 1;
        while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
        v[j + 1] = key;
    }
    return v[n / 2];
}

/* Retorna g_hw.n_repeticoes clampeado para [1, MAX_REP] */
static int nrep(void) {
    int n = g_hw.n_repeticoes;
    if (n < 1) n = 1;
    if (n > MAX_REP) n = MAX_REP;
    return n;
}

/* Formata inteiro com separador de milhar (ponto) */
static void fmt_int(char *buf, int n) {
    char tmp[32];
    int len = snprintf(tmp, sizeof(tmp), "%d", n);
    int out = 0;
    for (int i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0)
            buf[out++] = '.';
        buf[out++] = tmp[i];
    }
    buf[out] = '\0';
}

/* Cria diretorio (ignora erro se ja existir) */
static void plat_mkdir(const char *path) {
#ifdef _MSC_VER
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

/* ═══════════════════════════════════════════════════════
   COLETA GLOBAL DE RESULTADOS — busca
═══════════════════════════════════════════════════════ */
#define MAX_RESULTADOS 200
static BenchmarkResult todos_resultados[MAX_RESULTADOS];
static int n_resultados = 0;

static void imprimir_linha(BenchmarkResult *r) {
    if (n_resultados < MAX_RESULTADOS)
        todos_resultados[n_resultados++] = *r;
    char vol_str[16];
    fmt_int(vol_str, r->volume);
    printf("| %-12s | %-8s | %12s | %7d | %10.4f | %8.2fx | %10d |\n",
           r->algoritmo, r->modo, vol_str, r->threads,
           r->t_segundos, r->speedup, r->resultados);
}

static void imprimir_separador(void) {
    printf("|%s|%s|%s|%s|%s|%s|%s|\n",
           "--------------", "----------", "--------------",
           "---------", "------------", "----------", "------------");
}

static void imprimir_cabecalho(void) {
    printf("| %-12s | %-8s | %12s | %7s | %10s | %8s | %10s |\n",
           "Algoritmo", "Modo", "Volume", "Threads",
           "Tempo(s)", "Speedup", "Resultados");
    imprimir_separador();
}

/* ═══════════════════════════════════════════════════════
   COLETA GLOBAL DE RESULTADOS — matematica
═══════════════════════════════════════════════════════ */
#define MAX_MATH_RESULTADOS 100
static MathResult todos_math_resultados[MAX_MATH_RESULTADOS];
static int n_math_resultados = 0;

static void imprimir_linha_math(MathResult *r) {
    if (n_math_resultados < MAX_MATH_RESULTADOS)
        todos_math_resultados[n_math_resultados++] = *r;
    char vol_str[16];
    fmt_int(vol_str, r->volume);
    printf("| %-12s | %-8s | %12s | %7d | %10.4f | %8.2fx | %14.4f |\n",
           r->algoritmo, r->modo, vol_str, r->threads,
           r->t_segundos, r->speedup, r->valor);
}

/* ═══════════════════════════════════════════════════════
   SALVAR RUNS — novo formato, um JSON por run
═══════════════════════════════════════════════════════ */

/* Extrai run_id do timestamp ISO "YYYY-MM-DDTHH:MM:SS" -> "YYYYMMDD_HHMMSS" */
static void ts_to_run_id(const char *ts, char *run_id, size_t sz) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    sscanf(ts, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s);
    snprintf(run_id, sz, "%04d%02d%02d_%02d%02d%02d", y, mo, d, h, mi, s);
}

/* Escreve bloco "hardware" comum */
static void escrever_hw(FILE *f) {
    fprintf(f, "  \"hardware\": {\n");
    fprintf(f, "    \"cpu_nome\": \"%s\",\n",   g_hw.cpu_nome);
    fprintf(f, "    \"cpu_nucleos\": %d,\n",    g_hw.cpu_nucleos);
    fprintf(f, "    \"cpu_threads\": %d,\n",    g_hw.cpu_threads);
    fprintf(f, "    \"gpu_nome\": \"%s\",\n",   g_hw.gpu_nome);
    fprintf(f, "    \"gpu_cuda_cores\": %d,\n", g_hw.gpu_cuda_cores);
    fprintf(f, "    \"ram_gb\": %d\n",           g_hw.ram_gb);
    fprintf(f, "  },\n");
}

static void salvar_run_busca(const char *ts) {
    char run_id[32];
    ts_to_run_id(ts, run_id, sizeof(run_id));

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
    fprintf(f, "  \"timestamp\": \"%s\",\n", ts);
    fprintf(f, "  \"simulacao\": \"busca\",\n");
    fprintf(f, "  \"build\": \"%s\",\n", build);
    escrever_hw(f);
    fprintf(f, "  \"parametros\": {\n");
    fprintf(f, "    \"n_repeticoes\": %d,\n", g_hw.n_repeticoes);
    fprintf(f, "    \"volumes\": [");
    for (int i = 0; i < N_VOLUMES; i++)
        fprintf(f, "%d%s", VOLUMES[i], i < N_VOLUMES - 1 ? "," : "");
    fprintf(f, "],\n");
    fprintf(f, "    \"threads\": [");
    for (int i = 0; i < g_n_threads; i++)
        fprintf(f, "%d%s", g_threads[i], i < g_n_threads - 1 ? "," : "");
    fprintf(f, "],\n");
    fprintf(f, "    \"evento_bytes\": %zu,\n", sizeof(Event));
    fprintf(f, "    \"busca_min\": %.1f,\n",   (double)BUSCA_MIN);
    fprintf(f, "    \"busca_max\": %.1f,\n",   (double)BUSCA_MAX);
    fprintf(f, "    \"busca_alvo\": %.1f,\n",  (double)BUSCA_ALVO);
    fprintf(f, "    \"n_ids_hash\": %d\n",      N_IDS_HASH);
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

static void salvar_run_matematica(const char *ts) {
    char run_id[32];
    ts_to_run_id(ts, run_id, sizeof(run_id));

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
    fprintf(f, "  \"timestamp\": \"%s\",\n", ts);
    fprintf(f, "  \"simulacao\": \"matematica\",\n");
    fprintf(f, "  \"build\": \"%s\",\n", build);
    escrever_hw(f);
    fprintf(f, "  \"parametros\": {\n");
    fprintf(f, "    \"n_repeticoes\": %d,\n", g_hw.n_repeticoes);
    fprintf(f, "    \"math_volumes\": [");
    for (int i = 0; i < g_n_math_volumes; i++)
        fprintf(f, "%d%s", g_math_volumes[i], i < g_n_math_volumes - 1 ? "," : "");
    fprintf(f, "],\n");
    fprintf(f, "    \"threads\": [");
    for (int i = 0; i < g_n_threads; i++)
        fprintf(f, "%d%s", g_threads[i], i < g_n_threads - 1 ? "," : "");
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

/* ═══════════════════════════════════════════════════════
   BENCHMARK PRINCIPAL
═══════════════════════════════════════════════════════ */
int main(void) {
    /* Carrega hardware.cfg — obrigatorio; encerra com mensagem se ausente */
    config_inicializar();

#ifdef _OPENMP
    /*
     * Pinar threads nos nucleos fisicos antes de qualquer regiao paralela.
     * Deve ser feito via putenv() ANTES do runtime OpenMP inicializar.
     */
#ifdef _MSC_VER
    _putenv("OMP_PROC_BIND=close");
    _putenv("OMP_PLACES=cores");
#else
    putenv("OMP_PROC_BIND=close");
    putenv("OMP_PLACES=cores");
#endif
    printf("[OpenMP] OMP_PROC_BIND=close  OMP_PLACES=cores  (nucleos fisicos)\n");
#endif

#ifdef HAS_CUDA
    int cuda_ok = cuda_init();
#else
    int cuda_ok = 0;
    (void)cuda_ok;
#endif

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
    printf("  GPU : %-40s  (build CPU — sem GPU)\n",
           g_hw.gpu_nome);
#endif
    printf("  RAM : %d GB livres para benchmark\n", g_hw.ram_gb);
    printf("------------------------------------------------------------------\n");
    printf("  Threads OpenMP : ");
    for (int i = 0; i < g_n_threads; i++)
        printf("%d%s", g_threads[i], i < g_n_threads - 1 ? ", " : "\n");
    printf("  Repeticoes    : 1 warmup + %d cronometrados (mediana)\n",
           g_hw.n_repeticoes);
    printf("  Volumes busca : ");
    for (int i = 0; i < g_n_volumes; i++) {
        char buf[16]; fmt_int(buf, g_volumes[i]);
        double mb = (double)g_volumes[i] * 96.0 / (1024.0 * 1024.0);
        printf("%s (~%.0f MB)%s", buf, mb, i < g_n_volumes - 1 ? ", " : "\n");
    }
    printf("  Volumes math  : ");
    for (int i = 0; i < g_n_math_volumes; i++) {
        char buf[16]; fmt_int(buf, g_math_volumes[i]);
        printf("%s%s", buf, i < g_n_math_volumes - 1 ? ", " : "\n");
    }
    printf("  busca_linear [%.1f, %.1f]  |  busca_binaria alvo=%.1f  |  hash n_ids=%d\n",
           (double)BUSCA_MIN, (double)BUSCA_MAX, (double)BUSCA_ALVO, N_IDS_HASH);
    printf("==================================================================\n\n");

    /* ═══════════════════════════════════════════════════════
       LOOP DE BUSCA
    ═══════════════════════════════════════════════════════ */
    for (int vi = 0; vi < N_VOLUMES; vi++) {
        int n = VOLUMES[vi];
        char n_str[16];
        fmt_int(n_str, n);
        double mb = (double)n * sizeof(Event) / (1024.0 * 1024.0);

        printf(">>> Gerando %s eventos (%.2f MB)...\n", n_str, mb);
        fflush(stdout);

        Event *dados = gerar_eventos(n);
        if (!dados) { printf("Falha ao gerar %d eventos\n", n); continue; }

        Event *dados_ord = (Event *)malloc((size_t)n * sizeof(Event));
        memcpy(dados_ord, dados, (size_t)n * sizeof(Event));
        ordenar_por_valor(dados_ord, n);

        HashTable *ht = hash_criar(dados, n);

        int n_ids = (N_IDS_HASH < n) ? N_IDS_HASH : n;
        uint32_t *ids = (uint32_t *)malloc((size_t)n_ids * sizeof(uint32_t));
        for (int i = 0; i < n_ids; i++) {
            ids[i] = (uint32_t)((i * (n / n_ids)) + 1);
        }

        printf("\n--- Volume: %s eventos (%zu bytes/evento  ->  %.2f MB) ---\n",
               n_str, sizeof(Event), mb);
        imprimir_cabecalho();

        double t_serial_linear  = 0.0;
        double t_serial_binaria = 0.0;
        double t_serial_hash    = 0.0;

        double best_sp_linear  = 1.0, best_sp_binaria = 1.0,
               best_sp_hash = 1.0;
        char   best_lb_linear[24]  = "serial 1t";
        char   best_lb_binaria[24] = "serial 1t";
        char   best_lb_hash[24]    = "serial 1t";

        /* ── BUSCA LINEAR ── */
        {
            SearchResult r;

            /* warmup */
            { SearchResult _w = busca_linear_serial(dados, n, BUSCA_MIN, BUSCA_MAX);
              search_result_free(&_w); }
            /* timed */
            {
                int _nr = nrep(); double _tp = agora();
                for (int _ri = 0; _ri < _nr; _ri++) {
                    if (_ri > 0) search_result_free(&r);
                    r = busca_linear_serial(dados, n, BUSCA_MIN, BUSCA_MAX);
                    double _tn = agora(); g_rep_buf[_ri] = _tn - _tp; _tp = _tn;
                }
                t_serial_linear = medianaK(g_rep_buf, _nr);
            }
            int resultados = r.count;
            search_result_free(&r);

            BenchmarkResult br = {"linear", "serial", n, 1,
                                   t_serial_linear, 1.0, resultados};
            imprimir_linha(&br);

            for (int ti = 1; ti < N_THREADS; ti++) {
                int th = THREADS[ti];
                /* warmup */
                { SearchResult _w = busca_linear_openmp(dados, n, BUSCA_MIN, BUSCA_MAX, th);
                  search_result_free(&_w); }
                /* timed */
                double t_par;
                {
                    int _nr = nrep(); double _tp = agora();
                    for (int _ri = 0; _ri < _nr; _ri++) {
                        if (_ri > 0) search_result_free(&r);
                        r = busca_linear_openmp(dados, n, BUSCA_MIN, BUSCA_MAX, th);
                        double _tn = agora(); g_rep_buf[_ri] = _tn - _tp; _tp = _tn;
                    }
                    t_par = medianaK(g_rep_buf, _nr);
                }
                double speedup = t_serial_linear / t_par;
                BenchmarkResult bro = {"linear", "openmp", n, th,
                                        t_par, speedup, r.count};
                imprimir_linha(&bro);
                search_result_free(&r);
                if (speedup > best_sp_linear) {
                    best_sp_linear = speedup;
                    snprintf(best_lb_linear, sizeof(best_lb_linear), "openmp %dt", th);
                }
            }

#ifdef HAS_CUDA
            if (cuda_ok) {
                int _nr = nrep();
                double tt[MAX_REP], tk[MAX_REP];
                SearchResult rc;
                /* warmup */
                { SearchResult _w = busca_linear_cuda(dados, n, BUSCA_MIN, BUSCA_MAX,
                                                      &tt[0], &tk[0]);
                  search_result_free(&_w); }
                /* timed */
                int cuda_count = 0;
                for (int _ri = 0; _ri < _nr; _ri++) {
                    rc = busca_linear_cuda(dados, n, BUSCA_MIN, BUSCA_MAX,
                                           &tt[_ri], &tk[_ri]);
                    if (_ri == 0) cuda_count = rc.count;
                    search_result_free(&rc);
                }
                double t_med  = medianaK(tt, _nr);
                double tk_med = medianaK(tk, _nr);
                double sp_total  = t_serial_linear / t_med;
                double sp_kernel = t_serial_linear / tk_med;

                BenchmarkResult brc = {"linear", "cuda", n, GPU_CUDA_CORES,
                                        t_med, sp_total, cuda_count};
                brc.t_kernel_s = tk_med;   /* transferência PCIe = t_med - tk_med */
                imprimir_linha(&brc);

                printf("      [GPU real] kernel puro: %8.3f ms (%.2fx)  "
                       "| H2D+kernel (speedup acima): %8.3f ms\n",
                       tk_med * 1000.0, sp_kernel, t_med * 1000.0);

                if (sp_total > best_sp_linear) {
                    best_sp_linear = sp_total;
                    snprintf(best_lb_linear, sizeof(best_lb_linear), "cuda");
                }
            }
#endif
        }

        imprimir_separador();

        /* ── BUSCA BINÁRIA ── */
        {
            SearchResult r;

            /* warmup */
            { SearchResult _w = busca_binaria_serial(dados_ord, n, BUSCA_ALVO);
              search_result_free(&_w); }
            /* timed */
            {
                int _nr = nrep(); double _tp = agora();
                for (int _ri = 0; _ri < _nr; _ri++) {
                    if (_ri > 0) search_result_free(&r);
                    r = busca_binaria_serial(dados_ord, n, BUSCA_ALVO);
                    double _tn = agora(); g_rep_buf[_ri] = _tn - _tp; _tp = _tn;
                }
                t_serial_binaria = medianaK(g_rep_buf, _nr);
            }
            BenchmarkResult br = {"binaria", "serial", n, 1,
                                   t_serial_binaria, 1.0, r.count};
            imprimir_linha(&br);
            search_result_free(&r);

            for (int ti = 1; ti < N_THREADS; ti++) {
                int th = THREADS[ti];
                /* warmup */
                { SearchResult _w = busca_binaria_openmp(dados_ord, n, BUSCA_ALVO, th);
                  search_result_free(&_w); }
                /* timed */
                double t_par;
                {
                    int _nr = nrep(); double _tp = agora();
                    for (int _ri = 0; _ri < _nr; _ri++) {
                        if (_ri > 0) search_result_free(&r);
                        r = busca_binaria_openmp(dados_ord, n, BUSCA_ALVO, th);
                        double _tn = agora(); g_rep_buf[_ri] = _tn - _tp; _tp = _tn;
                    }
                    t_par = medianaK(g_rep_buf, _nr);
                }
                double speedup = t_serial_binaria / t_par;
                BenchmarkResult bro = {"binaria", "openmp", n, th,
                                        t_par, speedup, r.count};
                imprimir_linha(&bro);
                search_result_free(&r);
                if (speedup > best_sp_binaria) {
                    best_sp_binaria = speedup;
                    snprintf(best_lb_binaria, sizeof(best_lb_binaria), "openmp %dt", th);
                }
            }

#ifdef HAS_CUDA
            if (cuda_ok) {
                int _nr = nrep();
                double tt[MAX_REP], tk[MAX_REP];
                /* warmup */
                { SearchResult _w = busca_binaria_cuda(dados_ord, n, BUSCA_ALVO,
                                                       &tt[0], &tk[0]);
                  search_result_free(&_w); }
                /* timed */
                int cuda_count = 0;
                SearchResult rc;
                for (int _ri = 0; _ri < _nr; _ri++) {
                    rc = busca_binaria_cuda(dados_ord, n, BUSCA_ALVO,
                                           &tt[_ri], &tk[_ri]);
                    if (_ri == 0) cuda_count = rc.count;
                    search_result_free(&rc);
                }
                double t_med  = medianaK(tt, _nr);
                double tk_med = medianaK(tk, _nr);
                double sp_total  = t_serial_binaria / t_med;
                double sp_kernel = t_serial_binaria / tk_med;

                BenchmarkResult brc = {"binaria", "cuda", n, GPU_CUDA_CORES,
                                        t_med, sp_total, cuda_count};
                brc.t_kernel_s = tk_med;   /* transferência PCIe = t_med - tk_med */
                imprimir_linha(&brc);

                printf("      [GPU real] kernel puro: %8.3f ms (%.2fx)  "
                       "| H2D+kernel (speedup acima): %8.3f ms\n",
                       tk_med * 1000.0, sp_kernel, t_med * 1000.0);

                if (sp_total > best_sp_binaria) {
                    best_sp_binaria = sp_total;
                    snprintf(best_lb_binaria, sizeof(best_lb_binaria), "cuda");
                }
            }
#endif
        }

        imprimir_separador();

        /* ── HASH LOOKUP ── */
        {
            SearchResult r;
            int serial_found;
            #define HASH_SERIAL_BATCH(found_out) do {                       \
                (found_out) = 0;                                             \
                for (int _i = 0; _i < n_ids; _i++) {                        \
                    SearchResult _r = hash_lookup_serial(ht, ids[_i]);      \
                    (found_out) += _r.count;                                 \
                    search_result_free(&_r);                                 \
                }                                                            \
            } while(0)

            /* warmup */
            HASH_SERIAL_BATCH(serial_found);
            /* timed */
            {
                int _nr = nrep(); double _tp = agora();
                for (int _ri = 0; _ri < _nr; _ri++) {
                    HASH_SERIAL_BATCH(serial_found);
                    double _tn = agora(); g_rep_buf[_ri] = _tn - _tp; _tp = _tn;
                }
                t_serial_hash = medianaK(g_rep_buf, _nr);
            }
            BenchmarkResult br = {"hash", "serial", n, 1,
                                   t_serial_hash, 1.0, serial_found};
            imprimir_linha(&br);
            #undef HASH_SERIAL_BATCH

            for (int ti = 1; ti < N_THREADS; ti++) {
                int th = THREADS[ti];
                /* warmup */
                { SearchResult _w = hash_lookup_openmp(ht, ids, n_ids, th);
                  search_result_free(&_w); }
                /* timed */
                double t_par;
                {
                    int _nr = nrep(); double _tp = agora();
                    for (int _ri = 0; _ri < _nr; _ri++) {
                        if (_ri > 0) search_result_free(&r);
                        r = hash_lookup_openmp(ht, ids, n_ids, th);
                        double _tn = agora(); g_rep_buf[_ri] = _tn - _tp; _tp = _tn;
                    }
                    t_par = medianaK(g_rep_buf, _nr);
                }
                double speedup = t_serial_hash / t_par;
                BenchmarkResult bro = {"hash", "openmp", n, th,
                                        t_par, speedup, r.count};
                imprimir_linha(&bro);
                search_result_free(&r);
                if (speedup > best_sp_hash) {
                    best_sp_hash = speedup;
                    snprintf(best_lb_hash, sizeof(best_lb_hash), "openmp %dt", th);
                }
            }

#ifdef HAS_CUDA
            if (cuda_ok) {
                int _nr = nrep();
                double tt[MAX_REP], tk[MAX_REP];
                /* warmup */
                { SearchResult _w = hash_lookup_cuda(ht, ids, n_ids, &tt[0], &tk[0]);
                  search_result_free(&_w); }
                /* timed */
                int cuda_count = 0;
                SearchResult rc;
                for (int _ri = 0; _ri < _nr; _ri++) {
                    rc = hash_lookup_cuda(ht, ids, n_ids, &tt[_ri], &tk[_ri]);
                    if (_ri == 0) cuda_count = rc.count;
                    search_result_free(&rc);
                }
                double t_med  = medianaK(tt, _nr);
                double tk_med = medianaK(tk, _nr);
                double sp_total  = t_serial_hash / t_med;
                double sp_kernel = t_serial_hash / tk_med;

                BenchmarkResult brc = {"hash", "cuda", n, GPU_CUDA_CORES,
                                        t_med, sp_total, cuda_count};
                brc.t_kernel_s = tk_med;   /* transferência PCIe = t_med - tk_med */
                imprimir_linha(&brc);

                printf("      [GPU real] kernel puro: %8.3f ms (%.2fx)  "
                       "| H2D+kernel (speedup acima): %8.3f ms\n",
                       tk_med * 1000.0, sp_kernel, t_med * 1000.0);

                if (sp_total > best_sp_hash) {
                    best_sp_hash = sp_total;
                    snprintf(best_lb_hash, sizeof(best_lb_hash), "cuda");
                }
            }
#endif
        }

        printf("|%s|%s|%s|%s|%s|%s|%s|\n",
               "==============", "==========", "==============",
               "=========", "============", "==========", "============");

        printf("  Melhor speedup:  linear=%.2fx (%s)  binaria=%.2fx (%s)"
               "  hash=%.2fx (%s)\n\n",
               best_sp_linear,  best_lb_linear,
               best_sp_binaria, best_lb_binaria,
               best_sp_hash,    best_lb_hash);

        hash_destruir(ht);
        free(ids);
        free(dados_ord);
        free(dados);
    }

    printf("\nLegenda (busca):\n");
    printf("  Speedup S(N) = T_serial / T_paralelo\n");
    printf("  Amdahl:    S(N) = 1 / (fs + fp/N)\n");
    printf("  Gustafson: S(N) = N - fs*(N-1)\n");
    printf("  cuda:    tempo_s = H2D(PCIe) + kernel;  transferencia = tempo_s - kernel\n\n");

    /* ═══════════════════════════════════════════════════════
       LOOP DE SIMULACAO MATEMATICA
    ═══════════════════════════════════════════════════════ */
    printf("==================================================================\n");
    printf("  SIMULACAO MATEMATICA DENSA (Monte Carlo Pi + Mandelbrot)\n");
    printf("==================================================================\n\n");

    for (int vi = 0; vi < g_n_math_volumes; vi++) {
        int n = g_math_volumes[vi];
        char n_str[16];
        fmt_int(n_str, n);

        printf("\n--- Math Volume: %s ---\n", n_str);
        printf("| %-12s | %-8s | %12s | %7s | %10s | %8s | %14s |\n",
               "Algoritmo", "Modo", "Volume", "Threads", "Tempo(s)", "Speedup", "Valor");
        printf("|%s|%s|%s|%s|%s|%s|%s|\n",
               "--------------", "----------", "--------------",
               "---------", "------------", "----------", "----------------");

        /* ── MONTE CARLO ── */
        double t_serial_mc = 0.0;
        {
            /* warmup */
            montecarlo_serial((long)n);
            /* timed */
            double v_serial;
            {
                int _nr = nrep(); double _tp = agora(), _vsum = 0.0;
                for (int _ri = 0; _ri < _nr; _ri++) {
                    double _v = montecarlo_serial((long)n);
                    double _tn = agora(); g_rep_buf[_ri] = _tn - _tp; _tp = _tn;
                    _vsum += _v;
                }
                t_serial_mc = medianaK(g_rep_buf, _nr);
                v_serial = _vsum / _nr;
            }
            MathResult mr = {0};
            strcpy(mr.algoritmo, "montecarlo");
            strcpy(mr.modo, "serial");
            mr.volume = n; mr.threads = 1;
            mr.t_segundos = t_serial_mc; mr.speedup = 1.0; mr.valor = v_serial;
            imprimir_linha_math(&mr);

            /* OpenMP */
            for (int ti = 1; ti < N_THREADS; ti++) {
                int th = THREADS[ti];
                montecarlo_openmp((long)n, th); /* warmup */
                double t_par, val_avg;
                {
                    int _nr = nrep(); double _tp = agora(), _vsum = 0.0;
                    for (int _ri = 0; _ri < _nr; _ri++) {
                        double _v = montecarlo_openmp((long)n, th);
                        double _tn = agora(); g_rep_buf[_ri] = _tn - _tp; _tp = _tn;
                        _vsum += _v;
                    }
                    t_par = medianaK(g_rep_buf, _nr);
                    val_avg = _vsum / _nr;
                }
                double speedup = (t_par > 0.0) ? t_serial_mc / t_par : 1.0;
                MathResult mro = {0};
                strcpy(mro.algoritmo, "montecarlo");
                strcpy(mro.modo, "openmp");
                mro.volume = n; mro.threads = th;
                mro.t_segundos = t_par; mro.speedup = speedup; mro.valor = val_avg;
                imprimir_linha_math(&mro);
            }

#ifdef HAS_CUDA
            if (cuda_ok) {
                int _nr = nrep();
                double total[MAX_REP], kern[MAX_REP];
                /* warmup */
                { double _tk; montecarlo_cuda((long)n, &_tk); }
                /* timed: total = parede (malloc+kernel+D2H+free) via agora();
                   kern = so o kernel (cudaEvent, vindo do wrapper).
                   Monte Carlo gera os pontos no device -> "transferencia" = overhead de setup. */
                double _vsum = 0.0;
                for (int _ri = 0; _ri < _nr; _ri++) {
                    double _t0 = agora();
                    double _v = montecarlo_cuda((long)n, &kern[_ri]);
                    total[_ri] = agora() - _t0;
                    _vsum += _v;
                }
                double t_med  = medianaK(total, _nr);
                double tk_med = medianaK(kern, _nr);
                double speedup = (t_med > 0.0) ? t_serial_mc / t_med : 1.0;
                MathResult mrc = {0};
                strcpy(mrc.algoritmo, "montecarlo");
                strcpy(mrc.modo, "cuda");
                mrc.volume = n; mrc.threads = GPU_CUDA_CORES;
                mrc.t_segundos = t_med; mrc.speedup = speedup; mrc.valor = _vsum / _nr;
                mrc.t_kernel_s = tk_med;
                imprimir_linha_math(&mrc);
            }
#endif
        }

        printf("|%s|%s|%s|%s|%s|%s|%s|\n",
               "--------------", "----------", "--------------",
               "---------", "------------", "----------", "----------------");

        /* ── MANDELBROT ── */
        double t_serial_mb = 0.0;
        {
            /* warmup */
            mandelbrot_serial(n);
            /* timed */
            double v_serial;
            {
                int _nr = nrep(); double _tp = agora(), _vsum = 0.0;
                for (int _ri = 0; _ri < _nr; _ri++) {
                    double _v = mandelbrot_serial(n);
                    double _tn = agora(); g_rep_buf[_ri] = _tn - _tp; _tp = _tn;
                    _vsum += _v;
                }
                t_serial_mb = medianaK(g_rep_buf, _nr);
                v_serial = _vsum / _nr;
            }
            MathResult mr = {0};
            strcpy(mr.algoritmo, "mandelbrot");
            strcpy(mr.modo, "serial");
            mr.volume = n; mr.threads = 1;
            mr.t_segundos = t_serial_mb; mr.speedup = 1.0; mr.valor = v_serial;
            imprimir_linha_math(&mr);

            /* OpenMP */
            for (int ti = 1; ti < N_THREADS; ti++) {
                int th = THREADS[ti];
                mandelbrot_openmp(n, th); /* warmup */
                double t_par, vavg;
                {
                    int _nr = nrep(); double _tp = agora(), _vsum = 0.0;
                    for (int _ri = 0; _ri < _nr; _ri++) {
                        double _v = mandelbrot_openmp(n, th);
                        double _tn = agora(); g_rep_buf[_ri] = _tn - _tp; _tp = _tn;
                        _vsum += _v;
                    }
                    t_par = medianaK(g_rep_buf, _nr);
                    vavg = _vsum / _nr;
                }
                double speedup = (t_par > 0.0) ? t_serial_mb / t_par : 1.0;
                MathResult mro = {0};
                strcpy(mro.algoritmo, "mandelbrot");
                strcpy(mro.modo, "openmp");
                mro.volume = n; mro.threads = th;
                mro.t_segundos = t_par; mro.speedup = speedup; mro.valor = vavg;
                imprimir_linha_math(&mro);
            }

#ifdef HAS_CUDA
            if (cuda_ok) {
                int _nr = nrep();
                double total[MAX_REP], kern[MAX_REP];
                /* warmup */
                { double _tk; mandelbrot_cuda(n, &_tk); }
                /* timed: total = parede (malloc+kernel+D2H+free) via agora();
                   kern = so o kernel (cudaEvent, vindo do wrapper).
                   Mandelbrot calcula cada pixel no device -> "transferencia" = overhead de setup. */
                double _vsum = 0.0;
                for (int _ri = 0; _ri < _nr; _ri++) {
                    double _t0 = agora();
                    double _v = mandelbrot_cuda(n, &kern[_ri]);
                    total[_ri] = agora() - _t0;
                    _vsum += _v;
                }
                double t_med  = medianaK(total, _nr);
                double tk_med = medianaK(kern, _nr);
                double speedup = (t_med > 0.0) ? t_serial_mb / t_med : 1.0;
                MathResult mrc = {0};
                strcpy(mrc.algoritmo, "mandelbrot");
                strcpy(mrc.modo, "cuda");
                mrc.volume = n; mrc.threads = GPU_CUDA_CORES;
                mrc.t_segundos = t_med; mrc.speedup = speedup; mrc.valor = _vsum / _nr;
                mrc.t_kernel_s = tk_med;
                imprimir_linha_math(&mrc);
            }
#endif
        }

        printf("|%s|%s|%s|%s|%s|%s|%s|\n",
               "==============", "==========", "==============",
               "=========", "============", "==========", "================");
        (void)t_serial_mc; (void)t_serial_mb;
    }

    /* ─── Gerar arquivos de saída ─── */
    time_t now_busca = time(NULL);
    char ts_busca[32];
    strftime(ts_busca, sizeof(ts_busca), "%Y-%m-%dT%H:%M:%S", localtime(&now_busca));

    /* Pequena pausa para que o timestamp da matematica seja diferente */
    time_t now_math = time(NULL) + 60; /* offset nominal de 1 minuto */
    char ts_math[32];
    strftime(ts_math, sizeof(ts_math), "%Y-%m-%dT%H:%M:%S", localtime(&now_math));

    salvar_run_busca(ts_busca);
    salvar_run_matematica(ts_math);

    printf("\n>>> Arquivos salvos em runs/busca_%s/ e runs/matematica_%s/\n",
#ifdef HAS_CUDA
           "cuda", "cuda"
#else
           "cpu", "cpu"
#endif
    );

    return 0;
}
