# Build script para NEMA23 LILYGO Gateway
# Uso: .\build.ps1 [-Clean] [-BuildDir build_marti]
param(
    [switch]$Clean,
    [string]$BuildDir = "build_marti"
)

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ProjectDir

Write-Host "============================================" -ForegroundColor Cyan
Write-Host " NEMA23 LILYGO Gateway - Build" -ForegroundColor Cyan
Write-Host " Target: ESP32 (T-CAN485)" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan

. (Join-Path $PSScriptRoot "ensure_idf.ps1")
Import-EspIdfEnvironment

Set-Location $ProjectDir

if ($Clean) {
    Write-Host "[*] Limpiando build anterior..." -ForegroundColor Yellow
    idf.py -B $BuildDir fullclean
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "[*] Configurando target ESP32..." -ForegroundColor Yellow
idf.py -B $BuildDir set-target esp32
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[*] Compilando..." -ForegroundColor Yellow
idf.py -B $BuildDir build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "============================================" -ForegroundColor Green
Write-Host " Build completado! .bin en $BuildDir/" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Green
