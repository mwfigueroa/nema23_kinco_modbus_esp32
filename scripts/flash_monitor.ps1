# Flash & Monitor script para NEMA23 LILYGO Gateway
# Uso: .\flash_monitor.ps1 [-Port COM3] [-BuildDir build_marti] [-NoReset] [-NoMonitor]
param(
    [string]$Port = "",
    [string]$BuildDir = "build_marti",
    [switch]$NoReset,
    [switch]$NoMonitor
)

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ProjectDir

Write-Host "============================================" -ForegroundColor Cyan
Write-Host " NEMA23 LILYGO Gateway - Flash & Monitor" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan

. (Join-Path $PSScriptRoot "ensure_idf.ps1")
Import-EspIdfEnvironment

Set-Location $ProjectDir

if ($Port -eq "") {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($ports.Count -gt 0) {
        $Port = $ports[0]
        Write-Host "[*] Puerto detectado: $Port" -ForegroundColor Yellow
    } else {
        Write-Host "[!] No se detecto ningun puerto serie." -ForegroundColor Red
        Write-Host "    Especifica con: .\flash_monitor.ps1 -Port COM3" -ForegroundColor Red
        exit 1
    }
}

Write-Host ""

if ($NoReset) {
    Write-Host "============================================" -ForegroundColor Magenta
    Write-Host " MODO MANUAL: Cuando aparezca 'Connecting...'" -ForegroundColor Magenta
    Write-Host " 1. MANTEN presionado el boton BOOT (IO0)" -ForegroundColor Yellow
    Write-Host " 2. Presiona y suelta EN (RST)" -ForegroundColor Yellow
    Write-Host " 3. Suelta BOOT" -ForegroundColor Yellow
    Write-Host "============================================" -ForegroundColor Magenta
    Write-Host ""
    Write-Host "[*] Flasheando en $Port (modo manual)..." -ForegroundColor Yellow
    idf.py -B $BuildDir -p $Port flash --no-reset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    Write-Host "[*] Flasheando en $Port..." -ForegroundColor Yellow
    idf.py -B $BuildDir -p $Port flash
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if (-not $NoMonitor) {
    Write-Host "[*] Iniciando monitor serie..." -ForegroundColor Yellow
    idf.py -B $BuildDir -p $Port monitor
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    Write-Host "[OK] Flash completado. Usa 'idf.py -B $BuildDir -p $Port monitor' para ver la salida." -ForegroundColor Green
}
