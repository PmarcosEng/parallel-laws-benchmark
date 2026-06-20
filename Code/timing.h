#ifndef TIMING_H
#define TIMING_H

/* ═══════════════════════════════════════════════════════
   TIMING — medição de tempo e estatística de repetições.

   Módulo independente: só sabe ler o relógio, tirar mediana e
   cronometrar uma chamada repetida. Não conhece busca, matemática
   nem configuração — é a "régua" usada por todos os benchmarks.
═══════════════════════════════════════════════════════ */

/* Máximo de repetições cronometradas que cabem nos buffers de medição.
   repeticoes_validas() garante que nunca passamos disso. */
#define MAX_REPETICOES 9

/* Relógio monotônico em segundos, com precisão de nanosegundos onde houver.
   Windows -> QueryPerformanceCounter ;  Linux -> clock_gettime(CLOCK_MONOTONIC). */
double agora(void);

/* Ordena amostras[0..n_amostras-1] in-place e devolve o valor central. */
double mediana(double *amostras, int n_amostras);

/* Clampa o nº de repetições desejado para o intervalo [1, MAX_REPETICOES]. */
int repeticoes_validas(int desejado);

/* ───────────────────────────────────────────────────────
   CRONOMETRAR_MEDIANA — coração da medição.

   Executa `chamada` por `n_rep` vezes, mede a duração de cada uma com
   agora() e grava a mediana (em segundos) em `out_tempo`. Substitui os
   loops de cronometragem que antes estavam copiados em cada algoritmo.

     out_tempo : lvalue double que recebe a mediana das durações.
     n_rep     : nº de repetições cronometradas (<= MAX_REPETICOES).
     entre_rep : statement no início de CADA repetição — use (void)0 quando
                 não precisar; serve p/ liberar o resultado da rep anterior
                 (ex.: search_result_free(&r), seguro mesmo na 1ª volta se r
                 começar zerado).
     chamada   : statement a cronometrar (ex.: r = busca_linear_serial(...)).

   O warmup (1 execução fora da conta, para aquecer cache/JIT) é
   responsabilidade do chamador, feito antes de invocar a macro.
─────────────────────────────────────────────────────── */
#define CRONOMETRAR_MEDIANA(out_tempo, n_rep, entre_rep, chamada)        \
    do {                                                                 \
        double _amostras[MAX_REPETICOES];                                \
        double _t_prev = agora();                                        \
        for (int _rep = 0; _rep < (n_rep); _rep++) {                     \
            entre_rep;                                                   \
            chamada;                                                     \
            double _t_now = agora();                                     \
            _amostras[_rep] = _t_now - _t_prev;                          \
            _t_prev = _t_now;                                            \
        }                                                                \
        (out_tempo) = mediana(_amostras, (n_rep));                       \
    } while (0)

#endif /* TIMING_H */
