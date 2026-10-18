param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $projectRoot 'Plugins\CosmoBridge\Resources\Runtime\Win64'
# Spaces and Unicode expose accidental absolute-path assumptions.
$testRoot = Join-Path $projectRoot "Saved\RuntimeRelocation\独立 游戏 $([Guid]::NewGuid().ToString('N'))"
$testRuntime = Join-Path $testRoot 'Runtime'
$logs = Join-Path $testRoot 'Logs'
$report = Join-Path $testRoot 'verification.log'
$oldPath = $env:PATH
$oldHome = $env:PYTHONHOME
$oldPythonPath = $env:PYTHONPATH
try {
    New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $testRuntime -Recurse
    $python = Join-Path $testRuntime 'python\python.exe'
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:PYTHONHOME = Join-Path $testRoot 'no-system-python'
    $env:PYTHONPATH = Join-Path $testRoot 'no-development-packages'
    Push-Location $testRoot
    try {
        & $python -B (Join-Path $testRuntime 'app\runtime_check.py') 2>&1 | Tee-Object -FilePath $report
        if ($LASTEXITCODE -ne 0) { throw 'Relocated runtime validation failed.' }
        & $python -B (Join-Path $PSScriptRoot 'test_cosmo_lifecycle_smoke.py') --runtime $testRuntime --log-dir $logs 2>&1 | Tee-Object -FilePath $report -Append
        if ($LASTEXITCODE -ne 0) { throw 'Relocated service lifecycle failed.' }
        Write-Output "Runtime relocation verification passed. Evidence: $report"
    } finally { Pop-Location }
} finally {
    $env:PATH = $oldPath
    $env:PYTHONHOME = $oldHome
    $env:PYTHONPATH = $oldPythonPath
}
