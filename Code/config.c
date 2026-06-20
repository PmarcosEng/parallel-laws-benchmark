/* Suprime avisos de funções "inseguras" do MSVC (strncpy, strtok, etc.). */
#ifdef _MSC_VER
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HW_CFG_FILE "hardware.cfg"

/* ─── Estado global (declarado extern em config.h) ─── */

HardwareConfig g_hw = {
    "CPU", 4, 8, "GPU", 896, 8, 3,
    "1000,100000,1000000,10000000,20000000",
    "100000,500000,1000000,5000000,10000000"
};

int volumes_busca[MAX_GRADE];
int n_volumes_busca = 0;

int volumes_math[MAX_GRADE];
int n_volumes_math = 0;

int threads_teste[MAX_GRADE];
int n_threads_teste = 0;

/* ─── Construtores de grade ─── */

/* Monta threads_teste[] = {1, 2, 4, ..., max_threads}
   (potências de 2 + max_threads no fim se ele não for potência de 2). */
static void construir_grade_threads(int max_threads) {
    if (max_threads < 1) max_threads = 1;
    n_threads_teste = 0;
    threads_teste[n_threads_teste++] = 1;
    for (int t = 2; t < max_threads && n_threads_teste < MAX_GRADE - 1; t *= 2)
        threads_teste[n_threads_teste++] = t;
    if (n_threads_teste < MAX_GRADE - 1)
        threads_teste[n_threads_teste++] = max_threads;
}

/* Parseia CSV de inteiros positivos ("1000,100000,...") em destino[].
   Devolve quantos valores foram lidos (no máximo capacidade). */
static int parse_int_csv(const char *csv, int *destino, int capacidade) {
    int n_lidos = 0;
    char buf[256];
    strncpy(buf, csv, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *token = strtok(buf, ", ");
    while (token && n_lidos < capacidade) {
        int valor = atoi(token);
        if (valor > 0) destino[n_lidos++] = valor;
        token = strtok(NULL, ", ");
    }
    return n_lidos;
}

/* ─── Leitura do arquivo hardware.cfg ─── */

/* Devolve 1 se conseguiu abrir e ler o arquivo, 0 se ele não existe. */
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
        while (*valor == ' ' || *valor == '\t') valor++;   /* trim à esquerda */
        if      (strcmp(chave, "cpu_nome")       == 0) strncpy(hw->cpu_nome,          valor, 63);
        else if (strcmp(chave, "cpu_nucleos")    == 0) hw->cpu_nucleos              = atoi(valor);
        else if (strcmp(chave, "cpu_threads")    == 0) hw->cpu_threads              = atoi(valor);
        else if (strcmp(chave, "gpu_nome")       == 0) strncpy(hw->gpu_nome,          valor, 63);
        else if (strcmp(chave, "gpu_cuda_cores") == 0) hw->gpu_cuda_cores           = atoi(valor);
        else if (strcmp(chave, "ram_gb")         == 0) hw->ram_gb                   = atoi(valor);
        else if (strcmp(chave, "n_repeticoes")   == 0) hw->n_repeticoes             = atoi(valor);
        else if (strcmp(chave, "volumes_busca")  == 0) strncpy(hw->csv_volumes_busca, valor, 127);
        else if (strcmp(chave, "volumes_math")   == 0) strncpy(hw->csv_volumes_math,  valor, 127);
    }
    fclose(f);
    return 1;
}

void config_inicializar(void) {
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
    if (g_hw.csv_volumes_busca[0] == '\0')
        strncpy(g_hw.csv_volumes_busca, "1000,100000,1000000,10000000,20000000", 127);
    if (g_hw.csv_volumes_math[0] == '\0')
        strncpy(g_hw.csv_volumes_math, "100000,500000,1000000,5000000,10000000", 127);

    printf("[Config] Hardware carregado de %s\n", HW_CFG_FILE);

    construir_grade_threads(g_hw.cpu_threads);

    n_volumes_busca = parse_int_csv(g_hw.csv_volumes_busca, volumes_busca, MAX_GRADE);
    if (n_volumes_busca == 0) { volumes_busca[0] = 1000; n_volumes_busca = 1; }

    n_volumes_math = parse_int_csv(g_hw.csv_volumes_math, volumes_math, MAX_GRADE);
    if (n_volumes_math == 0) { volumes_math[0] = 100000; n_volumes_math = 1; }
}
