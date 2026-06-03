#ifndef GENERATOR_H
#define GENERATOR_H
#include "event.h"
Event *gerar_eventos(int n);
void   ordenar_por_valor(Event *dados, int n);
void   inspecionar_eventos(const Event *eventos, int n, int max_print);
#endif
