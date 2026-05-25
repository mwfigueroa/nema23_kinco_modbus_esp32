function Import-EspIdfEnvironment {
    [CmdletBinding()]
    param()

    if (Get-Command idf.py -ErrorAction SilentlyContinue) {
        return
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

            $idfPythonDir = Join-Path $env:IDF_TOOLS_PATH "tools\idf-python\3.11.2"
            if (Test-Path (Join-Path $idfPythonDir "python.exe")) {
                $env:PATH = "$idfPythonDir;$env:PATH"
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
