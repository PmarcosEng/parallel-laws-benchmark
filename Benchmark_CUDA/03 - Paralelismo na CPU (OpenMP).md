---
tags: []
status: ativo
data_criacao: 2026-04-26
data_atualizacao: 2026-04-26
gerado_por: Claude
---
# Paralelismo na CPU (OpenMP) — Engenharia de Hardware

Nosso projeto não usa o OpenMP de forma ingênua. Aplicamos técnicas profundas para evitar a destruição do *Cache Coherency* e maximizar o uso de todos os núcleos do processador.

> [!info] Dicionário Rápido
> - **Thread**: Um trabalhador virtual. Um processador de 8 núcleos pode ter 16 threads simultâneas (com HyperThreading).
> - **Race Condition**: Dois trabalhadores tentando escrever no mesmo endereço de memória ao mesmo tempo — resultado: dado corrompido.
> - **Lock**: Solução ingênua para Race Condition — forçar fila de espera. Destrói o paralelismo.
> - **Cache Coherency**: Quando um núcleo modifica dados, todos os outros núcleos são notificados que o dado na cache deles está desatualizado. Esse "aviso pelo rádio" atrasa a CPU inteira.

---

## Por Que Contar Juntos É Perigoso

O problema mais comum no paralelismo: múltiplas threads somando ao mesmo contador global.

```mermaid
flowchart LR
    subgraph "Thread 0"
        T0R["Lê contador = 5"]
        T0W["Escreve contador = 6"]
    end
    subgraph "Thread 1"
        T1R["Lê contador = 5"]
        T1W["Escreve contador = 6"]
    end
    GLOBAL["contador = 5"] --> T0R
    GLOBAL --> T1R
    T0W --> RESULT["contador = 6 ❌\nDeveria ser 7!"]
    T1W --> RESULT
```

Se Thread 0 e Thread 1 leem o mesmo valor `5` antes de qualquer uma escrever, ambas calculam `5+1=6` e escrevem `6`. Uma soma se perde. Isso é a **Race Condition**.

---

## Solução 1: `reduction` do OpenMP (Para Resultados Numéricos)

A diretiva `reduction` cria cópias **privadas** do contador para cada thread e soma tudo no final — sem fila, sem Lock.

```mermaid
flowchart TB
    INPUT["Array de 1.000.000 eventos"] --> SPLIT

    subgraph SPLIT["Divisão automática pelo OpenMP"]
        direction LR
        T0["Thread 0\nEventos 0–249.999"]
        T1["Thread 1\nEventos 250.000–499.999"]
        T2["Thread 2\nEventos 500.000–749.999"]
        T3["Thread 3\nEventos 750.000–999.999"]
    end

    T0 --> C0["local_inside₀ = 12.341"]
    T1 --> C1["local_inside₁ = 12.189"]
    T2 --> C2["local_inside₂ = 12.405"]
    T3 --> C3["local_inside₃ = 12.298"]

    subgraph TREE["Árvore de Redução (O 'Campeonato')"]
        direction TB
        R01["12.341 + 12.189\n= 24.530"]
        R23["12.405 + 12.298\n= 24.703"]
        FINAL["24.530 + 24.703\n= 49.233 ✅"]
    end

    C0 --> R01
    C1 --> R01
    C2 --> R23
    C3 --> R23
    R01 --> FINAL
    R23 --> FINAL
```

```c
long inside = 0; // Resultado final

#pragma omp parallel num_threads(threads) reduction(+:inside)
{
    // Cada thread tem sua cópia privada de 'inside'
    // (o compilador faz isso automaticamente)
    for (long i = omp_get_thread_num(); i < n; i += threads) {
        if (condicao_satisfeita(dados[i])) {
            inside++; // Seguro! Cada thread incrementa SUA cópia
        }
    }
    // No final: o OpenMP soma todas as cópias na variável global 'inside'
}
// Aqui 'inside' tem o total correto, sem Race Condition
```

> [!success] Por que isso evita False Sharing?
> **False Sharing** acontece quando threads diferentes modificam variáveis que ficam na **mesma Cache Line** (bloco de 64 bytes). Mesmo sem conflito lógico, o hardware força sincronização toda vez.
> A `reduction` mantém as variáveis locais nos **registradores** de cada core — sem Cache Line compartilhada, sem sincronização desnecessária.

---

## Solução 2: Arrays 2D por Thread (Para Retornar Structs)

A `reduction` só funciona com tipos primitivos (somar, subtrair). Quando precisamos retornar os **eventos encontrados** (ponteiros para structs), gerenciamos a memória manualmente com um array 2D:

```mermaid
flowchart LR
    subgraph "Buffer 2D — uma gaveta por thread"
        direction TB
        G0["Gaveta Thread 0\ntemp[0][0..n₀]"]
        G1["Gaveta Thread 1\ntemp[1][0..n₁]"]
        G2["Gaveta Thread 2\ntemp[2][0..n₂]"]
        G3["Gaveta Thread 3\ntemp[3][0..n₃]"]
    end

    T0["Thread 0"] -->|só escreve aqui| G0
    T1["Thread 1"] -->|só escreve aqui| G1
    T2["Thread 2"] -->|só escreve aqui| G2
    T3["Thread 3"] -->|só escreve aqui| G3

    G0 --> MERGE["Merge final\n(serial, thread 0 apenas)"]
    G1 --> MERGE
    G2 --> MERGE
    G3 --> MERGE
    MERGE --> RES["SearchResult\nresultado final"]
```

```c
// Aloca uma gaveta de resultado para cada thread
Event ***temp   = malloc(threads * sizeof(Event **));
int    *counts  = calloc(threads, sizeof(int));

for (int t = 0; t < threads; t++)
    temp[t] = malloc(n * sizeof(Event *)); // Pior caso: todos os eventos

#pragma omp parallel for num_threads(threads)
for (int i = 0; i < n; i++) {
    int tid = omp_get_thread_num(); // "Qual é o meu crachá?"

    if (evento_satisfaz_condicao(&dados[i])) {
        // Thread 'tid' só escreve na PRÓPRIA gaveta — zero conflito!
        temp[tid][ counts[tid]++ ] = &dados[i];
    }
}

// Merge serial (barato — só consolida ponteiros)
for (int t = 0; t < threads; t++)
    for (int j = 0; j < counts[t]; j++)
        resultado[total++] = temp[t][j];
```

---

## Escalonamento de Tarefas (Scheduling)

> [!info] Analogias
> - **Estático**: Buffet a quilo — você já sabe quanto vai comer antes de sentar. Simples e eficiente para trabalho uniforme.
> - **Dinâmico**: Rodízio — você pega o que tem no prato, come, e pede mais quando acabar. Essencial quando pedaços têm tamanhos diferentes.

```mermaid
flowchart LR
    subgraph STATIC["Estático — schedule(static)"]
        direction TB
        SA["Thread 0: pixels 0–249\n⬜⬜⬜⬜⬜⬜ (rápido)"]
        SB["Thread 1: pixels 250–499\n⬛⬛⬛⬛⬛⬛ (lento!)"]
        SC["Thread 2: pixels 500–749\n⬜⬜⬜⬜⬜⬜ (rápido)"]
        SD["Thread 3: pixels 750–999\n⬜⬜⬜⬜⬜⬜ (rápido)"]
        WAIT["Thread 2 e 3 ociosas\naguardando Thread 1 ⏳"]
    end

    subgraph DYNAMIC["Dinâmico — schedule(dynamic,4)"]
        direction TB
        DA["Thread 0: pega 4, faz, volta, pega 4..."]
        DB["Thread 1: pega 4 difíceis, demora..."]
        DC["Thread 2: pega 4, faz, volta, pega 4, pega 4..."]
        DD["Sem ociosidade! CPU 100% o tempo todo ✅"]
    end
```

Para buscas (trabalho uniforme por item), o padrão estático é ideal. Para o **Mandelbrot**, onde o centro do fractal exige 256× mais iterações que as bordas, usamos `schedule(dynamic, 4)`:

```c
#pragma omp parallel for num_threads(threads) \
    reduction(+:total) \
    schedule(dynamic, 4)  // ← cada thread pega 4 linhas por vez, dinamicamente
for (int py = 0; py < lado; py++) {
    for (int px = 0; px < lado; px++) {
        // Conta as iterações do fractal para este pixel
        // (pode ser 1 iteração nas bordas ou 256 no centro)
        total += mandelbrot_iter(px, py, lado);
    }
}
```

> [!success] Resultado com `schedule(dynamic, 4)`
> Todos os núcleos ficam ocupados 100% do tempo, balanceando automaticamente os pixels "difíceis" (centro escuro do fractal) e os "fáceis" (bordas brilhantes). Speedup quase linear com o número de núcleos!

---
⬅️ Voltar para: [[Workbench/Faculdade/Arquitetura_2°_Artigo/parallel-laws-benchmark/Benchmark_CUDA/00 - Indice do Projeto]]
➡️ Próximo: [[04 - Sistema de Build]]
