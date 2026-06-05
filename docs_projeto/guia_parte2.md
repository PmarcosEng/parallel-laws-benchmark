# GUIA COMPLETO DO PROJETO — PARTE 2

---

## [T08] Como se Programa uma GPU com CUDA

### O modelo mental fundamental

Uma CPU tem **poucos núcleos poderosos** (4–32), cada um executando código complexo com branch prediction, out-of-order execution, grandes caches.

Uma GPU tem **milhares de núcleos simples** (896 na GTX 1650, até 16384 na RTX 4090), cada um executando código simples de forma **massivamente paralela**.

```
CPU: 8 núcleos × tarefa complexa   = 8 tarefas simultâneas
GPU: 896 cores × tarefa simples    = 896 tarefas simultâneas
```

> **Dicionário Rápido desta Seção:**
> - **Branch prediction**: A habilidade incrivelmente complexa da CPU de tentar "adivinhar" se um `if` vai ser verdadeiro ou falso antes mesmo de fazer a conta, ganhando tempo. (A GPU não tem essa inteligência).
> - **Out-of-order execution**: A CPU reordenando seu código por conta própria na hora de executar. Se uma linha de cima trava esperando a RAM, ela pula e já adianta a conta de baixo.
> - **Latência**: É o famoso "lag". O tempo entre o chip pedir um dado e esse dado realmente chegar. 1 "ciclo" é o tempo de um tique do relógio do processador (bilhões de vezes por segundo). A VRAM é enorme, mas a latência para acessá-la é bem alta (demorada).

### Hierarquia de execução CUDA

```
Grid (lançamento inteiro do kernel)
 └── Blocks (blocos de threads)
      └── Threads (execução individual)
```

No projeto:
```c
const int THREADS_POR_BLOCO = 256;
const int N_BLOCOS = (n + THREADS_POR_BLOCO - 1) / THREADS_POR_BLOCO;
kernel_busca_linear<<<N_BLOCOS, THREADS_POR_BLOCO>>>(d_dados, n, vmin, vmax, d_indices, d_count);
```

Para `n = 1.000.000` eventos:
- `N_BLOCOS = ceil(1.000.000 / 256) = 3.907 blocos`
- `3.907 × 256 = 999.992 threads` (as últimas fazem `if (i < n) return;`)
- Cada thread processa **1 elemento**

### O que `<<<N_BLOCOS, THREADS_POR_BLOCO>>>` significa

Essa é a **sintaxe de lançamento de kernel** — exclusiva do CUDA C. O compilador `nvcc` a transforma em chamadas à CUDA Runtime API. Os parâmetros entre `<<<...>>>` são:

1. **Grid dimension**: quantos blocos no grid
2. **Block dimension**: quantos threads por bloco
3. **(opcional) Shared memory em bytes**
4. **(opcional) Stream CUDA**

### Variáveis built-in dentro do kernel

```c
__global__ void kernel_busca_linear(const Event* dados, int n, ...) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    //       qual bloco    threads/bloco   pos no bloco
    //          ↑ 0..N_BLOCOS-1               ↑ 0..255
}
```

Para o bloco 5, thread 37: `i = 5 × 256 + 37 = 1.317`

### Qualificadores de função CUDA

| Qualificador | Onde executa | Quem chama |
|---|---|---|
| `__global__` | GPU | CPU (via `<<<>>>`) |
| `__device__` | GPU | Código GPU apenas |
| `__host__` | CPU | CPU apenas (padrão) |

### Memória CUDA — hierarquia de velocidade

| Tipo | Escopo | Tamanho | Latência |
|---|---|---|---|
| **Registers** | 1 thread | ~255 por thread | 1 ciclo |
| **Shared Memory** | 1 bloco | 48–96 KB/SM | ~5 ciclos |
| **L1/L2 Cache** | SM/chip | KB–MB | ~20–50 ciclos |
| **Global Memory (VRAM)** | todos | GB | ~200–800 ciclos |
| **Host Memory (RAM)** | CPU | GB | ~PCIe latência |

---

## [T09] O Pipeline CUDA do Projeto

Toda operação CUDA segue este fluxo:

> **Dicionário Rápido desta Seção:**
> - **H2D (Host to Device)**: Ação de enviar dados da Memória RAM normal do PC (Host) para a Memória de Vídeo da GPU (Device).
> - **D2H (Device to Host)**: O caminho contrário. Trazer o resultado da Placa de Vídeo de volta para a RAM do PC usar.
> - **PCIe Bus (Barramento PCIe)**: A conexão física da placa mãe (o slot comprido) onde a placa de vídeo é encaixada. Os dados de H2D e D2H precisam passar fisicamente por ali. Por isso H2D é muito demorado.

```
CPU (RAM)                PCIe Bus               GPU (VRAM)
    │                       │                       │
    │──── cudaMalloc ──────►│──────────────────────►│ aloca VRAM
    │                       │                       │
    │──── cudaMemcpy ───────┼──────────────────────►│ H2D: copia dados
    │    HostToDevice       │                       │
    │                       │                       │
    │──── kernel<<<>>> ─────┼──────────────────────►│ executa kernel
    │                       │                       │
    │◄─── cudaMemcpy ───────┼───────────────────────│ D2H: busca resultado
    │    DeviceToHost       │                       │
    │                       │                       │
    │──── cudaFree ─────────┼──────────────────────►│ libera VRAM
```

### Como o projeto mede cada fase

```c
cudaEvent_t ev0, ev1, ev2, ev3;
cudaEventCreate(&ev0);  // ... ev1, ev2, ev3

cudaEventRecord(ev0);   // marca início
cudaMemcpy(d_dados, dados, n * sizeof(Event), cudaMemcpyHostToDevice);
cudaEventRecord(ev1);   // após H2D

cudaEventRecord(ev2);   // antes do kernel
kernel_busca_linear<<<N_BLOCOS, THREADS_POR_BLOCO>>>(...);
cudaEventRecord(ev3);   // após kernel
cudaEventSynchronize(ev3);  // espera GPU terminar

float ms_h2d, ms_ker;
cudaEventElapsedTime(&ms_h2d, ev0, ev1);   // tempo H2D em ms
cudaEventElapsedTime(&ms_ker, ev2, ev3);   // tempo kernel em ms
```

`cudaEvent_t` é um **timer de hardware da GPU** — precisão de ~0.5 µs. Muito mais preciso que `clock()` da CPU para medir operações GPU.

### O que o JSON guarda: decomposição transferência × kernel

Para cada linha de modo `cuda`, o benchmark grava dois campos extras: `tempo_kernel_s` (só o kernel, medido por `cudaEvent`) e `tempo_transfer_s` (= tempo total − kernel, ou seja o custo de H2D pelo barramento PCIe). Isso permite **separar o tempo de transferência do tempo de processamento na GPU** — que é justamente a fração serial da Lei de Amdahl. No dashboard, esses dois campos viram o gráfico empilhado "transferência PCIe × kernel".

> **Busca × matemática:** na busca, o array de `Event` é copiado para a GPU, então a transferência é real e pesa. Na matemática (Monte Carlo / Mandelbrot) os dados são **gerados dentro da GPU** — não há cópia de array, só um escalar de 8 bytes volta. Por isso, na matemática, `tempo_transfer_s` é praticamente zero (só overhead de setup) e o kernel domina quase 100%.

---

## [T10] Kernel de Busca Linear — Análise Detalhada

```c
__global__ void kernel_busca_linear(
    const Event* __restrict__ dados,    // __restrict__: sem aliasing
    int           n,
    float         vmin, float vmax,
    int*  __restrict__ out_indices,
    int*  __restrict__ d_count)
{
    int i = (int)(blockIdx.x * blockDim.x + threadIdx.x);

    if (i < n && dados[i].valor >= vmin && dados[i].valor <= vmax) {
        int pos = atomicAdd(d_count, 1);  // ← chave de tudo
        out_indices[pos] = i;
    }
}
```

### `__restrict__`

Diz ao compilador que os ponteiros **não se sobrepõem na memória**. Permite otimizações de load/store. Se `dados` e `out_indices` pudessem apontar para o mesmo endereço, o compilador teria que ser conservador.

> **Dicionário Rápido desta Seção:**
> - **Aliasing (sobreposição)**: Ocorre quando duas variáveis (ou ponteiros) diferentes apontam e modificam exatamente a mesma parte da memória. Isso atrapalha a otimização da máquina, porque ela não pode ter certeza de que o valor lido não mudou silenciosamente pelo outro ponteiro.
> - **Serialização**: É a grande inimiga do paralelismo. Quando algo obriga milhares de threads a pararem de agir de forma independente e formarem uma "fila indiana" lenta. Muitos `atomicAdd` no mesmo endereço causam serialização.

### `atomicAdd` — sem race condition

Sem `atomicAdd`, 1000 threads tentando `(*d_count)++` ao mesmo tempo causariam race condition: todas leriam `0`, todas escreveriam `1`, resultado final seria `1` em vez de `1000`.

`atomicAdd(ptr, val)`:
1. Lê o valor atual de `*ptr` **atomicamente** (uma transação indivisível no hardware)
2. Adiciona `val`
3. Escreve o resultado
4. **Retorna o valor ANTES da adição** — isso é a posição exclusiva da thread

```c
int pos = atomicAdd(d_count, 1);
out_indices[pos] = i;  // cada thread escreve numa posição única
```

**Desvantagem**: muitas threads dando `atomicAdd` simultaneamente causam **serialização** na memória global. É por isso que o kernel de Monte Carlo (Seção T11) reduz os parciais em **shared memory** e faz **apenas 1 `atomicAdd` por bloco**, em vez de um por elemento.

---

## [T11] Redução em Shared Memory (kernel Monte Carlo)

Quando milhares de threads precisam **somar** seus resultados num único contador, fazer um `atomicAdd` por elemento serializa o acesso à memória global (lento). A solução é reduzir primeiro **dentro do bloco**, em shared memory, e só então fazer um `atomicAdd` por bloco. O kernel de Monte Carlo usa exatamente esse padrão: cada thread conta quantos pontos caíram dentro do círculo, escreve o parcial em `sdata[tid]`, e o bloco soma esses parciais em árvore.

```c
__global__ void kernel_montecarlo(long n, unsigned long long *d_inside) {
    extern __shared__ unsigned long long sdata[];
    int tid = threadIdx.x;
    // ... cada thread acumula 'local_inside' no seu grid-stride loop ...

    sdata[tid] = local_inside;        // fase 1: cada thread escreve seu parcial
    __syncthreads();                  // barreira: todos no bloco chegam antes de seguir

    // fase 2: redução em árvore — log2(blockDim) passos
    for (int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (tid < s) sdata[tid] += sdata[tid + s];
        __syncthreads();
    }

    // fase 3: thread 0 do bloco faz 1 atomicAdd com a soma do bloco
    if (tid == 0) atomicAdd(d_inside, sdata[0]);
}
```

### Visualização da redução em árvore (bloco de 8 threads)

```
Passo 0:  [1, 0, 1, 1, 0, 1, 0, 1]   s=4
                 ↓
Passo 1:  [1+0, 0+1, 1+1, 1+1, -, -, -, -]  = [1, 1, 2, 2]   s=2
                 ↓
Passo 2:  [1+1, 1+2, -, -, -, -, -, -]  = [2, 3]   s=1
                 ↓
Passo 3:  [2+3, -, -, -, -, -, -, -]  = [5]   ← atomicAdd(d_inside, 5)
```

Para 256 threads → 8 passos → **apenas 1 `atomicAdd` por bloco** (vs 1 por ponto contado). O Mandelbrot usa o mesmo padrão para somar o total de iterações.

### `__syncthreads()`

Barreira de sincronização dentro do bloco. **Todas as threads do bloco** devem atingir o `__syncthreads()` antes que qualquer uma passe. Obrigatório após escrever em shared memory que outra thread vai ler.

---

## [T12] Kernel de Busca Binária na GPU

```c
__global__ void kernel_busca_binaria(
    const Event* dados, int n, float alvo, int* d_count)
{
    int tid      = blockIdx.x * blockDim.x + threadIdx.x;
    int n_chunks = gridDim.x * blockDim.x;           // total de threads
    int chunk    = (n + n_chunks - 1) / n_chunks;    // tamanho do chunk

    int lo = tid * chunk;
    int hi = lo + chunk - 1;
    if (hi >= n) hi = n - 1;
    if (lo > hi) return;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (dados[mid].valor == alvo) {
            atomicAdd(d_count, 1);
            // ... verifica adjacentes
            return;
        } else if (dados[mid].valor < alvo) lo = mid + 1;
        else hi = mid - 1;
    }
}
```

**Por que a GPU perde aqui?** O kernel termina em **microssegundos** (O(log n) por chunk). Mas copiar o array inteiro via PCIe leva **milissegundos**. O overhead de H2D domina completamente. Ilustra a **Lei de Amdahl**: a fração sequencial (transferência) limita o ganho.

---

## [T13] Achatamento da Hash Table para GPU

A hash table da CPU usa **lista encadeada** (ponteiros):

```
buckets[0] → HashNode{key=5, prox→} → HashNode{key=131106, prox=NULL}
buckets[1] → NULL
buckets[2] → HashNode{key=2, prox=NULL}
```

**Ponteiros host não funcionam na GPU** — eles apontam para endereços RAM que a GPU não enxerga.

**Solução: achatar em arrays contíguos**

```
flat_keys   = [5, 131106, 2]       // todas as chaves, bucket por bucket
flat_starts = [0, 2, 2]           // flat_starts[b] = onde começa o bucket b
flat_lens   = [2, 0, 1]           // quantas entradas em cada bucket
```

```c
// passo 1: conta entradas por bucket
for (int b = 0; b < tamanho; b++) {
    int cnt = 0;
    for (HashNode *nd = ht->buckets[b]; nd; nd = nd->prox) cnt++;
    h_lens[b]   = cnt;
    h_starts[b] = total_entries;
    total_entries += cnt;
}
// passo 2: preenche flat_keys
for (int b = 0; b < tamanho; b++) {
    int pos = h_starts[b];
    for (HashNode *nd = ht->buckets[b]; nd; nd = nd->prox)
        h_keys[pos++] = nd->key;
}
```

Então no kernel:
```c
uint32_t id     = ids[tid];
int      bucket = id % tamanho;          // hash function
int      start  = flat_starts[bucket];
int      len    = flat_lens[bucket];
for (int i = 0; i < len; i++) {
    if (flat_keys[start + i] == id) { atomicAdd(d_count, 1); break; }
}
```

---

## [T14] Monte Carlo e Mandelbrot na GPU

### Monte Carlo Pi

```c
__global__ void kernel_montecarlo(long n, unsigned long long *d_inside) {
    extern __shared__ unsigned long long sdata[];
    int gid    = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = gridDim.x * blockDim.x;

    // gerador LCG por thread — sem dependências entre threads
    unsigned int state = (gid * 1664525u + 1013904223u) ^ 0xDEADBEEFu;

    unsigned long long local_inside = 0;
    // grid-stride loop: 1 thread processa múltiplos pontos
    for (long i = gid; i < n; i += stride) {
        float x = lcg32_float(&state);
        float y = lcg32_float(&state);
        if (x*x + y*y <= 1.0f) local_inside++;
    }

    sdata[threadIdx.x] = local_inside;
    __syncthreads();
    // redução em árvore + atomicAdd
    ...
}
```

**Grid-stride loop**: em vez de lançar `n` threads (pode ser bilhões), lança um grid fixo e cada thread processa múltiplos pontos com passo `stride`. Mais eficiente para `n` muito grande.

**LCG por thread**: Linear Congruential Generator — cada thread tem sua própria semente derivada do `gid`. Sem dependência → sem sincronização → máxima paralelização. (Qualidade estatística menor que Mersenne Twister, mas suficiente para Monte Carlo.)

### Mandelbrot

```c
__global__ void kernel_mandelbrot(int side, unsigned long long *d_total) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int py = idx / side;
    int px = idx % side;

    float c_re = -2.0f + (float)px * (3.0f / side);
    float c_im = -1.5f + (float)py * (3.0f / side);
    // iteração z = z² + c
    float z_re = 0, z_im = 0;
    int iter = 0;
    while (z_re*z_re + z_im*z_im <= 4.0f && iter < 256) {
        float tmp = z_re*z_re - z_im*z_im + c_re;
        z_im = 2*z_re*z_im + c_im;
        z_re = tmp;
        iter++;
    }
    atomicAdd(d_total, (unsigned long long)iter);
}
```

**Por que GPU brilha no Mandelbrot?** Cada pixel é **completamente independente** — zero comunicação entre threads. O número de iterações varia por pixel (divergência de warp), mas a GPU ainda é muito mais rápida que CPU para volumes grandes.

---

## [T15] `extern "C"` — Interoperabilidade C e CUDA

Arquivos `.cu` são compilados como **C++** pelo `nvcc`. O `benchmark.c` é compilado como **C puro**. C++ usa **name mangling** — `busca_linear_cuda` vira algo como `_Z18busca_linear_cudaP5Eventiiff` no binário.

`extern "C"` desabilita o mangling para aquela função, tornando-a chamável do C puro:

```c
// em search_cuda.cu
extern "C" SearchResult busca_linear_cuda(Event *dados, ...) { ... }

// em search_cuda.h
#ifdef __cplusplus
extern "C" {
#endif
SearchResult busca_linear_cuda(Event *dados, ...);
#ifdef __cplusplus
}
#endif
```

---

## [T16] Compilação para Diferentes Hardwares — Compute Capability

Cada geração de GPU NVIDIA tem uma **Compute Capability (CC)** que define as features disponíveis:

| CC | Arquitetura | GPUs Exemplo | `-arch` flag |
|---|---|---|---|
| 3.0–3.7 | Kepler | GTX 7xx | `sm_30`–`sm_37` |
| 5.0–5.3 | Maxwell | GTX 9xx | `sm_50`–`sm_53` |
| 6.0–6.2 | Pascal | GTX 10xx | `sm_60`–`sm_62` |
| 7.0 | Volta | Tesla V100 | `sm_70` |
| **7.5** | **Turing** | **GTX 1650, RTX 20xx/30xx** | **`sm_75`** |
| 8.0 | Ampere A100 | A100 | `sm_80` |
| **8.6** | **Ampere** | **RTX 3060/3080/3090** | **`sm_86`** |
| 8.9 | Ada Lovelace | RTX 4060/4090 | `sm_89` |
| 9.0 | Hopper | H100 | `sm_90` |

### Makefile usa `sm_75` (Turing)

```makefile
ARCH = -arch=sm_75
```

Gera código para GTX 1650 (sua GPU). **Binário NÃO roda em GPUs anteriores** (sm_30..sm_70). Roda em GPUs iguais ou posteriores (sm_75+).

### build_cuda.bat usa `sm_86` (Ampere)

```bat
nvcc.exe -O2 -arch=sm_86 ...
```

Gera para RTX 3xxx. **Não roda na GTX 1650 (sm_75)**!

### Como gerar para múltiplas arquiteturas (CMake)

```cmake
set_target_properties(benchmark_cuda PROPERTIES
    CUDA_ARCHITECTURES "75;86;89"
)
```

Isso gera **PTX + cubin** para cada arquitetura. O executável é maior mas roda em qualquer GPU das gerações Turing, Ampere e Ada.

### PTX vs SASS — dois níveis de compilação

```
Código .cu
    ↓ nvcc (compilação)
PTX (Parallel Thread Execution) — assembly virtual, portável entre GPUs
    ↓ ptxas (durante compilação OU em runtime pelo driver)
SASS (Shader Assembly) — binário específico da arquitetura
```

Quando você compila com `-arch=sm_75`, o nvcc gera PTX para CC 7.5. O driver da GPU converte para SASS na primeira execução (JIT — Just In Time). Se você tem uma RTX 3080 (sm_86) e o binário tem apenas PTX para sm_75, o driver consegue executar via **forward compatibility** (mas não ao contrário).

> **Dicionário Rápido desta Seção:**
> - **JIT (Just In Time - Apenas a Tempo)**: Compilar na hora H. Em vez de entregar o arquivo `.exe` finalizado especificamente para uma placa mãe X, você entrega um arquivo pré-traduzido genérico (PTX). Quando o usuário roda o programa, o Driver da Placa de Vídeo termina a compilação nos bastidores moldando perfeitamente para aquela placa.

### Estratégia de distribuição robusta

```
nvcc -gencode arch=compute_75,code=sm_75  \
     -gencode arch=compute_86,code=sm_86  \
     -gencode arch=compute_75,code=compute_75  # PTX fallback
```

O último `-gencode` embute o PTX no binário — qualquer GPU futura consegue fazer JIT a partir dele.

---

## [T17] `hardware.cfg` — Como o Benchmark se Adapta

O arquivo é lido obrigatoriamente no início de `main()`. Se não existir, o programa termina com instruções:

```c
static int config_carregar(HardwareConfig *hw) {
    FILE *f = fopen("hardware.cfg", "r");
    if (!f) return 0;
    // lê linha a linha, parseia "chave=valor"
}
```

Os volumes de busca determinam o uso de RAM:

```
hardware.cfg diz:  volumes_busca=1000,100000,1000000,10000000
benchmark.c faz:   for (vi = 0; vi < N_VOLUMES; vi++) {
                       int n = VOLUMES[vi];           // 1000, 100000, ...
                       Event *dados = gerar_eventos(n); // n × 96 bytes
```

Para `n = 10.000.000`: `10M × 96 bytes = 915 MB` só para o array principal. Com cópia ordenada para busca binária = **1,8 GB** total.

---

## [T18] Medição de Tempo — Detalhes de Plataforma

### No Windows (MSVC)

```c
static double agora(void) {
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);  // ticks por segundo
    QueryPerformanceCounter(&cnt);     // ticks atuais
    return (double)cnt.QuadPart / (double)freq.QuadPart;
}
```

`QueryPerformanceCounter` usa o **TSC (Time Stamp Counter)** do hardware — precisão de ~100 ns no Windows moderno.

### No Linux/GCC

```c
static double agora(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
```

`CLOCK_MONOTONIC`: relógio que nunca volta atrás (ao contrário de `CLOCK_REALTIME` que pode ser ajustado pelo NTP). Precisão de ~1 ns.

### Mediana de N repetições

```c
static double medianaK(double *v, int n) {
    // insertion sort
    for (int i = 1; i < n; i++) { ... }
    return v[n / 2];   // elemento central
}
```

**Por que mediana e não média?** Eventos externos (interrupção do SO, cache miss inicial, atividade de background) criam **outliers positivos** nas medições. A mediana é robusta a outliers. Com 3 repetições, descarta o maior e o menor implicitamente.

> **Dicionário Rápido desta Seção:**
> - **TSC (Time Stamp Counter)**: Um cronômetro minúsculo cravado diretamente dentro do hardware do processador. Ele "bate" a cada ciclo (frequência, ex: 4GHz). É a forma de hardware mais precisa para medir pedaços milimétricos de tempo.
> - **Outliers (Ponto Fora da Curva)**: Um valor bizarro que foge da normalidade. Exemplo: se um teste leva `2s`, `2.1s`, e num deles o antivírus trava o PC do nada levando `15s`, o `15s` é o outlier. A média de `(2+2.1+15)/3` seria `6.3s` (resultado mentiroso). A mediana organiza (`2`, `2.1`, `15`) e pega o do meio (`2.1`), que é a verdade, descartando o evento bizarro.

---

## [T19] O `#ifdef HAS_CUDA` — Compilação Condicional

O mesmo `benchmark.c` funciona **com ou sem CUDA**. O macro `HAS_CUDA` é definido apenas quando compilado com `-DHAS_CUDA`:

```c
#ifdef HAS_CUDA
#  include "search_cuda.h"
#  include "sim_math_cuda.h"
#endif

// mais abaixo em main():
#ifdef HAS_CUDA
    int cuda_ok = cuda_init();
#else
    int cuda_ok = 0;
#endif

// nos loops de benchmark:
#ifdef HAS_CUDA
if (cuda_ok) {
    // roda kernel CUDA real
}
#endif
```

**Sem `-DHAS_CUDA`**: o compilador ignora tudo entre `#ifdef HAS_CUDA` e `#endif`. O executável resultante não linka com a CUDA Runtime Library — funciona em qualquer PC.

**Com `-DHAS_CUDA`**: o `nvcc` inclui os kernels, o executável linka `cudart.lib` e precisa de driver NVIDIA ≥ 525 (para CUDA 12).

---

## [T20] Fluxo Completo de uma Execução

```
1. main() → config_inicializar() → lê hardware.cfg
   ↓
2. cuda_init() → cudaSetDevice(0) → warmup (malloc+free de 256 bytes)
   ↓
3. for (vi=0; vi<N_VOLUMES; vi++):
   ↓
4.   gerar_eventos(n) → malloc(n × 96) → preenche com dados sintéticos
   ↓
5.   ordenar_por_valor() → qsort por ponteiros → reordena array original
   ↓
6.   hash_criar() → aloca tabela com 131.101 buckets
   ↓
7.   Benchmark BUSCA LINEAR:
     • warmup (1 run descartado)
     • serial: 3 runs → mediana → t_serial_linear
     • openmp 2t, 4t, 8t...: speedup = t_serial / t_par
     • cuda (se HAS_CUDA e cuda_ok): H2D + kernel + D2H
   ↓
8.   Benchmark BUSCA BINÁRIA (mesmo padrão)
   ↓
9.   Benchmark HASH LOOKUP (mesmo padrão)
   ↓
10.  free(dados), hash_destruir(), free(dados_ord)
   ↓
11. for (vi=0; vi<N_MATH_VOLUMES; vi++):
    • Monte Carlo Pi
    • Mandelbrot 2D
   ↓
12. salvar_run_busca() → JSON em runs/busca_cuda/TIMESTAMP.json
    salvar_run_matematica() → JSON em runs/matematica_cuda/TIMESTAMP.json
```

---

## [T21] Por que a GPU Às Vezes Perde para a CPU?

O benchmark foi projetado para **mostrar isso**. Resumo:

| Algoritmo | CPU serial | GPU CUDA | Resultado |
|---|---|---|---|
| Busca Linear (grande) | lento | rápido | **GPU vence** |
| Busca Binária | muito rápido (O log n) | lento (H2D domina) | **CPU vence** |
| Hash Lookup | O(1) | setup+H2D caro | **CPU vence** |
| Monte Carlo | lento | muito rápido | **GPU vence** |
| Mandelbrot | lento | muito rápido | **GPU vence** |

**Regra geral**: GPU só compensa quando o volume de **compute** (operações matemáticas) supera o custo de **transferência** (PCIe). Para algoritmos que fazem pouco por byte transferido (como busca binária: O(log n) operações para n bytes copiados), a CPU ganha.

Isso é a **Lei de Amdahl** na prática: a fração serial (H2D transfer) não é paralelizável e limita o speedup máximo possível.

---

## [T22] Ferramentas do Ecossistema CUDA

| Ferramenta | Uso |
|---|---|
| `nvcc` | Compilador CUDA |
| `cuda-gdb` | Debugger para kernels |
| `nvprof` (legado) / `nsys` | Profiler: mede tempo de cada kernel/transferência |
| `ncu` (Nsight Compute) | Profiler de baixo nível: occupancy, cache hits, bandwidth |
| `nvidia-smi` | Monitor de GPU (temperatura, uso, VRAM) |
| `cuDNN` | Biblioteca de deep learning (convoluções, etc.) |
| `cuBLAS` | BLAS linear algebra na GPU |
| `Thrust` | STL para CUDA (sort, reduce, scan em GPU) |
| `CUDA-MEMCHECK` | Detecta acessos inválidos à memória da GPU |

Para depurar e otimizar seus kernels, o fluxo seria:
```bash
nsys profile ./benchmark_cuda          # vê timeline geral
ncu --set full ./benchmark_cuda        # analisa um kernel em detalhe
```
