/* Suprime avisos de funcoes "inseguras" do MSVC (localtime, _putenv, etc.). */
#ifdef _MSC_VER
#  define _CRT_SECURE_NO_WARNINGS
#endif
/* No GCC/MinGW destravamos extensoes POSIX/GNU dos headers. _GNU_SOURCE e o
   superconjunto: cobre putenv (extensao XSI). MSVC ignora e usa o caminho proprio. */
#ifndef _MSC_VER
#  define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"     /* g_hw, grades de volumes/threads, parametros        */
#include "timing.h"     /* agora(), mediana(), CRONOMETRAR_MEDIANA            */
#include "report.h"     /* banner, tabelas, JSON — toda a apresentacao        */
#include "event.h"
#include "generator.h"
#include "search.h"
#include "sim_math.h"
#ifdef HAS_CUDA
#  include "search_cuda.h"
#  include "sim_math_cuda.h"
#endif

/* ═══════════════════════════════════════════════════════
   BENCHMARK — orquestracao.

   Este arquivo NAO mede tempo na unha, NAO imprime tabelas e NAO le
   configuracao: ele coordena. Para cada volume, gera os dados, dispara
   cada algoritmo (serial / OpenMP / CUDA) e entrega os resultados ao
   modulo report. A cronometragem vem de timing (CRONOMETRAR_MEDIANA),
   a saida vem de report, a entrada vem de config.

   Os kernels de verdade (a "simulacao") moram em search.c e sim_math.c.
═══════════════════════════════════════════════════════ */

/* ═══════════════════════════════════════════════════════
   BUSCA — uma funcao por algoritmo. Cada uma roda serial (baseline),
   varre os niveis de thread em OpenMP e, no build CUDA, o modo GPU.
   Devolve o melhor speedup observado para o resumo do volume.
═══════════════════════════════════════════════════════ */

static MelhorSpeedup rodar_busca_linear(Event *dados, int volume, int cuda_ok) {
    int n_rep = repeticoes_validas(g_hw.n_repeticoes);
    MelhorSpeedup melhor = { 1.0, "serial 1t" };

    /* baseline serial */
    SearchResult r = {0};
    { SearchResult w = busca_linear_serial(dados, volume, BUSCA_MIN, BUSCA_MAX);
      search_result_free(&w); }                                   /* warmup */
    double t_serial;
    CRONOMETRAR_MEDIANA(t_serial, n_rep,
                        search_result_free(&r),
                        r = busca_linear_serial(dados, volume, BUSCA_MIN, BUSCA_MAX));
    int resultados = r.count;
    search_result_free(&r);
    BenchmarkResult linha_serial = {"linear", "serial", volume, 1, t_serial, 1.0, resultados};
    registrar_resultado_busca(&linha_serial);

    /* OpenMP — um nivel de thread por vez */
    for (int i = 1; i < n_threads_teste; i++) {
        int threads = threads_teste[i];
        { SearchResult w = busca_linear_openmp(dados, volume, BUSCA_MIN, BUSCA_MAX, threads);
          search_result_free(&w); }                               /* warmup */
        double t_par;
        SearchResult rp = {0};
        CRONOMETRAR_MEDIANA(t_par, n_rep,
                            search_result_free(&rp),
                            rp = busca_linear_openmp(dados, volume, BUSCA_MIN, BUSCA_MAX, threads));
        double speedup = t_serial / t_par;
        BenchmarkResult linha = {"linear", "openmp", volume, threads, t_par, speedup, rp.count};
        registrar_resultado_busca(&linha);
        search_result_free(&rp);
        if (speedup > melhor.speedup) {
            melhor.speedup = speedup;
            snprintf(melhor.rotulo, sizeof(melhor.rotulo), "openmp %dt", threads);
        }
    }

#ifdef HAS_CUDA
    if (cuda_ok) {
        double tt[MAX_REPETICOES], tk[MAX_REPETICOES];
        { SearchResult w = busca_linear_cuda(dados, volume, BUSCA_MIN, BUSCA_MAX, &tt[0], &tk[0]);
          search_result_free(&w); }                               /* warmup */
        int cuda_count = 0;
        for (int rep = 0; rep < n_rep; rep++) {
            SearchResult rc = busca_linear_cuda(dados, volume, BUSCA_MIN, BUSCA_MAX, &tt[rep], &tk[rep]);
            if (rep == 0) cuda_count = rc.count;
            search_result_free(&rc);
        }
        double t_med  = mediana(tt, n_rep);
        double tk_med = mediana(tk, n_rep);
        double sp_total  = t_serial / t_med;
        double sp_kernel = t_serial / tk_med;
        BenchmarkResult linha_cuda = {"linear", "cuda", volume, g_hw.gpu_cuda_cores,
                                       t_med, sp_total, cuda_count};
        linha_cuda.t_kernel_s = tk_med;   /* transferencia PCIe = t_med - tk_med */
        registrar_resultado_busca(&linha_cuda);
        printf("      [GPU real] kernel puro: %8.3f ms (%.2fx)  "
               "| H2D+kernel (speedup acima): %8.3f ms\n",
               tk_med * 1000.0, sp_kernel, t_med * 1000.0);
        if (sp_total > melhor.speedup) {
            melhor.speedup = sp_total;
            snprintf(melhor.rotulo, sizeof(melhor.rotulo), "cuda");
        }
    }
#else
    (void)cuda_ok;
#endif
    return melhor;
}

static MelhorSpeedup rodar_busca_binaria(Event *dados_ord, int volume, int cuda_ok) {
    int n_rep = repeticoes_validas(g_hw.n_repeticoes);
    MelhorSpeedup melhor = { 1.0, "serial 1t" };

    /* baseline serial */
    SearchResult r = {0};
    { SearchResult w = busca_binaria_serial(dados_ord, volume, BUSCA_ALVO);
      search_result_free(&w); }                                   /* warmup */
    double t_serial;
    CRONOMETRAR_MEDIANA(t_serial, n_rep,
                        search_result_free(&r),
                        r = busca_binaria_serial(dados_ord, volume, BUSCA_ALVO));
    BenchmarkResult linha_serial = {"binaria", "serial", volume, 1, t_serial, 1.0, r.count};
    registrar_resultado_busca(&linha_serial);
    search_result_free(&r);

    /* OpenMP */
    for (int i = 1; i < n_threads_teste; i++) {
        int threads = threads_teste[i];
        { SearchResult w = busca_binaria_openmp(dados_ord, volume, BUSCA_ALVO, threads);
          search_result_free(&w); }                               /* warmup */
        double t_par;
        SearchResult rp = {0};
        CRONOMETRAR_MEDIANA(t_par, n_rep,
                            search_result_free(&rp),
                            rp = busca_binaria_openmp(dados_ord, volume, BUSCA_ALVO, threads));
        double speedup = t_serial / t_par;
        BenchmarkResult linha = {"binaria", "openmp", volume, threads, t_par, speedup, rp.count};
        registrar_resultado_busca(&linha);
        search_result_free(&rp);
        if (speedup > melhor.speedup) {
            melhor.speedup = speedup;
            snprintf(melhor.rotulo, sizeof(melhor.rotulo), "openmp %dt", threads);
        }
    }

#ifdef HAS_CUDA
    if (cuda_ok) {
        double tt[MAX_REPETICOES], tk[MAX_REPETICOES];
        { SearchResult w = busca_binaria_cuda(dados_ord, volume, BUSCA_ALVO, &tt[0], &tk[0]);
          search_result_free(&w); }                               /* warmup */
        int cuda_count = 0;
        for (int rep = 0; rep < n_rep; rep++) {
            SearchResult rc = busca_binaria_cuda(dados_ord, volume, BUSCA_ALVO, &tt[rep], &tk[rep]);
            if (rep == 0) cuda_count = rc.count;
            search_result_free(&rc);
        }
        double t_med  = mediana(tt, n_rep);
        double tk_med = mediana(tk, n_rep);
        double sp_total  = t_serial / t_med;
        double sp_kernel = t_serial / tk_med;
        BenchmarkResult linha_cuda = {"binaria", "cuda", volume, g_hw.gpu_cuda_cores,
                                       t_med, sp_total, cuda_count};
        linha_cuda.t_kernel_s = tk_med;
        registrar_resultado_busca(&linha_cuda);
        printf("      [GPU real] kernel puro: %8.3f ms (%.2fx)  "
               "| H2D+kernel (speedup acima): %8.3f ms\n",
               tk_med * 1000.0, sp_kernel, t_med * 1000.0);
        if (sp_total > melhor.speedup) {
            melhor.speedup = sp_total;
            snprintf(melhor.rotulo, sizeof(melhor.rotulo), "cuda");
        }
    }
#else
    (void)cuda_ok;
#endif
    return melhor;
}

/* Soma quantos dos n_ids existem na tabela — equivalente serial do batch
   de lookups que o modo OpenMP/CUDA faz de uma vez. */
static int hash_lookup_serial_batch(HashTable *ht, uint32_t *ids, int n_ids) {
    int encontrados = 0;
    for (int i = 0; i < n_ids; i++) {
        SearchResult r = hash_lookup_serial(ht, ids[i]);
        encontrados += r.count;
        search_result_free(&r);
    }
    return encontrados;
}

static MelhorSpeedup rodar_busca_hash(HashTable *ht, uint32_t *ids, int n_ids,
                                      int volume, int cuda_ok) {
    int n_rep = repeticoes_validas(g_hw.n_repeticoes);
    MelhorSpeedup melhor = { 1.0, "serial 1t" };

    /* baseline serial */
    int serial_found = hash_lookup_serial_batch(ht, ids, n_ids);   /* warmup */
    double t_serial;
    CRONOMETRAR_MEDIANA(t_serial, n_rep, (void)0,
                        serial_found = hash_lookup_serial_batch(ht, ids, n_ids));
    BenchmarkResult linha_serial = {"hash", "serial", volume, 1, t_serial, 1.0, serial_found};
    registrar_resultado_busca(&linha_serial);

    /* OpenMP */
    for (int i = 1; i < n_threads_teste; i++) {
        int threads = threads_teste[i];
        { SearchResult w = hash_lookup_openmp(ht, ids, n_ids, threads);
          search_result_free(&w); }                               /* warmup */
        double t_par;
        SearchResult rp = {0};
        CRONOMETRAR_MEDIANA(t_par, n_rep,
                            search_result_free(&rp),
                            rp = hash_lookup_openmp(ht, ids, n_ids, threads));
        double speedup = t_serial / t_par;
        BenchmarkResult linha = {"hash", "openmp", volume, threads, t_par, speedup, rp.count};
        registrar_resultado_busca(&linha);
        search_result_free(&rp);
        if (speedup > melhor.speedup) {
            melhor.speedup = speedup;
            snprintf(melhor.rotulo, sizeof(melhor.rotulo), "openmp %dt", threads);
        }
    }

#ifdef HAS_CUDA
    if (cuda_ok) {
        double tt[MAX_REPETICOES], tk[MAX_REPETICOES];
        { SearchResult w = hash_lookup_cuda(ht, ids, n_ids, &tt[0], &tk[0]);
          search_result_free(&w); }                               /* warmup */
        int cuda_count = 0;
        for (int rep = 0; rep < n_rep; rep++) {
            SearchResult rc = hash_lookup_cuda(ht, ids, n_ids, &tt[rep], &tk[rep]);
            if (rep == 0) cuda_count = rc.count;
            search_result_free(&rc);
        }
        double t_med  = mediana(tt, n_rep);
        double tk_med = mediana(tk, n_rep);
        double sp_total  = t_serial / t_med;
        double sp_kernel = t_serial / tk_med;
        BenchmarkResult linha_cuda = {"hash", "cuda", volume, g_hw.gpu_cuda_cores,
                                       t_med, sp_total, cuda_count};
        linha_cuda.t_kernel_s = tk_med;
        registrar_resultado_busca(&linha_cuda);
        printf("      [GPU real] kernel puro: %8.3f ms (%.2fx)  "
               "| H2D+kernel (speedup acima): %8.3f ms\n",
               tk_med * 1000.0, sp_kernel, t_med * 1000.0);
        if (sp_total > melhor.speedup) {
            melhor.speedup = sp_total;
            snprintf(melhor.rotulo, sizeof(melhor.rotulo), "cuda");
        }
    }
#else
    (void)cuda_ok;
#endif
    return melhor;
}

/* ═══════════════════════════════════════════════════════
   SIMULACAO MATEMATICA DENSA — Monte Carlo Pi e Mandelbrot.
   Aqui o resultado de cada chamada e um numero (pi estimado / total de
   iteracoes); acumulamos a media entre repeticoes para registrar o valor.
═══════════════════════════════════════════════════════ */

static void rodar_montecarlo(int volume, int cuda_ok) {
    int n_rep = repeticoes_validas(g_hw.n_repeticoes);

    /* serial */
    montecarlo_serial((long)volume);                              /* warmup */
    double t_serial, soma = 0.0;
    CRONOMETRAR_MEDIANA(t_serial, n_rep, (void)0,
                        soma += montecarlo_serial((long)volume));
    MathResult linha = {0};
    strcpy(linha.algoritmo, "montecarlo");
    strcpy(linha.modo, "serial");
    linha.volume = volume; linha.threads = 1;
    linha.t_segundos = t_serial; linha.speedup = 1.0; linha.valor = soma / n_rep;
    registrar_resultado_math(&linha);

    /* OpenMP */
    for (int i = 1; i < n_threads_teste; i++) {
        int threads = threads_teste[i];
        montecarlo_openmp((long)volume, threads);                 /* warmup */
        double t_par, soma_par = 0.0;
        CRONOMETRAR_MEDIANA(t_par, n_rep, (void)0,
                            soma_par += montecarlo_openmp((long)volume, threads));
        double speedup = (t_par > 0.0) ? t_serial / t_par : 1.0;
        MathResult linha_omp = {0};
        strcpy(linha_omp.algoritmo, "montecarlo");
        strcpy(linha_omp.modo, "openmp");
        linha_omp.volume = volume; linha_omp.threads = threads;
        linha_omp.t_segundos = t_par; linha_omp.speedup = speedup; linha_omp.valor = soma_par / n_rep;
        registrar_resultado_math(&linha_omp);
    }

#ifdef HAS_CUDA
    if (cuda_ok) {
        double total[MAX_REPETICOES], kern[MAX_REPETICOES];
        { double tk; montecarlo_cuda((long)volume, &tk); }        /* warmup */
        /* total = parede (malloc+kernel+D2H+free) via agora(); kern = so o kernel
           (cudaEvent, vindo do wrapper). Os pontos nascem no device, entao a
           "transferencia" e o overhead de setup. */
        double soma_cuda = 0.0;
        for (int rep = 0; rep < n_rep; rep++) {
            double t0 = agora();
            soma_cuda += montecarlo_cuda((long)volume, &kern[rep]);
            total[rep] = agora() - t0;
        }
        double t_med  = mediana(total, n_rep);
        double tk_med = mediana(kern, n_rep);
        double speedup = (t_med > 0.0) ? t_serial / t_med : 1.0;
        MathResult linha_cuda = {0};
        strcpy(linha_cuda.algoritmo, "montecarlo");
        strcpy(linha_cuda.modo, "cuda");
        linha_cuda.volume = volume; linha_cuda.threads = g_hw.gpu_cuda_cores;
        linha_cuda.t_segundos = t_med; linha_cuda.speedup = speedup;
        linha_cuda.valor = soma_cuda / n_rep; linha_cuda.t_kernel_s = tk_med;
        registrar_resultado_math(&linha_cuda);
    }
#else
    (void)cuda_ok;
#endif
}

static void rodar_mandelbrot(int volume, int cuda_ok) {
    int n_rep = repeticoes_validas(g_hw.n_repeticoes);

    /* serial */
    mandelbrot_serial(volume);                                    /* warmup */
    double t_serial, soma = 0.0;
    CRONOMETRAR_MEDIANA(t_serial, n_rep, (void)0,
                        soma += mandelbrot_serial(volume));
    MathResult linha = {0};
    strcpy(linha.algoritmo, "mandelbrot");
    strcpy(linha.modo, "serial");
    linha.volume = volume; linha.threads = 1;
    linha.t_segundos = t_serial; linha.speedup = 1.0; linha.valor = soma / n_rep;
    registrar_resultado_math(&linha);

    /* OpenMP */
    for (int i = 1; i < n_threads_teste; i++) {
        int threads = threads_teste[i];
        mandelbrot_openmp(volume, threads);                       /* warmup */
        double t_par, soma_par = 0.0;
        CRONOMETRAR_MEDIANA(t_par, n_rep, (void)0,
                            soma_par += mandelbrot_openmp(volume, threads));
        double speedup = (t_par > 0.0) ? t_serial / t_par : 1.0;
        MathResult linha_omp = {0};
        strcpy(linha_omp.algoritmo, "mandelbrot");
        strcpy(linha_omp.modo, "openmp");
        linha_omp.volume = volume; linha_omp.threads = threads;
        linha_omp.t_segundos = t_par; linha_omp.speedup = speedup; linha_omp.valor = soma_par / n_rep;
        registrar_resultado_math(&linha_omp);
    }

#ifdef HAS_CUDA
    if (cuda_ok) {
        double total[MAX_REPETICOES], kern[MAX_REPETICOES];
        { double tk; mandelbrot_cuda(volume, &tk); }              /* warmup */
        double soma_cuda = 0.0;
        for (int rep = 0; rep < n_rep; rep++) {
            double t0 = agora();
            soma_cuda += mandelbrot_cuda(volume, &kern[rep]);
            total[rep] = agora() - t0;
        }
        double t_med  = mediana(total, n_rep);
        double tk_med = mediana(kern, n_rep);
        double speedup = (t_med > 0.0) ? t_serial / t_med : 1.0;
        MathResult linha_cuda = {0};
        strcpy(linha_cuda.algoritmo, "mandelbrot");
        strcpy(linha_cuda.modo, "cuda");
        linha_cuda.volume = volume; linha_cuda.threads = g_hw.gpu_cuda_cores;
        linha_cuda.t_segundos = t_med; linha_cuda.speedup = speedup;
        linha_cuda.valor = soma_cuda / n_rep; linha_cuda.t_kernel_s = tk_med;
        registrar_resultado_math(&linha_cuda);
    }
#else
    (void)cuda_ok;
#endif
}

/* ═══════════════════════════════════════════════════════
   MAIN — fluxo do experimento
═══════════════════════════════════════════════════════ */
int main(void) {
    /* Carrega hardware.cfg — obrigatorio; encerra com mensagem se ausente. */
    config_inicializar();

#ifdef _OPENMP
    /* Pinar threads nos nucleos fisicos ANTES de qualquer regiao paralela
       (via putenv, antes do runtime OpenMP inicializar). */
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
#endif

    imprimir_banner(cuda_ok);

    /* ─── LOOP DE BUSCA ─── */
    for (int vi = 0; vi < n_volumes_busca; vi++) {
        int volume = volumes_busca[vi];
        char vol_str[16];
        fmt_int(vol_str, volume);
        double mb = (double)volume * sizeof(Event) / (1024.0 * 1024.0);

        printf(">>> Gerando %s eventos (%.2f MB)...\n", vol_str, mb);
        fflush(stdout);

        Event *dados = gerar_eventos(volume);
        if (!dados) { printf("Falha ao gerar %d eventos\n", volume); continue; }

        Event *dados_ord = (Event *)malloc((size_t)volume * sizeof(Event));
        memcpy(dados_ord, dados, (size_t)volume * sizeof(Event));
        ordenar_por_valor(dados_ord, volume);

        HashTable *ht = hash_criar(dados, volume);

        int n_ids = (N_IDS_HASH < volume) ? N_IDS_HASH : volume;
        uint32_t *ids = (uint32_t *)malloc((size_t)n_ids * sizeof(uint32_t));
        for (int i = 0; i < n_ids; i++)
            ids[i] = (uint32_t)((i * (volume / n_ids)) + 1);

        printf("\n--- Volume: %s eventos (%zu bytes/evento  ->  %.2f MB) ---\n",
               vol_str, sizeof(Event), mb);
        tabela_busca_cabecalho();

        MelhorSpeedup linear = rodar_busca_linear(dados, volume, cuda_ok);
        tabela_busca_separador();
        MelhorSpeedup binaria = rodar_busca_binaria(dados_ord, volume, cuda_ok);
        tabela_busca_separador();
        MelhorSpeedup hash = rodar_busca_hash(ht, ids, n_ids, volume, cuda_ok);
        tabela_busca_rodape();

        imprimir_resumo_busca(linear, binaria, hash);

        hash_destruir(ht);
        free(ids);
        free(dados_ord);
        free(dados);
    }

    imprimir_legenda_busca();

    /* ─── LOOP DE SIMULACAO MATEMATICA ─── */
    imprimir_titulo_math();
    for (int vi = 0; vi < n_volumes_math; vi++) {
        int volume = volumes_math[vi];
        char vol_str[16];
        fmt_int(vol_str, volume);

        printf("\n--- Math Volume: %s ---\n", vol_str);
        tabela_math_cabecalho();

        rodar_montecarlo(volume, cuda_ok);
        tabela_math_separador();
        rodar_mandelbrot(volume, cuda_ok);
        tabela_math_rodape();
    }

    /* ─── Gerar arquivos de saida ─── */
    time_t now_busca = time(NULL);
    char ts_busca[32];
    strftime(ts_busca, sizeof(ts_busca), "%Y-%m-%dT%H:%M:%S", localtime(&now_busca));

    /* offset nominal de 1 minuto para o timestamp da matematica diferir */
    time_t now_math = time(NULL) + 60;
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
