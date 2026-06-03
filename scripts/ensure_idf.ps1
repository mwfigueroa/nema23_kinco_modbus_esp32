function Import-EspIdfEnvironment {
    [CmdletBinding()]
    param()

    if (Get-Command idf.py -ErrorAction SilentlyContinue) {
        return
    }

    # Fix Python: Windows Store stub bloquea python.exe real
    $realPythonPaths = @(
        "C:\Users\$env:USERNAME\AppData\Local\Programs\Python\Python312",
        "C:\Python312",
        "C:\Python311",
        "C:\Python3"
    )
    foreach ($pyPath in $realPythonPaths) {
        if (Test-Path (Join-Path $pyPath "python.exe")) {
            $env:PATH = "$pyPath;$pyPath\Scripts;$env:PATH"
            Write-Host "[*] Python fijado: $pyPath" -ForegroundColor DarkGray
            break
        }
    }

    $candidatePaths = @()

    if ($env:IDF_PATH) {
        $candidatePaths += $env:IDF_PATH
    }

    $candidatePaths += @(
        (Join-Path $env:USERPROFILE "esp\v5.5.1\esp-idf"),
        (Join-Path $env:USERPROFILE "esp\esp-idf"),
        "C:\Espressif\frameworks\esp-idf-v5.5.1",
        "C:\Espressif\frameworks\esp-idf"
    )

    foreach ($idfPath in $candidatePaths | Select-Object -Unique) {
        $exportScript = Join-Path $idfPath "export.ps1"
        if (Test-Path $exportScript) {
            $env:IDF_PATH = $idfPath
            if (-not $env:IDF_TOOLS_PATH) {
                $env:IDF_TOOLS_PATH = Join-Path $env:USERPROFILE ".espressif"
            }

            Write-Host "[*] Cargando ESP-IDF desde $exportScript" -ForegroundColor Yellow

            $prevPref = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            try {
                . $exportScript
            } finally {
                $ErrorActionPreference = $prevPref
            }

            return
        }
    }

    Write-Host "[!] No se encontró ESP-IDF." -ForegroundColor Red
    Write-Host "    Define IDF_PATH o instala ESP-IDF desde https://docs.espressif.com/projects/esp-idf/" -ForegroundColor Red
    exit 1
}
