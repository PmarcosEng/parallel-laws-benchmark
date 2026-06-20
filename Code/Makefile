CC      = gcc
# -Wno-missing-field-initializers: BenchmarkResult tem campo opcional t_kernel_s
# (só CUDA) que fica zero por init parcial intencional nos modos CPU.
CFLAGS  = -std=c99 -Wall -Wextra -Wno-missing-field-initializers -O2
LDFLAGS = -lm
OPENMP  = -fopenmp
SRC     = generator.c search.c sim_math.c config.c timing.c report.c benchmark.c
HDR     = event.h search.h generator.h sim_math.h config.h timing.h report.h

# ── Desenvolvimento (linkagem dinâmica, binário menor) ──────────────────────
all: benchmark

benchmark: $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(OPENMP) -o benchmark $(SRC) $(LDFLAGS)
	@echo ">>> benchmark compilado (dinamico + OpenMP)."

# Sem OpenMP — fallback para Windows sem suporte
benchmark_noomp: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o benchmark $(SRC) $(LDFLAGS)
	@echo ">>> benchmark compilado (sem OpenMP)."

# ── Distribuição: binário estático, sem dependências externas ───────────────
# O executável resultante roda em qualquer Windows sem instalar MinGW/libgomp.
# Uso: make dist   →  gera benchmark_dist.exe  (ou benchmark_dist no Linux)
dist: $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(OPENMP) -static -s \
		-o benchmark_dist \
		$(SRC) \
		$(LDFLAGS) -lgomp -lpthread
	@echo ">>> benchmark_dist gerado (estatico, redistribuivel sem dependencias)."

# Distribuição sem OpenMP (portátil máximo — 1 arquivo, 0 DLLs necessárias)
dist_noomp: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -static -s \
		-o benchmark_dist_noomp \
		$(SRC) \
		$(LDFLAGS)
	@echo ">>> benchmark_dist_noomp gerado (estatico, sem OpenMP)."


# ── CUDA (RTX/GTX com CUDA Toolkit instalado) ───────────────────────────────
# Requer: nvcc no PATH (CUDA Toolkit) + gcc + libgomp
# sm_75 = Turing (GTX 1650, RTX 2xxx/3xxx). Ajuste para sua GPU se necessário.
NVCC       = nvcc
ARCH       = -arch=sm_75
CUDA_FLAGS = $(ARCH) -O3 -Xcompiler "-fopenmp" -DHAS_CUDA
CUDA_SRC   = search_cuda.cu sim_math_cuda.cu

benchmark_cuda: $(SRC) $(CUDA_SRC) $(HDR) search_cuda.h sim_math_cuda.h
	$(NVCC) $(CUDA_FLAGS) -o benchmark_cuda $(SRC) $(CUDA_SRC) -lm
	@echo ">>> benchmark_cuda compilado (CUDA real + OpenMP)."

clean:
	rm -f benchmark benchmark_noomp benchmark_dist benchmark_dist_noomp benchmark_cuda *.obj *.o

.PHONY: all dist dist_noomp benchmark_noomp benchmark_cuda clean
