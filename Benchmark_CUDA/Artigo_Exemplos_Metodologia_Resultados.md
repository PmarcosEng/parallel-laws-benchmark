---
tags: [artigo, metodologia, resultados, exemplo, proposta]
status: ativo
data_criacao: 2026-05-04
data_atualizacao: 2026-05-04
gerado_por: Claude
---

# Exemplos para Metodologia e Resultados Esperados — Artigo de Projeto

> Material de apoio. Você lê, pega o que fizer sentido e **reescreve com suas palavras** no artigo. Não copie e cole.

---

## Contexto importante (gênero do texto)

Este é um **artigo de projeto / proposta de pesquisa**, não relatório final. Implicações:

- **Tudo em tempo futuro:** "será implementado", "serão medidos", "espera-se observar". Nunca "obteve-se", "os resultados mostraram".
- **Sem dados, sem tabelas preenchidas, sem gráficos de speedup real.** Você descreve o experimento que *vai* fazer.
- **O protótipo já implementado é andaime interno seu** — ele te ajuda a escrever uma metodologia mais concreta (porque você sabe exatamente o que vai medir e como), mas **não aparece no artigo**. Não escreva "o benchmark já foi implementado e testado".
- **Resultados Esperados** (Seção 7) é o lugar para fundamentar suas hipóteses com previsões quantitativas embasadas na teoria, **não para mostrar números medidos**.

A entrega final (benchmark + artigo descritivo dos resultados obtidos) é a próxima etapa. Esse texto agora fundamenta *que vale a pena fazer*.

---

## Diagnóstico do PDF atual

Pontos sólidos:
- Introdução, problema de pesquisa e hipóteses estão bem escritos com sua voz.
- Objetivos específicos são concretos e mensuráveis.
- Subseção 5.3 (CUDA/OpenMP, analogia das caixas e `reduction`) é a melhor parte.

Lacunas:
- **6.1 Materiais** — só "Texto genérico". Mesmo na proposta, hardware-alvo precisa ser nomeado.
- **6.2 Estrutura do Benchmark** — parágrafo OK, falta justificar decisões (por que `Event` 96 B? por que mediana?).
- **6.3 Procedimento Experimental** — bullets vazios. Aqui é onde a metodologia ganha nota: você precisa convencer que o experimento **será** confiável.
- **6.4 Análise dos Dados** — uma frase. Falta dizer como você **pretende** comparar curva real com Amdahl/Gustafson — o coração do trabalho.
- **7 Resultados Esperados** — três bullets bons + um "texto genérico". Dá pra refinar com previsões quantitativas embasadas.

---

## 1. Metodologia — exemplos por subseção (tudo em futuro)

### 6.1 Materiais

> A pesquisa será conduzida em duas configurações de hardware, escolhidas por representarem gerações distintas de GPU NVIDIA — Turing (compute capability 7.5) e Ampere (8.6) — permitindo observar o impacto da arquitetura sobre o comportamento previsto pelas leis estudadas.
>
> **Configuração A (Turing).** CPU AMD Ryzen 7 4800H (8 núcleos físicos, 16 threads, 2,9 GHz base), 16 GB DDR4-3200, GPU NVIDIA GeForce GTX 1650 (896 CUDA cores, 4 GB GDDR6, PCIe 3.0 ×16). Sistema operacional Windows 11.
>
> **Configuração B (Ampere).** *[preencher com a sua segunda máquina, ou remover se for só uma]* — CPU `___`, RAM `___`, GPU NVIDIA RTX 30xx (`___` CUDA cores), Linux Ubuntu 22.04 / WSL2.
>
> **Software.** Compilador GCC 13.x com OpenMP 5.0 para o módulo CPU; NVIDIA CUDA Toolkit 12.x (NVCC) para o módulo GPU. As *flags* de compilação serão `-O3 -march=native -fopenmp` no GCC e `-arch=sm_75` (Turing) ou `-arch=sm_86` (Ampere) no NVCC, garantindo que cada binário utilize as instruções nativas da arquitetura-alvo. **As mesmas flags de otimização serão aplicadas à versão serial**, evitando que o ganho do paralelo seja artificialmente inflado por um *baseline* sub-ótimo.

**Por que isto é melhor que "texto genérico":**
- Cita compute capability — banca técnica reconhece como rigor.
- Justifica a escolha das duas máquinas (gerações diferentes = teste mais robusto).
- A última frase sobre flags do baseline é o tipo de detalhe que mostra honestidade experimental.

**LaTeX equivalente:**
```latex
\subsection{Materiais}

A pesquisa será conduzida em duas configurações de hardware, escolhidas por
representarem gerações distintas de GPU NVIDIA — Turing (compute capability
7.5) e Ampere (8.6) — permitindo observar o impacto da arquitetura sobre o
comportamento previsto pelas leis estudadas. A Tabela~\ref{tab:hw} resume os
componentes.

\begin{table}[h]
\centering
\caption{Configurações de hardware planejadas para o experimento.}
\label{tab:hw}
\begin{tabular}{lll}
\hline
Componente & Configuração A (Turing) & Configuração B (Ampere) \\ \hline
CPU        & Ryzen 7 4800H (8c/16t)  & \textit{[preencher]}    \\
RAM        & 16 GB DDR4-3200         & \textit{[preencher]}    \\
GPU        & GTX 1650 (896 CUDA)     & RTX 30xx                \\
SO         & Windows 11              & Ubuntu 22.04 / WSL2     \\ \hline
\end{tabular}
\end{table}
```

---

### 6.2 Estrutura do Benchmark

> O benchmark será implementado em C/CUDA puro, sem dependências externas, e organizado em módulos com responsabilidades isoladas:
>
> - **`generator`** — gerará dados sintéticos: amostras gaussianas pela transformada polar de Box-Muller, *timestamps* obedecendo a um processo de Poisson e um *checksum* XOR para detectar corrupção em transferências CPU↔GPU.
> - **`search` / `search_cuda`** — implementarão busca linear, binária e por *hash* nas variantes serial, OpenMP e CUDA.
> - **`sim_math` / `sim_math_cuda`** — implementarão Monte Carlo (estimativa de π por amostragem em $[-1,1]^2$) e Mandelbrot (iterações até divergência).
> - **`benchmark.c`** — atuará como orquestrador: lerá `hardware.cfg`, gerará os dados, executará todos os módulos sob cada volume e exportará os resultados em JSON.
>
> A estrutura `Event`, unidade básica de dado processada nas buscas, será projetada com **96 bytes alinhados** — três linhas de cache de 32 bytes — para casar com a unidade de coalescing de memória da GPU NVIDIA. Esse alinhamento permite que cada acesso pelo *warp* (32 *threads*) carregue exatamente três linhas contínuas, eliminando *misaligned loads* e maximizando a largura de banda efetiva. Trata-se de uma decisão de projeto motivada diretamente pela arquitetura, e não de uma escolha arbitrária — coerente com o objetivo de demonstrar a integração entre hardware e software.

**Por que isto é melhor:**
- Justifica a escolha do `96 B = 3×32` com argumento técnico (coalescing) — costura com objetivo geral.
- Liga cada módulo ao papel que exerce no experimento.

---

### 6.3 Procedimento Experimental (esta é a seção crítica da nota)

> O procedimento será estruturado em quatro etapas, projetadas para isolar o desempenho do algoritmo de ruídos do sistema operacional:
>
> **(i) Geração reprodutível dos dados.** Para cada volume $n \in \{10^3, 10^5, 10^6, 10^7\}$, o módulo `generator` produzirá um vetor de `Event` a partir de uma *seed* fixa. Isso garante que execuções diferentes processem exatamente a mesma entrada, eliminando a variação dos dados como fonte de erro experimental.
>
> **(ii) Cronometragem em alta resolução.** Os tempos serão medidos com `QueryPerformanceCounter` (Windows) e `clock_gettime(CLOCK_MONOTONIC)` (Linux) — ambos fornecem resolução de microssegundos lendo diretamente o *Time Stamp Counter* do processador, sem passar pelo relógio do sistema operacional. 
> 
> Não entendi bem essa parte 
> Para o módulo CUDA, será medido também o tempo isolado do *kernel* via `cudaEvent_t`, separando-o do custo de transferência via PCIe. A distinção entre $T_{\text{total}}$ (incluindo `cudaMemcpy`) e $T_{\text{kernel}}$ é central no estudo, pois o custo da transferência é justamente o termo que constitui a fração serial intransponível na lei de Amdahl quando aplicada ao caso GPU.
>
> **(iii) Warmup + N repetições + mediana.** Cada medição seguirá o protocolo: uma execução de *warmup* descartada (para preencher *cache* e estabilizar o *clock* da GPU), seguida de $N=9$ execuções cronometradas. Sobre as $N$ amostras será calculada a **mediana**, e não a média, porque o ruído típico do SO é fortemente assimétrico: uma única interrupção de antivírus ou *swap* de página pode adicionar dezenas de milissegundos a uma medição de poucos milissegundos, inflando a média sem mover a mediana.
>
> $$\tilde{T} = \mathrm{med}\{T_1, T_2, \ldots, T_9\}$$
>
> **(iv) Métricas derivadas.** Para cada combinação (algoritmo, modo, volume), serão calculados:
>
> $$S(n) = \frac{T_{\text{serial}}}{T_{\text{paralelo}}}, \qquad E(n) = \frac{S(n)}{n_{\text{cores}}}$$
>
> onde $S$ é o *speedup* observado e $E$ a eficiência por núcleo. No caso CUDA, dois *speedups* serão reportados: $S_{\text{total}}$ (incluindo memcpy) e $S_{\text{kernel}}$ (apenas execução na GPU). A diferença entre ambos quantificará o custo do barramento PCIe — termo central na análise pela lei de Amdahl.

**Por que isto é melhor:**
- Cada decisão é **justificada** (por que mediana, por que warmup, por que separar kernel de memcpy).
- A separação $S_{\text{total}}$ vs $S_{\text{kernel}}$ é exatamente o que vai testar sua **H3** na próxima etapa.
- "Seed fixa" é detalhe pequeno que mostra rigor.

---

### 6.4 Análise dos Dados

> Os arquivos JSON gerados pelo orquestrador serão consumidos por um *dashboard* HTML interativo construído sobre Chart.js, contendo três painéis: *speedup* por volume, *ranking* por algoritmo e comparação cruzada entre as duas configurações de hardware.
>
> A análise central confrontará as curvas observadas com os modelos teóricos. Para cada algoritmo, a fração paralelizável $p$ será estimada por ajuste não-linear da curva de Amdahl
>
> $$S_{\text{Amdahl}}(n) = \frac{1}{(1-p) + p/n}$$
>
> aos pontos $(n_{\text{cores}}, S_{\text{obs}})$. O valor estimado $\hat{p}$ funcionará como medida indireta da *fração serial efetiva* daquele algoritmo na arquitetura testada — incluindo, no caso CUDA, o custo da transferência via PCIe.
>
> Em paralelo, a lei de Gustafson será aplicada na vertente *weak scaling*: ao crescer o volume proporcionalmente ao número de processadores, espera-se que a fração serial se torne desprezível. O afastamento entre o *speedup* observado e o limite de Gustafson permitirá classificar cada algoritmo como *compute bound* (próximo ao limite) ou *memory bound* (afastado, dominado pela largura de banda).

**Por que isto é melhor:**
- "Estimar $\hat{p}$ por ajuste" é o pulo do gato: você não só compara com Amdahl, **vai medir a fração paralela do seu próprio código**. Banca adora.
- Distinguir *compute bound* / *memory bound* fecha o objetivo geral.

---

## 2. Resultados Esperados (Seção 7) — versão refinada

Mantenha o formato de proposta. A versão abaixo refina seus 3 bullets atuais e remove o "texto genérico":

> Os resultados esperados, embasados na fundamentação teórica e nas hipóteses formuladas, são:
>
> - **Algoritmos compute bound (Monte Carlo e Mandelbrot).** Espera-se *speedup* GPU/serial entre **50× e 500×** para volumes a partir de $10^6$ amostras, com a curva observada se aproximando assintoticamente do limite de Gustafson à medida que o volume cresce. A fração paralela estimada deverá satisfazer $\hat{p} > 0{,}99$, evidenciando que esses algoritmos são quase puramente paralelos. Esse comportamento corroboraria a **H3**.
>
> - **Busca linear em volumes grandes ($n \geq 10^6$).** Prevê-se *speedup* GPU/serial entre **5× e 50×**. Com volumes desta ordem, o custo de `cudaMemcpy` representa fração pequena do tempo de varredura, e a curva tenderá ao limite de Amdahl com $\hat{p} \approx 0{,}9$ — corroborando a porção da **H1** que prevê inversão de regime acima de certo volume.
>
> - **Busca binária e *hash* em volumes pequenos ($n \leq 10^5$).** Espera-se que a CPU paralela (OpenMP) supere a GPU. A latência fixa do PCIe (≈ 10–20 µs por transferência) constituirá fração serial não-amortizável, fazendo a curva de Amdahl saturar em $S < 5$ no caso CUDA — confirmando o teto previsto pela lei. Para a busca por *hash*, espera-se observar a degradação prevista pela **H2** quando a taxa de colisões cresce com o volume, transformando a resolução de colisão na fração serial dominante.
>
> - **Síntese e contribuição.** O conjunto dessas medições deverá fornecer um mapa empírico que oriente a escolha hardware/algoritmo conforme o tipo de carga: GPU para problemas *compute bound* sobre grandes volumes, CPU paralela para cargas *memory bound* ou de volume modesto, CPU serial para volumes que cabem na *cache* L1/L2. Mais do que comparar tempos, o trabalho proporá uma leitura prática das leis de Amdahl e Gustafson como ferramentas de decisão arquitetural — não apenas curvas teóricas.

**Por que isto é melhor:**
- Cada bullet **amarra explicitamente em uma hipótese** (H1/H2/H3) — fecha o ciclo do artigo.
- Faixas numéricas (50×–500×, 5×–50×) dão concretude sem comprometer com valores que você ainda não mediu.
- O bullet de síntese substitui o "texto genérico" por uma promessa de contribuição clara.

---

## 3. O que **não** colocar no artigo nesta etapa

- "O benchmark já foi implementado e validado" — quebra o gênero proposta.
- Tabelas/gráficos com números medidos do protótipo.
- Captura de tela do dashboard rodando.
- "Os resultados confirmaram H1..." — você ainda não tem resultados *do experimento controlado*.

O protótipo serve para você **escrever uma metodologia mais concreta** (porque sabe exatamente o que vai medir e como) — mas a redação fica em tempo futuro. É honesto: o experimento controlado, com as duas configurações e o protocolo definitivo, ainda será conduzido.

---

## 4. Erros típicos que a banca pega (mesmo numa proposta)

- **Verbos misturando passado e futuro** — releia tudo procurando "foi/foram" e converta para "será/serão".
- **Speedup sem dizer contra que serial.** Comparar CUDA com OpenMP de 16 threads dá número diferente de comparar com serial single-thread. Defina o *baseline*.
- **Não citar versão de driver/CUDA.** "CUDA 12.x" é vago — coloque `12.3` ou o que for.
- **Esquecer flags iguais no baseline serial.** Já mencionado em 6.1, mas vale repetir: o serial precisa rodar com `-O3 -march=native` igual ao OpenMP.
- **Hipóteses não amarradas aos resultados esperados.** Cada H1/H2/H3 deve aparecer explicitamente nos bullets do esperado.

---

## 5. Próximos passos sugeridos

1. Reescrever 6.1–6.4 com suas palavras a partir dos exemplos acima, em **tempo futuro**.
2. Refinar a Seção 7 com os bullets que amarram em H1/H2/H3.
3. Tratar separadamente a Seção 5 (Fundamentação) — outros "texto genérico" estão lá. Quando for atacar, peça material específico.
4. Substituir as 6 referências `SOBRENOME, A` por reais (últimos 5 anos, ≥90% estrangeiras, conforme a regra que você anotou em 5).
