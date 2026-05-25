[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "ensure_idf.ps1")
Import-EspIdfEnvironment

function Test-Command {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        Write-Host "[FALTA] $Name" -ForegroundColor Red
        return $false
    }

    Write-Host "[OK]    $Name -> $($command.Source)" -ForegroundColor Green
    return $true
}

Write-Host "Verificando entorno ESP-IDF..." -ForegroundColor Cyan

$allOk = $true

if (-not $env:IDF_PATH) {
    Write-Host "[FALTA] IDF_PATH no está definido. Ejecuta export.ps1 antes de usar el proyecto." -ForegroundColor Red
    $allOk = $false
} else {
    Write-Host "[OK]    IDF_PATH -> $env:IDF_PATH" -ForegroundColor Green
}

$allOk = (Test-Command "idf.py") -and $allOk
$allOk = (Test-Command "python") -and $allOk
$allOk = (Test-Command "cmake") -and $allOk
$allOk = (Test-Command "ninja") -and $allOk
$allOk = (Test-Command "xtensa-esp32-elf-gcc") -and $allOk

if (Test-Path ".\sdkconfig.defaults") {
    Write-Host "[OK]    sdkconfig.defaults presente" -ForegroundColor Green
} else {
    Write-Host "[FALTA] sdkconfig.defaults" -ForegroundColor Red
    $allOk = $false
}

if ($allOk) {
    Write-Host "`nEntorno listo para compilar." -ForegroundColor Green
    exit 0
}

Write-Host "`nEl entorno todavía no está completo." -ForegroundColor Yellow
exit 1
