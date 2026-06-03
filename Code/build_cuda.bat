@echo off
REM ============================================================
REM  build_cuda.bat  —  Duplo clique para compilar e empacotar
REM
REM  Gera duas pastas prontas para distribuicao:
REM
REM    dist_com_cuda\   ->  PCs com GPU NVIDIA (driver atualizado)
REM    dist_sem_cuda\   ->  Qualquer Windows 10/11
REM
REM  Requer: CUDA Toolkit instalado + Visual Studio 2019/2022
REM ============================================================

echo.
echo === Compilando e empacotando benchmark ===
echo.

powershell -ExecutionPolicy Bypass -Command ^
  "$base = '%CD%';" ^
  "$cl = (Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter 'cl.exe' -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match 'Hostx64\\x64' } | Select-Object -First 1 -ExpandProperty DirectoryName);" ^
  "if (-not $cl) { Write-Host 'ERRO: cl.exe nao encontrado. Instale o Visual Studio.'; Read-Host; exit 1 };" ^
  "Write-Host ('cl.exe: ' + $cl);" ^
  "Set-Location $base;" ^
  "& 'nvcc.exe' -O2 -arch=sm_75 -ccbin $cl -Xcompiler '/O2,/openmp,/W3' -DHAS_CUDA benchmark.c generator.c search.c sim_math.c search_cuda.cu sim_math_cuda.cu -o benchmark_cuda.exe 2>&1 | Write-Host;" ^
  "if ($LASTEXITCODE -ne 0) { Write-Host 'ERRO na compilacao CUDA.'; Read-Host; exit 1 };" ^
  "Write-Host 'OK: benchmark_cuda.exe gerado.';" ^
  "$vcomp = (Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter 'vcomp140.dll' -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match 'x64.Microsoft.VC' } | Select-Object -First 1 -ExpandProperty FullName);" ^
  "if (-not $vcomp) { $vcomp = 'C:\Windows\System32\vcomp140.dll' };" ^
  "$d1 = Join-Path $base 'dist_com_cuda';" ^
  "New-Item -ItemType Directory -Force -Path $d1 | Out-Null;" ^
  "Copy-Item 'benchmark_cuda.exe' (Join-Path $d1 'benchmark_cuda.exe') -Force;" ^
  "Copy-Item 'hardware.cfg'       (Join-Path $d1 'hardware.cfg')       -Force;" ^
  "Copy-Item $vcomp               (Join-Path $d1 'vcomp140.dll')       -Force;" ^
  "Write-Host '';" ^
  "Write-Host 'dist_com_cuda\ (GPU NVIDIA + driver atualizado):';" ^
  "Get-ChildItem $d1 | ForEach-Object { Write-Host ('  ' + $_.Name + ' - ' + $_.Length + ' bytes') };" ^
  "$d2 = Join-Path $base 'dist_sem_cuda';" ^
  "New-Item -ItemType Directory -Force -Path $d2 | Out-Null;" ^
  "Copy-Item 'benchmark_dist.exe' (Join-Path $d2 'benchmark.exe') -Force;" ^
  "Copy-Item 'hardware.cfg'       (Join-Path $d2 'hardware.cfg')  -Force;" ^
  "Write-Host '';" ^
  "Write-Host 'dist_sem_cuda\ (qualquer Windows 10/11):';" ^
  "Get-ChildItem $d2 | ForEach-Object { Write-Host ('  ' + $_.Name + ' - ' + $_.Length + ' bytes') };" ^
  "Write-Host '';" ^
  "Write-Host '=== Pronto! Distribua a pasta correspondente ===';"

echo.
pause
