# ============================================================
#  CMakeLists.txt — parallel-laws-benchmark
#  Aposenta o build_cuda.bat e o Makefile: UM arquivo só, que
#  compila no Linux e no Windows e gera UM executável capaz de
#  rodar em várias GPUs (o "fat binary" que conversamos).
#
#  Como usar no Linux:
#     mkdir build && cd build
#     cmake .. -DCMAKE_BUILD_TYPE=Release
#     cmake --build . -j
#  Sai o ./benchmark_cuda. Rode num diretório que tenha o
#  hardware.cfg do lado (o programa lê ele do diretório atual).
# ============================================================

# Versão mínima do CMake.
#   - 3.18 já entende o "XX-real / XX-virtual" (ver abaixo).
#   - Mas Blackwell (sm_120, RTX 50xx) só é reconhecido por CMake
#     novo. Se ele reclamar de "120", seu CMake está velho:
#     atualize (3.28+) OU use o bloco -gencode comentado lá embaixo.
cmake_minimum_required(VERSION 3.24)

# Nome do projeto + linguagens.
#   C    = os arquivos .c  (benchmark, generator, search, sim_math)
#   CUDA = os arquivos .cu (os kernels da GPU)
# Declarar CUDA aqui faz o CMake achar o nvcc sozinho E já linkar
# o runtime do CUDA (o cudart) sem você pedir.
project(parallel_laws_benchmark LANGUAGES C CUDA)

# O projeto usa C99 (for(int i=...), // comentários, etc.)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ------------------------------------------------------------
#  Quais GPUs o executável carrega dentro de si
# ------------------------------------------------------------
# Cada "XX-real"   = a TRADUÇÃO FINAL daquela placa (roda na hora).
# O   "120-virtual"= a versão GENÉRICA de reserva (PTX): qualquer
#                    GPU futura, ou sem tradução final aqui, faz a
#                    tradução-na-hora a partir dela.
#
#   sm_75  = GTX 16xx / RTX 20xx   (Turing)   <- sua GTX 1650
#   sm_86  = RTX 30xx              (Ampere)   <- sua RTX 3060/3080
#   sm_89  = RTX 40xx              (Ada)
#   sm_120 = RTX 50xx             (Blackwell)
#
# ⚠️ Série 10 (Pascal/sm_61) NÃO entra: o CUDA 13 removeu Pascal.
#    O piso possível no seu toolkit é Turing (sm_75).
set(CMAKE_CUDA_ARCHITECTURES 75-real 86-real 89-real 120-real 120-virtual)

# --- Alternativa, se o CUDA_ARCHITECTURES acima der problema ---
# (CMake velho que não conhece "120"). Descomente as 2 linhas e
# comente o set() de cima. É o mesmo "-gencode" do guia [T16]:
# set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} \
#   -gencode arch=compute_75,code=sm_75 \
#   -gencode arch=compute_86,code=sm_86 \
#   -gencode arch=compute_89,code=sm_89 \
#   -gencode arch=compute_120,code=sm_120 \
#   -gencode arch=compute_120,code=compute_120")  # <- o PTX de reserva

# ------------------------------------------------------------
#  OpenMP (o paralelismo de CPU, os #pragma omp do benchmark.c)
# ------------------------------------------------------------
# REQUIRED = se não achar, para com erro claro em vez de gerar
# um binário quebrado sem paralelismo.
find_package(OpenMP REQUIRED)

# ------------------------------------------------------------
#  O executável: lista TODOS os fontes, .c e .cu na mesma chamada
# ------------------------------------------------------------
add_executable(benchmark_cuda
    benchmark.c
    generator.c
    search.c
    sim_math.c
    search_cuda.cu
    sim_math_cuda.cu
)

# Liga o "interruptor" HAS_CUDA (igual ao -DHAS_CUDA do .bat):
# ativa os blocos #ifdef HAS_CUDA que metem os kernels de verdade
# dentro do benchmark.c.
target_compile_definitions(benchmark_cuda PRIVATE HAS_CUDA)

# ------------------------------------------------------------
#  Bibliotecas que o programa precisa no fim (linkagem)
# ------------------------------------------------------------
#   OpenMP::OpenMP_C = runtime do OpenMP (no Windows era o vcomp140.dll)
#   O cudart (runtime do CUDA) o CMake linka sozinho, por causa do
#   "CUDA" lá no project().
target_link_libraries(benchmark_cuda PRIVATE OpenMP::OpenMP_C)

# A libm (sqrt, log, sin... que o gerador usa) só existe como biblioteca
# SEPARADA no Linux/Unix (o "-lm"). No Windows ela já vem dentro da
# biblioteca padrão do C, então não se linka nada — por isso o "m" só
# entra no Unix. (Linkar "m" no Windows dava o LNK1181: m.lib não existe.)
if(UNIX)
    target_link_libraries(benchmark_cuda PRIVATE m)
endif()

# ------------------------------------------------------------
#  Otimização — deixe o CMake cuidar, NÃO force -O3 aqui
# ------------------------------------------------------------
# A otimização é escolhida pela CONFIGURAÇÃO do build:
#    Release -> otimizado      (host /O2, kernels otimizados)
#    Debug   -> sem otimização (/Od) + checagens de runtime (/RTC1)
# Forçar -O3 na mão brigava com o modo Debug do MSVC: /RTC1 + /O2
# são incompatíveis (foi o erro D8016 que você viu). Por isso saiu.
#
# ⚠️ Um BENCHMARK tem que ser compilado em RELEASE. Medir em Debug
#    dá números errados (otimização desligada). Como pedir Release:
#      Windows (Visual Studio):  cmake --build . --config Release
#      Linux  (Makefile/Ninja):  já vem do  -DCMAKE_BUILD_TYPE=Release

# ------------------------------------------------------------
#  (OPCIONAL) cudart embutido no executável — leia antes de ativar
# ------------------------------------------------------------
# Lembra do problema do .exe do Windows que ia faltar o cudart numa
# máquina sem Toolkit? Esta linha resolve no Linux: embute o runtime
# do CUDA dentro do binário. Aí ele roda em qualquer máquina com
# driver NVIDIA, SEM precisar do cudart instalado.
# Custo: binário maior. Pra DISTRIBUIR, vale a pena. Pra só rodar
# no seu notebook (que tem o Toolkit), pode deixar desligado.
# set_target_properties(benchmark_cuda PROPERTIES CUDA_RUNTIME_LIBRARY Static)
