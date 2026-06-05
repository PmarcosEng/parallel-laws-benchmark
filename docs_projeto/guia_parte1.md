# GUIA COMPLETO DO PROJETO — PARTE 1

---

## [T01] Visão Geral do Projeto

Este projeto é um **benchmark científico** que compara o desempenho de algoritmos de busca e matemática executados em três modos:

| Modo | Tecnologia | Onde roda |
|---|---|---|
| `serial` | C puro, 1 thread | CPU |
| `openmp` | OpenMP, N threads | CPU (paralelo) |
| `cuda` | CUDA real | GPU NVIDIA |

O objetivo acadêmico é demonstrar empiricamente as **Leis de Amdahl e Gustafson** — ou seja, mostrar na prática quando e por que paralelismo ajuda ou não.

---

## [T02] Mapa de Arquivos

```
simulacao/
├── event.h           → Struct Event (96 bytes) + BenchmarkResult
├── generator.h/c     → Gera eventos sintéticos realistas
├── search.h/c        → Algoritmos de busca CPU (serial + OpenMP)
├── search_cuda.h/.cu → Kernels CUDA de busca na GPU real
├── sim_math.h/c      → Monte Carlo Pi + Mandelbrot (CPU)
├── sim_math_cuda.h/.cu → Monte Carlo + Mandelbrot (CUDA real)
├── benchmark.c       → main() — orquestra tudo, salva JSON
├── hardware.cfg      → Configuração do hardware (você edita)
├── Makefile          → Build no Linux/WSL com GCC
└── build_cuda.bat    → Build no Windows com MSVC + nvcc
```

---

## [T03] A Struct Event — Por que 96 bytes?

```c
typedef struct {
    uint32_t   id;               //  4 bytes
    int64_t    timestamp;        //  8 bytes  ← alinhado em offset 8 (padding implícito de 4 após id)
    float      valor;            //  4 bytes
    float      valor_secundario; //  4 bytes
    Categoria  categoria;        //  4 bytes  (enum = int)
    uint8_t    status;           //  1 byte
    // 3 bytes de padding implícito aqui (alinhamento)
    char       tag[32];          // 32 bytes
    char       origem[16];       // 16 bytes
    uint32_t   checksum;         //  4 bytes
    uint8_t    _pad[8];          //  8 bytes  ← padding EXPLÍCITO
} Event;                         // TOTAL: 96 bytes
```

### Conta detalhada byte a byte

| Campo | Tamanho | Offset | Razão |
|---|---|---|---|
| `id` (uint32_t) | 4 | 0 | — |
| *(padding implícito)* | 4 | 4 | `int64_t` precisa estar em múltiplo de 8 |
| `timestamp` (int64_t) | 8 | 8 | — |
| `valor` (float) | 4 | 16 | — |
| `valor_secundario` (float) | 4 | 20 | — |
| `categoria` (enum/int) | 4 | 24 | — |
| `status` (uint8_t) | 1 | 28 | — |
| *(padding implícito)* | 3 | 29 | `tag` não precisa alinhar mas o compilador pode inserir |
| `tag` (char[32]) | 32 | 32 | — |
| `origem` (char[16]) | 16 | 64 | — |
| `checksum` (uint32_t) | 4 | 80 | — |
| `_pad` (uint8_t[8]) | 8 | 84 | padding **explícito** para chegar em 96 |

> **Dicionário Rápido desta Seção:**
> - **Offset**: A distância (em bytes) do início da struct até onde aquele campo específico começa. Por exemplo, `valor` está a 16 bytes do início.
> - **Padding implícito**: Espaços vazios inseridos automaticamente pelo compilador. Ele faz isso porque a CPU lê a memória mais rápido se os dados estiverem em posições (offsets) múltiplas do seu tamanho (ex: variáveis de 8 bytes começando em posições múltiplas de 8).
> - **Padding explícito**: Espaços vazios que o programador insere de propósito (aqui, o `_pad[8]`) para garantir que o tamanho total final seja exatamente o que ele quer (96).
> - **Warp CUDA**: Na GPU da NVIDIA, as threads não andam sozinhas. Elas são agrupadas em blocos inseparáveis de 32 threads chamados "warps". Todos num warp executam a mesma instrução ao mesmo tempo.
> - **Acesso coalescido**: É quando as 32 threads de um warp pedem para ler um bloco de endereços coladinhos uns nos outros. A GPU percebe isso e faz uma única leitura gigante ultrarrápida, em vez de 32 leituras lentas e separadas.

> **Por que 96 bytes e não 88 ou 128?**
> 96 = múltiplo de 32 (tamanho de um warp CUDA). Quando o kernel acessa `dados[i].valor`, a GPU lê em transações de 128 bytes alinhadas. Com structs de 96 bytes, um warp de 32 threads acessa exatamente 3 × 1024 bytes = 3 linhas de cache — acesso coalescido e eficiente.

---

## [T04] Como Funciona a Geração de Dados (`generator.c`)

O gerador cria eventos **sintéticos mas realistas**. Usa `srand(42)` — semente fixa — para que cada execução produza exatamente os mesmos dados (reprodutibilidade científica).

### Distribuição Gaussiana — Box-Muller

```c
static float gaussiana(float media, float desvio) {
    float u, v, s;
    do {
        u = rand_float() * 2.0f - 1.0f;  // uniforme em [-1, 1]
        v = rand_float() * 2.0f - 1.0f;
        s = u*u + v*v;
    } while (s >= 1.0f || s == 0.0f);    // rejeita pontos fora do círculo
    float fator = sqrtf(-2.0f * logf(s) / s);
    return media + desvio * (u * fator);
}
```

**Por que Box-Muller?** `rand()` gera distribuição **uniforme**. Sensores reais geram leituras com distribuição **normal** (temperatura, pressão). O método transforma 2 uniformes em 2 normais usando a identidade matemática de Box-Muller (1958).

### Distribuição de Poisson para timestamps

```c
static int64_t intervalo_poisson(double taxa_ms) {
    double u = (double)rand() / RAND_MAX;
    return (int64_t)(-taxa_ms * log(u));  // exponencial inversa
}
```

Eventos reais não chegam em intervalos fixos. O intervalo entre eventos de Poisson segue distribuição exponencial: `X ~ Exp(λ)`, onde `λ = 1/taxa_ms`.

### Distribuição não uniforme de categorias

```c
static Categoria categoria_realista(void) {
    int r = rand() % 100;
    if (r < 70) return CAT_NORMAL;   // 70%
    if (r < 90) return CAT_ALERTA;   // 20%
    if (r < 98) return CAT_CRITICO;  //  8%
    return CAT_INATIVO;              //  2%
}
```

### Correlação entre valor e valor_secundario

```c
e->valor_secundario = VALOR2_CORRELACAO * e->valor
    + sqrtf(1.0f - VALOR2_CORRELACAO * VALOR2_CORRELACAO) * ruido;
```

Isso é a fórmula de correlação de Pearson. `r = 0.7` significa que as duas leituras se correlacionam 70% — como temperatura e umidade do mesmo sensor.

### Checksum XOR

```c
return e->id ^ (uint32_t)e->timestamp ^ *(uint32_t*)&e->valor
       ^ (uint32_t)e->categoria ^ (uint32_t)e->status;
```

O `*(uint32_t*)&e->valor` é um **type-pun**: reinterpreta os 4 bytes do float como um uint32_t sem conversão aritmética. Detecta corrupção de memória durante o benchmark.

> **Dicionário Rápido desta Seção:**
> - **Type-pun**: Uma técnica (uma "gambiarra" comum em C) para pegar um pedaço de memória que guarda uma coisa (um número quebrado `float`) e ler como se fosse outra (um número inteiro `uint32_t`), só para ver os zeros e uns exatos que estão ali, sem fazer contas de conversão.

---

## [T05] OpenMP — Como Funciona

OpenMP é uma **API de paralelismo de memória compartilhada** para C/C++/Fortran. Funciona com **pragmas** (diretivas de compilador) que dividem loops entre threads do SO.

> **Dicionário Rápido desta Seção:**
> - **API de paralelismo de memória compartilhada**: Significa que várias threads do processador vão trabalhar juntas e todas vão compartilhar e acessar a mesma memória RAM do computador.
> - **Pragmas**: Comandos (`#pragma`) que você dá diretamente para o compilador. Eles não mudam a lógica matemática, mas dizem "Ei compilador, pegue esse for-loop normal e transforme em um que roda em paralelo".
> - **Race condition (Condição de corrida)**: Um bug invisível e perigoso. Acontece quando duas threads tentam mexer na mesma variável ao mesmo tempo. Ex: as duas leem `0`, somam `1` e guardam `1`. O certo seria o final ser `2`.
> - **SMT (Simultaneous Multithreading) / HyperThreading**: Tecnologia onde o processador finge ter 2 núcleos virtuais para cada 1 núcleo físico real, aproveitando os milissegundos ociosos do núcleo.

### Pragma básico

```c
#pragma omp parallel for schedule(static)
for (int i = 0; i < n; i++) {
    // cada thread processa uma fatia diferente de i
}
```

O compilador (GCC com `-fopenmp`, MSVC com `/openmp`) transforma isso em código que:
1. Cria um pool de N threads (do SO)
2. Divide o range `[0, n)` em N fatias iguais (`schedule(static)`)
3. Cada thread executa sua fatia em paralelo
4. Barreira implícita ao final: todas esperem antes de continuar

### Reduction — sem race condition no contador

```c
int count = 0;
#pragma omp parallel for reduction(+:count)
for (int i = 0; i < n; i++) {
    if (dados[i].valor >= min && dados[i].valor <= max)
        count++;
}
```

Sem `reduction`, múltiplas threads incrementando `count` causariam **race condition** (resultado errado). Com `reduction(+:count)`:
- Cada thread tem sua **cópia local** de `count`
- Ao final, o runtime OpenMP soma todas as cópias em `count` com uma **árvore de redução**

### Estratégia manual de paralelismo (busca linear OpenMP)

No projeto, a `busca_linear_openmp` usa uma estratégia manual mais sofisticada:

```c
Event ***temp  = malloc(threads * sizeof(Event **));
// cada thread salva seus resultados em temp[tid][]
#pragma omp parallel for
for (i = 0; i < n; i++) {
    int tid = omp_get_thread_num();
    temp[tid][counts[tid]++] = &dados[i];
}
// depois, junta tudo sequencialmente
```

Isso evita que threads tentem escrever no mesmo array de saída ao mesmo tempo.

### Variáveis de ambiente OpenMP

```c
putenv("OMP_PROC_BIND=close");   // pina threads em núcleos próximos (menos latência L2/L3)
putenv("OMP_PLACES=cores");      // 1 thread por núcleo físico (evita SMT desnecessário)
```

---

## [T06] Makefile — Como Funciona

O Makefile é lido pelo programa `make` (GNU Make). A sintaxe básica é:

```makefile
alvo: dependências
	comando  ← TAB obrigatório (não espaços!)
```

> **Dicionário Rápido desta Seção:**
> - **Linkagem (LDFLAGS, -lm)**: É a última etapa antes do executável ficar pronto. O "linker" (agrupador) junta o código que você escreveu com as bibliotecas prontas do sistema (como a biblioteca matemática `-lm`) para formar um só programa.
> - **Binário Estático (`-static`)**: Um executável que empacota todas as bibliotecas necessárias dentro de si mesmo. Ele fica maior (mais pesadinho), mas roda em qualquer lugar sem dar o famoso erro de "DLL faltando".
> - **Símbolos de debug (`-s` / strip)**: Textos e nomes de variáveis que o compilador guarda dentro do `.exe` para, caso dê erro, ele consiga te mostrar a linha exata. Arrancar eles (strip) deixa o `.exe` bem mais leve.

### Análise linha a linha

```makefile
CC      = gcc              # compilador C
CFLAGS  = -std=c99 -Wall -Wextra -O2   # flags padrão
LDFLAGS = -lm             # linka libm (funções matemáticas: sqrt, log...)
OPENMP  = -fopenmp        # habilita OpenMP no GCC
SRC     = generator.c search.c sim_math.c benchmark.c
```

### Target `all` (padrão)

```makefile
all: benchmark   # rodar "make" sem argumento executa este target
```

### Target `benchmark`

```makefile
benchmark: $(SRC) event.h search.h
	$(CC) $(CFLAGS) $(OPENMP) -o benchmark $(SRC) $(LDFLAGS)
```

Expande para:
```bash
gcc -std=c99 -Wall -Wextra -O2 -fopenmp -o benchmark \
    generator.c search.c sim_math.c benchmark.c -lm
```

- `-std=c99`: usa padrão C99 (permite `//` comentários, `for(int i=...)`...)
- `-Wall -Wextra`: ativa todos os avisos do compilador
- `-O2`: otimização nível 2 (inlining, eliminação de código morto, vetorização)
- `-fopenmp`: habilita `#pragma omp` e linka `libgomp`
- `-lm`: linka `libm` (math.h)

### Target `dist` (binário estático)

```makefile
dist:
	$(CC) $(CFLAGS) $(OPENMP) -static -s \
		-o benchmark_dist \
		$(SRC) \
		$(LDFLAGS) -lgomp -lpthread
```

- `-static`: inclui TODAS as bibliotecas no executável (sem DLLs externas)
- `-s`: strip — remove símbolos de debug (menor tamanho)
- `-lgomp`: biblioteca OpenMP explícita (necessária com -static)
- `-lpthread`: pthreads (OpenMP usa pthreads por baixo no Linux)

### Target `benchmark_cuda`

```makefile
NVCC       = nvcc
ARCH       = -arch=sm_75
CUDA_FLAGS = $(ARCH) -O3 -Xcompiler "-fopenmp" -DHAS_CUDA
CUDA_SRC   = search_cuda.cu sim_math_cuda.cu

benchmark_cuda: $(SRC) $(CUDA_SRC) event.h search.h search_cuda.h sim_math_cuda.h
	$(NVCC) $(CUDA_FLAGS) -o benchmark_cuda $(SRC) $(CUDA_SRC) -lm
```

- `nvcc`: compilador da NVIDIA (processa `.cu` e delega `.c` para o compilador C host)
- `-arch=sm_75`: gera código para Compute Capability 7.5 (GTX 1650, RTX 2xxx/3xxx)
- `-Xcompiler "-fopenmp"`: passa `-fopenmp` para o compilador host (GCC/MSVC)
- `-DHAS_CUDA`: define o macro `HAS_CUDA` — ativa os blocos `#ifdef HAS_CUDA` no benchmark.c

### Target `clean`

```makefile
clean:
	rm -f benchmark benchmark_noomp benchmark_dist benchmark_dist_noomp benchmark_cuda *.obj *.o
```

Remove todos os binários gerados. No Windows com MinGW o `rm` funciona via bash integrado.

### `.PHONY`

```makefile
.PHONY: all dist dist_noomp benchmark_noomp benchmark_cuda clean
```

Diz ao make que esses targets **não são arquivos**. Sem isso, se existir um arquivo chamado `clean`, `make clean` não faria nada.

---

## [T07] `build_cuda.bat` — Como Funciona e Alternativas

### Por que existe o .bat?

O Makefile usa `gcc` — funciona no Linux e no Windows com MinGW/MSYS2. Porém, o `nvcc` no Windows funciona melhor com o compilador **MSVC** (`cl.exe`) do Visual Studio como compilador host. O `.bat` resolve isso.

### Análise do bat

```bat
@echo off           ← não exibe os próprios comandos no terminal
```

A lógica principal é um bloco PowerShell embutido:

```bat
powershell -ExecutionPolicy Bypass -Command ^
  "$cl = (Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' ..."
```

**Por que PowerShell dentro do .bat?**
- O `.bat` puro tem sintaxe limitada para manipular strings e caminhos
- PowerShell tem `Get-ChildItem` (busca recursiva de arquivos), `Copy-Item`, `New-Item`
- A combinação: interface .bat (duplo clique) + lógica PowerShell (poderosa)

### Linha de compilação CUDA

```powershell
nvcc.exe -O2 -arch=sm_86 -ccbin $cl -Xcompiler '/O2,/openmp,/W3' -DHAS_CUDA `
    benchmark.c generator.c search.c sim_math.c search_cuda.cu sim_math_cuda.cu `
    -o benchmark_cuda.exe
```

- `-arch=sm_86`: Ampere (RTX 3xxx). **Diferente do Makefile** que usa `sm_75` (Turing)!
- `-ccbin $cl`: usa `cl.exe` do Visual Studio como compilador host
- `-Xcompiler '/O2,/openmp,/W3'`: passa flags MSVC: `/O2`=otimização, `/openmp`=OpenMP, `/W3`=avisos
- Compila `.c` e `.cu` juntos numa única chamada ao `nvcc`

### O que o .bat empacota

```
dist_com_cuda\
  benchmark_cuda.exe   ← binário com CUDA
  hardware.cfg         ← configuração (obrigatório!)
  vcomp140.dll         ← DLL do OpenMP do Visual Studio

dist_sem_cuda\
  benchmark.exe        ← binário CPU-only (estático)
  hardware.cfg
```

### Existe alternativa melhor ao .bat?

**Sim.** As opções modernas em ordem de recomendação:

| Ferramenta | Linguagem | Vantagens |
|---|---|---|
| **CMake** | DSL própria | Padrão da indústria, detecta CUDA/OpenMP automaticamente |
| **Ninja + CMake** | DSL | CMake gera Ninja: build mais rápido que Make |
| **xmake** | Lua | Simples, moderno, suporte nativo a CUDA |
| **PowerShell puro** | PowerShell | Sem dependências, legível, já instalado no Windows |
| **Python (invoke/nox)** | Python | Legível, cross-platform, fácil de manter |

> **Dicionário Rápido desta Seção:**
> - **DSL (Domain-Specific Language)**: Uma linguagem de programação criada para fazer apenas uma tarefa muito específica. O CMake usa uma linguagem que só serve para configurar compilações de projetos C/C++, e para mais nada.

**Exemplo equivalente em CMake** (o padrão do mercado):

```cmake
cmake_minimum_required(VERSION 3.18)
project(benchmark LANGUAGES C CXX CUDA)

find_package(OpenMP REQUIRED)

add_executable(benchmark_cuda
    benchmark.c generator.c search.c sim_math.c
    search_cuda.cu sim_math_cuda.cu
)

target_compile_definitions(benchmark_cuda PRIVATE HAS_CUDA)
target_link_libraries(benchmark_cuda PRIVATE OpenMP::OpenMP_C m)
set_target_properties(benchmark_cuda PROPERTIES
    CUDA_ARCHITECTURES "75;86"   # gera para Turing E Ampere
)
```

```bash
mkdir build && cd build
cmake .. -G Ninja
ninja
```

**Vantagem chave do CMake**: `CUDA_ARCHITECTURES "75;86"` gera código para **múltiplas GPUs** num único binário — o driver escolhe em runtime. O `.bat` e o Makefile atual compilam para uma arquitetura fixa.

