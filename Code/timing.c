/* No GCC/Linux precisamos destravar clock_gettime (extensão POSIX).
   _GNU_SOURCE é o superconjunto que cobre CLOCK_MONOTONIC.
   MSVC ignora a macro e usa QueryPerformanceCounter. */
#ifndef _MSC_VER
#  define _GNU_SOURCE
#endif

#include "timing.h"

#ifdef _MSC_VER
#  include <windows.h>
double agora(void) {
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart / (double)freq.QuadPart;
}
#else
#  include <time.h>
double agora(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
#endif

/* Insertion sort + valor central. n_amostras é sempre pequeno (<= MAX_REPETICOES),
   então O(n²) aqui é irrelevante e evita dependência de qsort/comparadores. */
double mediana(double *amostras, int n_amostras) {
    for (int i = 1; i < n_amostras; i++) {
        double chave = amostras[i];
        int j = i - 1;
        while (j >= 0 && amostras[j] > chave) {
            amostras[j + 1] = amostras[j];
            j--;
        }
        amostras[j + 1] = chave;
    }
    return amostras[n_amostras / 2];
}

int repeticoes_validas(int desejado) {
    if (desejado < 1)              return 1;
    if (desejado > MAX_REPETICOES) return MAX_REPETICOES;
    return desejado;
}
