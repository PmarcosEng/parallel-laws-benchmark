# Parallel Laws Benchmark — Amdahl & Gustafson na prática

Benchmark científico em **C** que **mede empiricamente** as **Leis de Amdahl e Gustafson**, rodando o mesmo algoritmo em três "cérebros" do computador e comparando o ganho de velocidade real com o que a teoria prevê:

> **CPU serial** (1 thread) · **CPU paralela** (OpenMP, N threads) · **GPU NVIDIA** (CUDA)

![C](https://img.shields.io/badge/C-C99-00599C?logo=c) ![OpenMP](https://img.shields.io/badge/Paralelismo-OpenMP-orange) ![CUDA](https://img.shields.io/badge/GPU-CUDA-76B900?logo=nvidia) ![CMake](https://img.shields.io/badge/Build-CMake-064F8C?logo=cmake) ![Dashboard](https://img.shields.io/badge/Dashboard-Chart.js-ff6384)

> Projeto do 2º artigo de **Arquitetura de Soluções**.
> **Título:** *Benchmark Empírico das Leis de Amdahl e Gustafson em CPU Serial, OpenMP e GPU CUDA.*

---

## A pergunta de pesquisa

> Em que medida as leis de **Amdahl** e **Gustafson** conseguem explicar, na prática, o *speedup* obtido por algoritmos distintos com o mesmo objetivo, executados em **CPU serial**, **CPU paralela (OpenMP)** e **GPU (CUDA)** — e como o **volume de dados** influencia esse comportamento?

O fim do escalonamento do clock forçou a indústria a trocar de escala: em vez de processadores mais rápidos, mais processadores por chip (e GPUs para computação geral). Com isso, o desempenho passou a depender do **encaixe entre software e hardware**. Este benchmark torna esse encaixe visível, medindo dois regimes opostos:

- **Cargas memory-bound** (buscas) — o gargalo é o acesso à memória, não o cálculo. Território da **Lei de Amdahl**: a fração serial e a latência de memória impõem um teto baixo ao paralelismo.
- **Cargas compute-bound** (matemática densa) — o gargalo é o cálculo puro. Território da **Lei de Gustafson**: quanto maior o volume, mais linear e alto o ganho — é onde a GPU dispara.

O modelo **Roofline** (Williams, Waterman & Patterson, 2009) costura os dois: ele situa cada algoritmo entre o teto de memória e o teto de computação conforme sua intensidade operacional (FLOPs por byte).

---

## As duas leis, em uma frase cada

- **Lei de Amdahl** — a velocidade máxima é limitada pela parte que *não* dá pra paralelizar. Se 10% do código é serial, o teto de ganho é 10× por mais núcleos que você jogue no problema.
- **Lei de Gustafson** — com mais paralelismo você não resolve o *mesmo* problema mais rápido; você resolve um problema **maior** no mesmo tempo. Escalar o volume junto com os threads mantém (ou melhora) o speedup.

O benchmark gera dados sintéticos, roda cada algoritmo em todos os modos, calcula o **speedup** (`T_serial ÷ T_paralelo`) e salva tudo em JSON para visualização num dashboard interativo.

---

## Modos e algoritmos

| Modo | Tecnologia | Onde roda | Analogia |
|------|-----------|-----------|----------|
| `serial` | C puro, 1 thread | CPU | **1 operário forte.** Um item por vez. |
| `openmp` | OpenMP, N threads | CPU paralela | **Vários operários.** Dividem o trabalho — às vezes esbarram uns nos outros. |
| `cuda` | CUDA real | GPU NVIDIA | **Exército de milhares de formigas.** Cada uma é fraca; juntas, movem montanhas. |

**Algoritmos testados**

- **Busca de eventos** (memory-bound → Amdahl) — Linear `O(n)`, Binária `O(log n)`, Hash `O(1)` amortizado
- **Matemática densa** (compute-bound → Gustafson) — Monte Carlo (estimativa de π) e Fractal de Mandelbrot 2D

> A busca binária é o caso-limite: `O(log n)` faz tão pouco trabalho que o custo de criar threads / copiar dados para a GPU supera qualquer ganho — ela fica **abaixo de 1×**, exatamente o que Amdahl prevê quando a parte paralelizável é minúscula. A ordenação que a binária exige (`qsort`, `O(n log n)`, serial) roda **fora do cronômetro** por ser custo amortizado.

---

## 🔬 Metodologia e parâmetros da simulação

Pesquisa de natureza **experimental e quantitativa**. Cada algoritmo foi implementado nos três alvos (serial / OpenMP / CUDA) e submetido a volumes crescentes de dados; para cada configuração mediu-se o tempo, do qual derivam speedup e eficiência, confrontados com as previsões de Amdahl e Gustafson.

### Máquinas de teste

Três máquinas com perfis distintos de paralelismo (8–16 threads de CPU, 896–8.704 núcleos CUDA), todas Windows:

| # | CPU | Núcleos / Threads | GPU | CUDA cores | RAM | SO |
|:--:|---|:--:|---|:--:|:--:|:--:|
| 1 | AMD Ryzen 7 4800H | 8c / 16t | GTX 1650 | 896 | 8 GB | Windows |
| 2 | Intel i5 12400 | 6c / 12t | RTX 3060 | 3.584 | 32 GB | Windows |
| 3 | Intel i3 10105F | 4c / 8t | RTX 3080 | 8.704 | 8 GB | Windows |

O executável foi **compilado uma única vez** como fat binary cobrindo Turing (`sm_75`, GTX 1650) e Ampere (`sm_86`, RTX 3060/3080) e distribuído às três máquinas com as runtimes CUDA — cada GPU seleciona seu código sem recompilar. Ambiente de compilação: **Visual Studio 2022 Community** + CUDA Toolkit.

### Parâmetros do benchmark

| Parâmetro | Valor |
|---|---|
| **Threads (OpenMP)** | potências de 2 — `1, 2, 4, 8, …` até o máximo lógico da máquina (16 no R7, 12 no i5, 8 no i3) |
| **Volumes de busca** (nº de eventos) | `1.000 · 100.000 · 1.000.000 · 10.000.000 · 20.000.000` |
| **Volumes de matemática** (nº de pontos) | `1.000.000 · 5.000.000 · 10.000.000 · 50.000.000 · 100.000.000` |
| **Repetições** | `n_repeticoes = 9` — mediana das 9, após 1 execução de *warm-up* descartada |
| **Relógio** | monotônico, precisão de nanossegundos |
| **Struct `Event`** | 96 bytes (alinhada p/ cache) |
| **Gerador de dados** | Box-Muller polar sobre `rand()`, **semente fixa `srand(42)`** → mesmo conjunto reproduzido em toda execução e em toda máquina |
| **Busca por faixa/alvo** | `min = 45.0`, `max = 55.0`, `alvo = 50.0` |
| **IDs distintos p/ Hash** | `n_ids_hash = 10.000` (força colisões → lista encadeada → cache miss) |
| **Mandelbrot** | `max_iter = 256` |

**Métricas coletadas:** tempo de execução (mediana, em segundos) · speedup (`T_serial ÷ T_paralelo`) · eficiência (`speedup ÷ nº de processadores`).

---

## 📊 Resultados

> **Em uma frase:** em **cálculo denso** a GPU é imbatível — **até 266× no Mandelbrot** numa RTX 3080. Em **busca** o paralelismo rende pouco (e a binária até atrapalha). É exatamente o que Amdahl e Gustafson preveem.

Runs de **30/06/2026** (`Resultados_Simulação/`), modo de speedup *total-based* (inclui a transferência PCIe no tempo da GPU).

### Pico de speedup por workload

Melhor speedup paralelo (OpenMP ou GPU) sobre o serial, no maior volume testado:

| Workload | Ryzen 7 / GTX 1650 | i5 12400 / RTX 3060 | i3 10105F / RTX 3080 |
|---|:--:|:--:|:--:|
| Busca Linear `O(n)`   | 1,76× | 2,47× | 1,73× |
| Busca Binária `O(log n)` | 0,93× | 0,98× | 0,83× |
| Hash Lookup `O(1)`    | 71,0× | **92,1×** | 86,6× |
| Monte Carlo π         | 13,4× | 22,0× | **68,4×** |
| Mandelbrot 2D         | 163,3× | 199,6× | **266,3×** |

### A anomalia do PCIe (achado central do artigo)

A transferência **DRAM → VRAM** pela barra PCIe é um custo fixo que o kernel da GPU não vê. Quanto mais rápido o kernel roda, **maior o peso relativo** dessa transferência. No Mandelbrot, à medida que o volume cresce, a fração de tempo gasta em PCIe **despenca de ~40–50% para ~1–6%** — o cálculo passa a dominar e o custo de cópia se dilui (Gustafson na veia).

O contraste entre máquinas é o ponto: numa GPU com muitos recursos (RTX 3080), o cálculo é resolvido tão rápido que **a transferência volta a pesar** — a mesma carga que é compute-bound numa GPU modesta beira o memory-bound numa GPU potente. É o dimensionamento software↔hardware determinando onde mora o gargalo.

> Os gráficos completos (speedup por thread, teto de Amdahl, curva de Gustafson, decomposição PCIe × kernel, radar entre máquinas) são gerados de forma interativa no **dashboard** — veja abaixo.

---

## 🛠️ Como compilar

O código-fonte fica em [`Code/`](Code). Duas rotas de build:

### CMake (recomendado — Linux, WSL e Windows, com CUDA multi-arquitetura)

```bash
cd Code
cmake -S . -B build
cmake --build build --config Release
```

O `CMakeLists.txt` gera um **fat binary** cobrindo Turing → Blackwell (`sm_75, 86, 89, 120` + PTX de reserva), então o mesmo executável roda em GPUs da GTX 16xx às RTX 50xx. Se o CMake não reconhecer `sm_120`, é o toolkit velho — atualize o CUDA Toolkit.

### Makefile (Linux/WSL — atalho para CPU ou build direto)

```bash
cd Code
make                 # CPU: serial + OpenMP (binário dinâmico)
make dist            # CPU: binário estático, redistribuível sem dependências
make benchmark_cuda  # CPU + GPU CUDA real (requer nvcc / CUDA Toolkit)
```

**Requisitos:** `gcc` + `libgomp` (OpenMP) para os modos de CPU; **CUDA Toolkit** (`nvcc`) para o modo GPU. Sem GPU NVIDIA, os modos de CPU rodam normalmente.

## ⚙️ Como configurar

Edite [`Code/hardware.cfg`](Code/hardware.cfg) antes de rodar — o programa lê este arquivo obrigatoriamente ao iniciar:

```ini
cpu_nome=r7
cpu_nucleos=8
cpu_threads=16          # testa threads 1, 2, 4, 8, ..., cpu_threads
gpu_nome=GTX 1650
gpu_cuda_cores=896      # GTX 1650=896 · RTX 3060=3584 · RTX 3080=8704
ram_gb=4
n_repeticoes=3          # mediana de N execuções (1 warmup descartado)
volumes_busca=1000,100000,1000000,10000000
volumes_math=100000,500000,1000000
```

> ⚠️ Cada evento de busca ocupa ~96 bytes. 10 M de eventos ≈ 2 GB de RAM total. Se o PC travar, remova os maiores volumes de `volumes_busca`. A matemática densa não aloca arrays grandes — é segura em qualquer volume.

## ▶️ Como rodar

```bash
./benchmark_cuda        # ou ./benchmark  /  benchmark.exe (conforme o build)
```

O programa lê `hardware.cfg`, gera os dados sintéticos, executa todos os modos e salva os resultados em **`runs/busca_<gpu>/`** e **`runs/matematica_<gpu>/`** (arquivos JSON com timestamp).

## 📈 Como visualizar

Abra [`Dashboard/dashboard.html`](Dashboard/dashboard.html) no navegador e **arraste os JSONs** gerados para dentro da página. Abas disponíveis:

- **Busca de Eventos** e **Matemática Densa** — speedup, teto de Amdahl e curva de Gustafson por algoritmo
- **Eficiência** — E = Speedup ÷ p (Amdahl/Gustafson por thread; GPU por CUDA core)
- **Comparar Runs** — sobrepõe execuções diferentes
- **Panorama Geral** — radar + heatmap entre hardwares
- **Comparar Hardware** — agrupa automaticamente por CPU/GPU

Há execuções de exemplo prontas em [`Resultados_Simulação/`](Resultados_Simulação) (as três máquinas acima).

---

## 🗂️ Estrutura do projeto

```
parallel-laws-benchmark/
├── Code/                    # Código-fonte C + CUDA
│   ├── benchmark.c          # main() — o maestro: lê config, mede, salva JSON
│   ├── config.c/.h          # HardwareConfig + grades de volume/thread
│   ├── timing.c/.h          # cronômetro, mediana, macro CRONOMETRAR_MEDIANA
│   ├── report.c/.h          # toda a saída: banner, tabelas e JSON
│   ├── generator.c/.h       # dados sintéticos (Box-Muller, Poisson, checksum)
│   ├── event.h              # struct Event de 96 bytes (alinhada p/ cache)
│   ├── search.c/.h          # busca na CPU (serial + OpenMP)
│   ├── search_cuda.cu/.h    # busca na GPU (CUDA)
│   ├── sim_math.c/.h        # Monte Carlo + Mandelbrot na CPU
│   ├── sim_math_cuda.cu/.h  # Monte Carlo + Mandelbrot na GPU
│   ├── hardware.cfg         # configuração do usuário
│   ├── CMakeLists.txt       # build multi-arch (Linux/WSL/Windows)
│   └── Makefile             # build Linux/WSL (atalhos de CPU/CUDA)
├── Dashboard/               # dashboard.html (Chart.js) — arraste os JSONs
├── Resultados_Simulação/    # JSONs de exemplo das três máquinas
├── dist/                    # binários redistribuíveis (Linux-x64, Windows-x64)
└── docs_projeto/            # artigo (PDF) e material de apoio
```

## 🧰 Stack técnica

`C99` · `OpenMP` · `CUDA` (nvcc, fat binary `sm_75/86/89/120`) · build `CMake` + `Makefile` · dados em `JSON` · dashboard em `HTML` + `Chart.js`

---

<sub>Projeto acadêmico — 2º artigo de Arquitetura de Soluções. Referencial teórico, metodologia e discussão completa no artigo em <a href="docs_projeto/">docs_projeto/</a>.</sub>
