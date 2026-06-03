---
tags: []
status: ativo
data_criacao: 2026-04-26
data_atualizacao: 2026-04-26
gerado_por: Claude
---
# Fundamentos de GPU — Arquitetura NVIDIA

Para programar na GPU, você precisa esquecer como a CPU funciona. A CPU é como um **Professor Universitário**: faz contas complexíssimas rapidamente, mas trabalha sozinho. A GPU é como um **Exército de 10.000 Formigas**: cada formiga é simples, mas trabalhando juntas elas movem montanhas num instante.

---

## CPU vs GPU — A Filosofia do Design

```mermaid
flowchart LR
    subgraph CPU["CPU — Latência baixa"]
        direction TB
        C1["Núcleo 1\nRápido, complexo\n4–16 núcleos"] 
        C2["Núcleo 2"]
        C3["…"]
        CACHE_L3["Cache L3\nGrande — reduz latência"]
        CTRL["Controle avançado\nBranch prediction\nOut-of-order execution"]
    end

    subgraph GPU["GPU — Throughput alto"]
        direction TB
        SM1["SM 1\n128 CUDA cores"]
        SM2["SM 2"]
        SM3["… (dezenas de SMs)"]
        SIMT["SIMT — executa a\nmesma instrução em\n32 threads (Warp)"]
        GMEM["VRAM — alta bandwidth\n~500 GB/s"]
    end

    CPU -->|"melhor para: lógica complexa\ndados irregulares, poucos itens"| TASK1["📋 Lógica de controle\nRaciocínio sequencial"]
    GPU -->|"melhor para: mesma conta\nem milhões de itens"| TASK2["🔢 Massa de dados\nCálculo paralelo"]
```

---

## A Logística do Exército (Grid → Block → Thread)

Quando a CPU ordena que a GPU execute uma missão (Kernel), ela organiza o exército em hierarquia:

```mermaid
flowchart TB
    GRID["Grid (o Exército inteiro)\nN_BLOCOS esquadrões"] --> B1
    GRID --> B2
    GRID --> BN

    subgraph B1["Block 0 (Esquadrão)"]
        T0["Thread 0"] & T1["Thread 1"] & T2["Thread 2"] & T3["… Thread 255"]
    end

    subgraph B2["Block 1 (Esquadrão)"]
        T256["Thread 0"] & T257["Thread 1"] & T258["… Thread 255"]
    end

    BN["Block N-1…"]
```

> [!info] Por que 256 threads por bloco?
> - Múltiplo de **32** (tamanho do Warp) — nunca desperdiça capacidade do SM.
> - **256** é o padrão da indústria: memória compartilhada suficiente, ocupância alta.
> - Valores maiores (512, 1024) podem reduzir a ocupância se o kernel usar muitos registradores.

### A Fórmula Mágica do `N_BLOCOS`

```c
const int THREADS_POR_BLOCO = 256;

// Divisão com arredondamento para CIMA (Teto / Ceiling Division)
// Garante que todos os elementos sejam cobertos
const int N_BLOCOS = (n + THREADS_POR_BLOCO - 1) / THREADS_POR_BLOCO;
```

Exemplo: $n = 1000$ eventos

$$N_{blocos} = \left\lceil \frac{1000}{256} \right\rceil = \left\lfloor \frac{1000 + 255}{256} \right\rfloor = \left\lfloor \frac{1255}{256} \right\rfloor = 4 \text{ blocos}$$

$4 \times 256 = 1024$ threads acionadas, mas só 1000 têm trabalho. As 24 extras verificam `if (id >= n) return;` e saem imediatamente — sem custo.

```c
__global__ void kernel_exemplo(const Event* dados, int n, ...) {
    int id = blockIdx.x * blockDim.x + threadIdx.x; // "Qual meu número de crachá global?"
    if (id >= n) return; // As 24 formigas extras vão pra casa

    // Cada formiga processa UM item — o dela
    processar(dados[id]);
}
```

---

## O Caminho dos Dados e o Gargalo PCIe

```mermaid
flowchart LR
    subgraph HOST["💻 Computador (Host)"]
        RAM["RAM\n~30 GB/s"]
        CPU["CPU"]
    end

    subgraph PCIE["🚌 Barramento PCIe (A Rodovia)"]
        H2D["➡️ H2D\n~15 GB/s"]
        D2H["⬅️ D2H\n~15 GB/s"]
    end

    subgraph DEVICE["🎮 Placa de Vídeo (Device)"]
        VRAM["VRAM (Global Memory)\n~500 GB/s"]
        SM["Streaming Multiprocessors\n(os núcleos)"]
    end

    CPU -->|cudaMalloc + cudaMemcpy| H2D
    H2D --> VRAM
    VRAM --> SM
    SM -->|resultado| VRAM
    VRAM --> D2H
    D2H -->|cudaMemcpy D→H| RAM
```

> [!warning] O Calcanhar de Aquiles da GPU
> A VRAM processa dados a **~500 GB/s** internamente. Mas o barramento PCIe que liga o computador à GPU transfere apenas **~15 GB/s** — 33× mais lento.
>
> **Consequência (Lei de Amdahl)**: Se a transferência demora 2 segundos e o kernel GPU demora 0.001 segundo, o speedup total é de apenas $2.001 / 2.000 ≈ 1.0005$. A GPU mal ajudou!
>
> **Solução (Lei de Gustafson)**: Problemas **gigantes** de cálculo puro (Monte Carlo com 10M amostras, Mandelbrot) amortizam o custo de transferência e a GPU vence com folga.

---

## Hierarquia de Memória da GPU

```mermaid
flowchart TB
    subgraph SPEED["↑ Mais rápido / ↓ Mais lento"]
        direction TB
        REG["🔑 Registradores\n~0 latência — bolso da formiga\n(variáveis locais: int x = 5)"]
        SHM["⚡ Shared Memory (L1)\n~5 clocks — mesa redonda do esquadrão\n(compartilhada dentro do Block)\n~48 KB por SM"]
        L2["📦 L2 Cache\n~50 clocks — corredor do galpão\n(automático, gerenciado pelo hardware)"]
        GLOB["🏭 Global Memory (VRAM)\n~200–800 clocks — pátio externo\nonde o caminhão H2D descarrega\n(GB, lenta, mas gigante)"]
        CONST["📌 Constant Memory\n~5 clocks com cache — avisos no quadro negro\n(read-only, broadcast para todos os threads)"]
    end

    REG --> SHM --> L2 --> GLOB
    CONST -.->|"leitura broadcast"| REG
```

| Memória | Latência | Tamanho | Quem acessa | Como usar |
|---------|----------|---------|-------------|-----------|
| Registradores | ~0 clocks | ~256 KB/SM | 1 thread | Variáveis locais automáticas |
| Shared Memory | ~5 clocks | 48–96 KB/SM | Todas as threads do bloco | `__shared__ int arr[256]` |
| L2 Cache | ~50 clocks | 4–40 MB | Todas as threads | Automático (hardware) |
| Global Memory (VRAM) | ~800 clocks | GB | Todas as threads | `cudaMalloc` + `cudaMemcpy` |
| Constant Memory | ~5 clocks* | 64 KB | Todas as threads (read-only) | `__constant__ float val` |

> [!tip] Por que a Shared Memory é tão importante?
> A Shared Memory é a chave da **Redução Paralela** (ver [[06 - Algoritmos de Busca na GPU]]). Ao invés de 256 threads fazerem `atomicAdd` na VRAM lenta (800 clocks × 256 = 204.800 clocks!), elas colaboram na Shared Memory rápida e só uma delas faz o `atomicAdd` final. Speedup de ~256× nessa operação!

---

## O Warp: A Menor Unidade Real de Execução

```mermaid
flowchart LR
    subgraph BLOCK["Block (256 threads)"]
        W1["Warp 0\nThreads 0–31\n(executam JUNTAS)"]
        W2["Warp 1\nThreads 32–63"]
        W3["Warp 2\nThreads 64–95"]
        WN["… Warp 7\nThreads 224–255"]
    end
    W1 -->|"SIMT: mesma instrução\nem 32 threads simultâneas"| SM["SM (Streaming Multiprocessor)"]
```

> [!warning] Divergência de Warp (O Inimigo do CUDA)
> ```c
> // PROBLEMÁTICO: metade do Warp vai por um caminho, metade por outro
> if (threadIdx.x % 2 == 0) {
>     faz_coisa_rapida();  // threads 0,2,4,6… executam isso
> } else {
>     faz_coisa_lenta();   // threads 1,3,5,7… executam isso
> }
> ```
> Quando threads do mesmo Warp tomam caminhos diferentes (`if/else`), a GPU **serializa** — executa um caminho de cada vez, desativando as threads que não precisam. O Warp de 32 threads efetivamente vira 2 Warps de 16, perdendo 50% da capacidade!
>
> **Solução**: Organizar os dados para que threads do mesmo Warp sempre tomem o mesmo caminho (Warp Coherency).

---
⬅️ Voltar para: [[Workbench/Faculdade/Arquitetura_2°_Artigo/parallel-laws-benchmark/Benchmark_CUDA/00 - Indice do Projeto]]
➡️ Próximo: [[06 - Algoritmos de Busca na GPU]]
