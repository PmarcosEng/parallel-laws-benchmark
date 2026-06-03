---
tags:
  - estudo
  - guia
  - benchmark
  - cuda
status: ativo
data_criacao: 2026-05-02
data_atualizacao: 2026-05-02
gerado_por: Claude
tipo: guia
---

# Guia de Estudo — Projeto de Artigo Benchmark_CUDA

> [!info] Para quem é este guia
> Você vai entregar um **projeto de artigo** (proposta) na **terça 05/05/2026**. Hoje é **sábado 02/05** — você tem **3 dias úteis**.
> O código da simulação foi gerado com IA, mas a apresentação oral é sua. Este guia te leva do zero ao "consigo defender" no menor tempo possível.

---

## 1. O que você precisa saber sobre projeto de artigo

Um **projeto** é uma proposta. Tudo no futuro:
- "Pretendo investigar..."
- "Espero encontrar..."
- "A metodologia será..."

**Você não precisa de resultados ainda.** Resultados reais ficam pro artigo final (próxima etapa). Aqui basta uma proposta clara, uma pergunta de pesquisa boa e uma metodologia coerente.

---

## 2. Roteiro de 3 dias

### 🟢 Dia 1 — Sábado 02/05 (hoje)

**Objetivo:** entender a arquitetura do projeto e preencher a primeira metade do LaTeX.

**Ler nesta ordem:**
1. [[01 - Visao Geral do Projeto]] — o que o projeto faz, os 4 modos de execução
2. [[02 - Estrutura de Dados e Geracao]] — Event de 96 bytes, Box-Muller, Poisson
3. [[03 - Paralelismo na CPU (OpenMP)]] — como dividir trabalho entre núcleos
4. [[04 - Sistema de Build]] — Makefile e build_cuda.bat
5. [[05 - Fundamentos de GPU]] — warp, block, grid, SIMT

**Pra cada arquivo, escreva 1 frase de resumo no caderno** com suas palavras. Se não conseguir, releia.

**Preencher no LaTeX:**
- Introdução
- Problema de Pesquisa
- Hipóteses
- Objetivo Geral + Específicos

### 🟡 Dia 2 — Domingo 03/05

**Objetivo:** dominar algoritmos, medição e leis. Fechar o texto.

**Ler:**
6. [[06 - Algoritmos de Busca na GPU]] — linear, binária, hash em CUDA
7. [[07 - Algoritmos Matematicos]] — Monte Carlo (π) e Mandelbrot
8. [[08 - Compilacao e Arquitetura CUDA]] — JIT, PTX, NVCC
9. [[09 - Medicao e Resultados]] — **MAIS IMPORTANTE** — QPC, mediana, outlier, Amdahl, Gustafson
10. [[10 - Dashboard e Configuracao]] — hardware.cfg e visualização

**Preencher no LaTeX:**
- Fundamentação Teórica (use o arquivo 09 + 05)
- Metodologia (use 02, 03, 04, 06, 07)
- Resultados Esperados (use 01 — seção "Resultado esperado")

**Buscar referências:** abrir Google Scholar com filtro "Desde 2022". Termos:
- `Amdahl's law GPU CUDA scalability`
- `OpenMP CUDA performance comparison benchmark`
- `Monte Carlo simulation GPU acceleration`
- `Mandelbrot parallel computing performance`
- `Gustafson scaling law modern processors`

Pegue **6 papers**. Leia só o **abstract + conclusão** de cada. Anote autor, título, journal, ano. Mínimo 90% estrangeiros (5 de 6).

### 🔵 Dia 3 — Segunda 04/05

**Objetivo:** polir e ensaiar.

- Compilar o PDF no Overleaf, ler do início ao fim como leitor estranho.
- Ajustar cronograma do projeto (datas reais das próximas etapas).
- Responder por escrito as **12 perguntas de autoavaliação** (seção 5 deste guia).
- Se travar em alguma, voltar no arquivo correspondente.

### ✅ Terça 05/05 — Entregar.

---

## 3. Os 3 Conceitos-Âncora (decore isto)

Se você só conseguir gravar 3 coisas, que sejam estas:

### 🔹 Os 4 modos de execução

| Modo | Onde roda | Vence quando |
|---|---|---|
| `serial` | CPU, 1 thread | volume pequeno, comparação base |
| `openmp` | CPU, N threads | dados irregulares, lógica complexa |
| `gpu_sim` | CPU simulando GPU | só pra validar matemática |
| `cuda` | GPU NVIDIA | volume grande, mesma conta repetida |

### 🔹 Compute Bound vs Memory Bound

- **Compute Bound** (puro cálculo): GPU domina por 50×–500× (ex: Monte Carlo, Mandelbrot).
- **Memory Bound** (espera memória): GPU pode perder pra CPU em volume pequeno por causa da latência PCIe (ex: busca binária pequena).
- **Lição:** transferir dados pra GPU custa caro. Só vale a pena se o cálculo justificar.

### 🔹 Amdahl vs Gustafson

| Lei | Pergunta que responde | Quando aparece nos seus dados |
|---|---|---|
| **Amdahl** | "Qual o speedup máximo se eu paralelizar?" | Curva **satura** com mais núcleos — fração serial é o teto |
| **Gustafson** | "E se eu usar mais núcleos pra resolver problema maior?" | Speedup **cresce quase linear** com volume — caso Monte Carlo/Mandelbrot |

Fórmulas:
$$S_{Amdahl}(n) = \frac{1}{(1-p) + \frac{p}{n}} \qquad S_{Gustafson}(n) = n - (1-p)(n-1)$$

---

## 4. Mapa: documentação → seção do artigo

| Seção do LaTeX | Arquivos pra puxar conteúdo |
|---|---|
| Introdução | 01 (motivação) |
| Problema de Pesquisa | 09 (leis) + 01 (modos) |
| Hipóteses | 01 ("Resultado esperado") + 09 |
| Objetivos | 01, 06, 07 |
| Fundamentação — Amdahl/Gustafson | 09 |
| Fundamentação — Arquitetura GPU | 05, 08 |
| Fundamentação — OpenMP | 03 |
| Metodologia — materiais/build | 04, 10 |
| Metodologia — algoritmos | 02, 06, 07 |
| Metodologia — medição | 09 |
| Resultados Esperados | 01 (final) + 09 |

---

## 5. Perguntas de Autoavaliação

Responda em voz alta, como se fosse o professor perguntando:

1. Em uma frase, qual é o objetivo do seu projeto?
2. Quais são os 4 modos de execução e qual hardware cada um usa?
3. Por que o struct `Event` tem exatamente 96 bytes?
4. O que é um *warp* em CUDA? Quantas threads tem?
5. Como você decide quantos blocos lançar pra processar `n` elementos?
6. Por que medir tempo com mediana e não com média?
7. O que é um *outlier* numa medição de benchmark?
8. Diferença entre *compute bound* e *memory bound* — dê um exemplo de cada nos seus algoritmos.
9. Quando a lei de Amdahl prevê melhor o resultado? E Gustafson?
10. Por que a GPU pode perder pra CPU em busca binária com poucos elementos?
11. O que o Monte Carlo simula no seu projeto e por que ele é o caso ideal pra GPU?
12. Como o `hardware.cfg` muda o experimento?

**Critério:** se travar em mais de 3, ainda não está pronto — volta na documentação.

---

## 6. O que você NÃO precisa decorar

- Sintaxe exata de CUDA (`<<<blocks, threads>>>`)
- Linhas específicas do código C
- Comandos do Makefile
- Detalhes do dashboard HTML

Saber **que existe** e **pra que serve** é suficiente.

---

## 7. Sobre o código ter sido gerado com IA

**No projeto (esta entrega):** não precisa declarar. Você está propondo metodologia, não relatando autoria de cada linha.

**No artigo final (próxima etapa):** declare em nota de rodapé ou agradecimentos:
> *"Parte do código-fonte foi gerada com auxílio de ferramentas de IA generativa, sob revisão e validação do autor."*

**O que te protege em qualquer apresentação:** entender o que o código faz. Quem digitou é irrelevante; quem entende, defende.

---

## 8. Checklist Pré-Entrega

- [ ] Li os 10 arquivos da documentação
- [ ] Respondi as 12 perguntas de autoavaliação sem travar
- [ ] PDF compila sem erro no Overleaf
- [ ] Capa com nome correto, instituição, data
- [ ] Pergunta de pesquisa **é literalmente uma pergunta** (com "?")
- [ ] Mínimo 2 hipóteses afirmativas
- [ ] Objetivos: 1 geral + ≥3 específicos
- [ ] Fundamentação só com referências de **2021–2026**
- [ ] **≥90% das referências são estrangeiras** (mínimo 5 de 6)
- [ ] Cronograma cobre as etapas até a entrega do artigo final
- [ ] Texto em terceira pessoa (sem "eu acho", "eu penso")
- [ ] Reli o PDF inteiro como se fosse o avaliador

---

⬅️ Voltar para: [[Workbench/Faculdade/Arquitetura_2°_Artigo/parallel-laws-benchmark/Benchmark_CUDA/00 - Indice do Projeto]]
