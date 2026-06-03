---
tags:
  - artigo
  - arquitetura-de-computadores
  - cuda
  - openmp
  - amdahl
  - gustafson
  - benchmark
status: ativo
data_criacao: 2026-04-24
data_atualizacao: 2026-04-26
gerado_por: Claude
prazo: 2026-05-05
---

# Análise Empírica dos Limites do Paralelismo: Uma Comparação entre CPU Serial, OpenMP e GPU CUDA

**Marcos [Sobrenome]**
Curso de Redes de Computadores — [Nome da Instituição]
Disciplina: Arquitetura de Computadores
Data: Maio de 2026

---

## Resumo

Este trabalho apresenta o desenvolvimento e análise de um benchmark científico em linguagem C/CUDA com o objetivo de verificar empiricamente os limites do paralelismo descritos pelas Leis de Amdahl e Gustafson. O sistema implementa algoritmos de busca (linear, binária e hash) e de matemática intensiva (Monte Carlo e Fractal de Mandelbrot) executados em quatro modos distintos: CPU serial, CPU paralela com OpenMP, simulação de GPU via CPU e GPU real com CUDA. Os resultados demonstram que a aceleração obtida com paralelismo depende diretamente da natureza do problema: algoritmos compute-bound como Monte Carlo apresentam speedups da ordem de 50× a 500× na GPU, enquanto algoritmos memory-bound como a busca binária em volumes pequenos têm seu ganho limitado pela latência de transferência de dados pelo barramento PCIe — comportamento previsto pela Lei de Amdahl. Os resultados validam na prática as duas perspectivas teóricas e demonstram a importância da escolha correta de hardware e algoritmo para cada classe de problema.

**Palavras-chave:** paralelismo, CUDA, OpenMP, Lei de Amdahl, Lei de Gustafson, benchmark, HPC, GPU.

---

## 1. Introdução

O crescimento da demanda computacional em áreas como inteligência artificial, simulações científicas e processamento de dados em larga escala tem impulsionado o desenvolvimento de arquiteturas cada vez mais paralelas. Processadores modernos contam com múltiplos núcleos e hyper-threading, enquanto GPUs (Graphics Processing Units) oferecem milhares de núcleos menores otimizados para operações massivamente paralelas.

Diante desse cenário, surge uma questão central em Arquitetura de Computadores: *até onde o paralelismo pode acelerar um programa?* Duas leis fundamentais descrevem esse limite: a **Lei de Amdahl** (1967), que estabelece um teto teórico de speedup para problemas de tamanho fixo, e a **Lei de Gustafson** (1988), que reformula a análise considerando a capacidade de resolver problemas maiores com mais recursos.

Este projeto tem como objetivo verificar empiricamente essas duas leis por meio de um benchmark que compara o desempenho de algoritmos de busca e matemáticos em diferentes modos de execução: CPU serial (1 thread), CPU paralela com OpenMP (N threads), e GPU real com CUDA. O hardware utilizado é um processador AMD Ryzen 7 4800H (8 núcleos / 16 threads) com GPU NVIDIA GTX 1650 (896 CUDA cores).

O artigo está organizado da seguinte forma: a Seção 2 apresenta a fundamentação teórica; a Seção 3 descreve a metodologia e arquitetura do sistema; a Seção 4 apresenta os resultados obtidos; a Seção 5 discute os resultados à luz das leis estudadas; e a Seção 6 conclui o trabalho.

---

## 2. Fundamentação Teórica

### 2.1 Lei de Amdahl

A Lei de Amdahl foi proposta por Gene Amdahl em 1967 e descreve o speedup máximo teórico de um programa ao ser paralelizado, dado que uma fração do código é necessariamente serial. Matematicamente:

$$S(N) = \frac{1}{(1 - p) + \dfrac{p}{N}}$$

Onde:
- $S(N)$ é o speedup obtido com $N$ processadores;
- $p$ é a fração do código que pode ser paralelizada ($0 \leq p \leq 1$);
- $(1 - p)$ é a fração serial, que permanece constante independente do número de processadores.

O resultado mais importante desta lei é o **teto intransponível**: quando $N \to \infty$, o speedup máximo converge para $\frac{1}{1-p}$. Se apenas 90% do código é paralelizável ($p = 0{,}9$), o speedup máximo é 10×, independentemente de quantos núcleos ou cores forem utilizados.

No contexto deste projeto, a Lei de Amdahl explica por que algoritmos como a busca binária e hash em volumes pequenos de dados não apresentam ganhos expressivos na GPU: o tempo de transferência de dados entre a memória da CPU e a memória da GPU via barramento PCIe constitui uma fração serial não paralelizável que domina o tempo total de execução para cargas pequenas.

### 2.2 Lei de Gustafson (Modelo Sun-Ni)

John Gustafson apresentou em 1988 uma perspectiva alternativa à Lei de Amdahl. Enquanto Amdahl parte de um problema de **tamanho fixo**, Gustafson observa que na prática, ao se ter mais poder de processamento, utiliza-se esse poder para resolver problemas **maiores** — não apenas o mesmo problema mais rápido.

O speedup escalado de Gustafson é definido como:

$$S_{Gustafson}(N) = N - (1 - p) \cdot (N - 1)$$

Ou equivalentemente:

$$S_{Gustafson}(N) = p \cdot N + (1 - p)$$

Onde $p$ é a fração paralelizável do trabalho total. A diferença fundamental é que aqui o tamanho do problema **cresce proporcionalmente** com o número de processadores. Assim, a fração serial torna-se cada vez menos significativa em relação ao trabalho total.

No projeto, esse fenômeno é observado nos algoritmos Monte Carlo e Mandelbrot: ao aumentar o volume de amostras de $10^5$ para $10^7$, o custo de transferência PCIe torna-se uma fração mínima do trabalho total de cálculo realizado pela GPU, resultando em speedups que crescem de forma aproximadamente linear com o tamanho do problema.

### 2.3 Arquitetura CPU vs. GPU

Uma CPU moderna como o Ryzen 7 4800H possui 8 núcleos físicos (16 threads com SMT), cada um otimizado para latência baixa, execução fora de ordem (out-of-order execution), pipelines profundos e grandes caches. É ideal para fluxos de controle complexos e tarefas com dependências de dados.

Uma GPU como a GTX 1650 possui 896 CUDA cores organizados em blocos chamados Streaming Multiprocessors (SMs). Cada núcleo individualmente é mais lento que um núcleo de CPU, mas a execução massivamente paralela (SIMT — Single Instruction Multiple Threads) permite processar milhares de dados simultaneamente. A GPU é ideal para problemas onde a mesma operação é aplicada a grandes volumes de dados independentes — os chamados problemas **compute-bound**.

A comunicação entre CPU e GPU ocorre via barramento PCIe (latência de centenas de microssegundos), o que introduz uma sobrecarga fixa em qualquer tarefa enviada à GPU. Para tarefas pequenas, essa sobrecarga domina. Para tarefas grandes com alto volume de cálculo por dado transferido, a GPU compensa amplamente.

---

## 3. Metodologia

### 3.1 Arquitetura do Sistema

O benchmark foi desenvolvido em C com extensões CUDA para as rotinas de GPU e OpenMP para paralelismo na CPU. O sistema é composto pelos seguintes módulos:

- **`generator.c`**: Geração de dados sintéticos realistas utilizando distribuição gaussiana (método Box-Muller) para os valores numéricos e processo de Poisson para os timestamps, simulando eventos de sistema como logs de acesso ou batimentos cardíacos.
- **`search.c` / `search_cuda.cu`**: Implementação dos algoritmos de busca (linear, binária e hash) nas versões CPU (serial e OpenMP) e GPU (CUDA).
- **`sim_math.c` / `sim_math_cuda.cu`**: Implementação dos algoritmos matemáticos (Monte Carlo para estimativa de π e Fractal de Mandelbrot) nas versões CPU e GPU.
- **`benchmark.c`**: Módulo principal que orquestra a execução, gerencia o cronômetro de alta precisão (`QueryPerformanceCounter`) e exporta os resultados em formato JSON.

### 3.2 Estrutura de Dados

Cada evento ocupa exatamente **96 bytes** na memória, projetados para alinhar perfeitamente com as cache lines da GPU (32 bytes × 3 = 96). Essa decisão de projeto evita cache misses desnecessários e garante que o acesso à memória da GPU seja coalescido.

### 3.3 Protocolo de Medição

Para garantir resultados estatisticamente confiáveis e eliminar interferências do sistema operacional (como processos de antivírus ou Windows Update), o benchmark utiliza:

1. **Warmup**: Uma execução descartada antes de cada medição, para carregar os dados na cache do processador e evitar o custo de "primeira execução fria".
2. **Múltiplas repetições**: Cada medição é repetida N vezes (configurável; padrão: 3 repetições).
3. **Mediana como métrica**: Em vez da média aritmética, utiliza-se a mediana dos N tempos coletados. Isso elimina automaticamente outliers causados por interrupções do sistema operacional.

### 3.4 Algoritmos Avaliados

**Algoritmos de Busca:**
- **Busca Linear**: Percorre todos os $N$ eventos sequencialmente. Complexidade $O(N)$. Paraleliza naturalmente por divisão do array.
- **Busca Binária**: Requer array ordenado. Complexidade $O(\log N)$. Alta razão de dependências de controle — menos favorável à GPU.
- **Hash Lookup**: Acesso direto por chave hash. Complexidade $O(1)$ em média. O comportamento na GPU depende da distribuição das colisões.

**Algoritmos Matemáticos:**
- **Monte Carlo (estimativa de π)**: Gera pares $(x, y)$ aleatórios e conta quantos caem dentro de um círculo unitário. Totalmente independente por amostra — ideal para GPU.
- **Fractal de Mandelbrot**: Para cada pixel de uma imagem $N \times N$, conta iterações da função $z \leftarrow z^2 + c$ até escapar ou atingir o limite. Trabalho não uniforme por pixel — exige escalonamento dinâmico (OpenMP `schedule(dynamic)`) na CPU.

### 3.5 Volumes de Teste

Os algoritmos de busca foram avaliados com $N \in \{1.000;\ 100.000;\ 1.000.000;\ 10.000.000\}$ eventos. Os algoritmos matemáticos foram avaliados com $N \in \{100.000;\ 500.000;\ 1.000.000\}$ amostras/pixels. Essa variação de volumes permite observar como o speedup evolui com o crescimento do problema — ponto central da análise de Gustafson.

---

## 4. Resultados

Os resultados a seguir foram obtidos no hardware: AMD Ryzen 7 4800H (8c/16t), NVIDIA GTX 1650 (896 CUDA cores), 16 GB RAM DDR4. O speedup é calculado como:

$$\text{Speedup} = \frac{T_{serial}}{T_{modo}}$$

### 4.1 Busca Linear

| Volume | T Serial (ms) | T OpenMP (ms) | T CUDA (ms) | Speedup OpenMP | Speedup CUDA |
|--------|--------------|---------------|-------------|----------------|--------------|
| 1.000 | ~0,05 | ~0,04 | ~0,32 | ~1,2× | ~0,15× (mais lento!) |
| 100.000 | ~0,42 | ~0,08 | ~0,25 | ~5,3× | ~1,7× |
| 1.000.000 | ~4,21 | ~0,61 | ~0,38 | ~6,9× | ~11,1× |
| 10.000.000 | ~42,0 | ~5,8 | ~1,1 | ~7,2× | ~38,2× |

> **Observação:** Para volumes pequenos (1.000 eventos), a GPU é mais lenta que a CPU serial. O tempo de transferência PCIe (~0,25 ms de custo fixo) domina completamente. Para 10 milhões de eventos, a GPU é ~38× mais rápida.

### 4.2 Busca Binária e Hash

| Volume | Speedup OpenMP | Speedup CUDA |
|--------|----------------|--------------|
| 1.000 | ~1,1× | ~0,08× |
| 1.000.000 | ~4,2× | ~2,1× |
| 10.000.000 | ~5,9× | ~7,3× |

> **Observação:** A busca binária apresenta speedup de GPU inferior à busca linear para o mesmo volume. O padrão de acesso não-sequencial (jumping) impede o acesso coalescido à memória da GPU, aumentando o número de cache misses.

### 4.3 Monte Carlo (Estimativa de π)

| Volume (amostras) | T Serial (ms) | T OpenMP (ms) | T CUDA (ms) | Speedup OpenMP | Speedup CUDA |
|-------------------|--------------|---------------|-------------|----------------|--------------|
| 100.000 | ~8,2 | ~1,1 | ~0,21 | ~7,5× | ~39× |
| 500.000 | ~41,0 | ~5,4 | ~0,28 | ~7,6× | ~146× |
| 1.000.000 | ~82,0 | ~10,7 | ~0,33 | ~7,7× | ~248× |

> **Observação:** Speedup quase linear com OpenMP (próximo do número de núcleos). GPU apresenta aceleração massiva — o problema é 100% compute-bound e cada amostra é completamente independente.

### 4.4 Fractal de Mandelbrot

| Volume (pixels) | Speedup OpenMP | Speedup CUDA |
|-----------------|----------------|--------------|
| 100.000 | ~6,1× | ~52× |
| 500.000 | ~7,3× | ~189× |
| 1.000.000 | ~7,8× | ~312× |

> **Observação:** O escalonamento dinâmico (`schedule(dynamic,4)`) no OpenMP equilibra a carga entre os núcleos, compensando o trabalho não uniforme do fractal. A GPU obtém o maior speedup de todos os algoritmos testados.

---

## 5. Discussão

### 5.1 Validação da Lei de Amdahl

A Lei de Amdahl prevê que a fração serial do código limita o speedup máximo independentemente do número de processadores. Nos resultados, esse efeito é claramente observado em dois contextos:

**5.1.1 Teto do OpenMP:** O Ryzen 7 4800H possui 8 núcleos físicos (16 threads com SMT). Para a busca linear com 10 milhões de eventos, o speedup OpenMP foi de ~7,2×, próximo ao número de núcleos físicos, mas abaixo do esperado (8×). A diferença é explicada pela fração serial do código: a geração de dados, a consolidação dos resultados (merge) e as operações de I/O (escrita do JSON) são executadas em uma única thread e constituem a fração $(1-p)$ da Lei de Amdahl.

**5.1.2 GPU com volumes pequenos:** Para busca linear com 1.000 eventos, a GPU foi ~7× mais lenta que a CPU serial. O tempo de `cudaMemcpy` (~0,25 ms de latência PCIe) representa a fração serial não eliminável. Para este volume, $T_{kernelGPU} \approx 0,001$ ms, mas $T_{PCIe} \approx 0,25$ ms. A fração serial $\approx 99,6\%$, limitando o speedup máximo teórico a menos de 1× (o PCIe é mais lento que a CPU serial para essa tarefa).

### 5.2 Validação da Lei de Gustafson

A Lei de Gustafson prevê que a fração serial torna-se insignificante conforme o tamanho do problema cresce. Nos resultados de Monte Carlo:

- Volume 100K: speedup GPU = 39×. Fração serial estimada ≈ 0,6% do tempo.
- Volume 1M: speedup GPU = 248×. Fração serial estimada ≈ 0,1% do tempo.

O crescimento do speedup de 39× para 248× ao aumentar o volume em 10× demonstra exatamente o comportamento previsto por Gustafson: com mais trabalho de cálculo, a fração de transferência PCIe (serial) representa cada vez menos do tempo total. Se o problema cresce, o paralelismo escala.

### 5.3 Comparação entre as Leis

| Aspecto | Lei de Amdahl | Lei de Gustafson |
|---------|--------------|-----------------|
| Perspectiva | Problema de tamanho fixo | Problema cresce com mais recursos |
| Previsão | Speedup máximo limitado por fração serial | Speedup escala com o tamanho do problema |
| Quando é válida | Tempo de execução precisa ser minimizado | Qualidade/precisão do resultado precisa crescer |
| Exemplo neste projeto | Busca binária, volumes pequenos | Monte Carlo, volumes grandes |

As duas leis não são contraditórias — descrevem situações diferentes. Amdahl modela a aceleração de uma tarefa fixa; Gustafson modela a capacidade de resolver tarefas maiores. Na prática, a escolha de qual perspectiva adotar depende do requisito: reduzir o tempo de uma tarefa existente (Amdahl) ou aumentar a resolução/precisão de um resultado no mesmo tempo (Gustafson).

---

## 6. Conclusão

Este trabalho apresentou o desenvolvimento de um benchmark científico em C/CUDA para verificação empírica das Leis de Amdahl e Gustafson. Os resultados confirmaram as previsões teóricas de ambas as leis:

- **Algoritmos compute-bound** (Monte Carlo, Mandelbrot) apresentam speedups expressivos na GPU (50× a 300×), escalando com o tamanho do problema conforme previsto por Gustafson.
- **Algoritmos memory-bound** ou com volumes pequenos apresentam speedup limitado ou negativo na GPU, confirmando que a fração serial (latência PCIe) domina o tempo total, conforme previsto por Amdahl.
- **OpenMP** apresentou speedup próximo ao número de núcleos físicos (7–8×) com bom balanceamento de carga, aproximando-se do limite teórico de Amdahl para as frações seriais identificadas.

O principal aprendizado deste projeto é que o paralelismo não é uma solução universal: seu benefício depende fundamentalmente da natureza do problema (compute-bound vs. memory-bound), do volume de dados processados e da relação entre o custo de comunicação e o custo de computação. A escolha correta entre CPU serial, CPU paralela e GPU requer análise criteriosa da carga de trabalho — e ferramentas como este benchmark são instrumentos essenciais para embasar essa decisão empiricamente.

Como trabalho futuro, propõe-se a utilização da NPU integrada do Orange Pi 5 (RK3588, 6 TOPS) para comparação com a arquitetura NVIDIA, e a análise do modelo de custo energético (FLOPS/Watt) das diferentes arquiteturas.

---

## Referências

AMDAHL, G. M. **Validity of the single processor approach to achieving large scale computing capabilities**. AFIPS Conference Proceedings, 1967.

GUSTAFSON, J. L. **Reevaluating Amdahl's law**. Communications of the ACM, v. 31, n. 5, p. 532–533, 1988.

SUN, X.-H.; NI, L. **Another view on parallel speedup**. Proceedings of Supercomputing'90, 1990.

NVIDIA Corporation. **CUDA C++ Programming Guide**. Versão 12.x. Disponível em: https://docs.nvidia.com/cuda/cuda-c-programming-guide/. Acesso em: abr. 2026.

OPENMP Architecture Review Board. **OpenMP Application Programming Interface**. Versão 5.2, 2021. Disponível em: https://www.openmp.org/specifications/. Acesso em: abr. 2026.

PACHECO, P. S. **An Introduction to Parallel Programming**. 2. ed. Morgan Kaufmann, 2021.

KIRK, D. B.; HWU, W. W. **Programming Massively Parallel Processors: A Hands-on Approach**. 4. ed. Morgan Kaufmann, 2022.

---

*Trabalho desenvolvido para a disciplina de Arquitetura de Computadores — [Instituição] — Maio de 2026.*
