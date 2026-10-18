param(
    [string]$EngineRoot = 'D:\APP\APP_Game\Unreal Engine\UE_5.7',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'VFX.uproject'
$python = Join-Path $projectRoot '.venv\Scripts\python.exe'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$ubt = Join-Path $EngineRoot 'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll'
$dotnet = Get-ChildItem (Join-Path $EngineRoot 'Engine\Binaries\ThirdParty\DotNet') -Filter dotnet.exe -Recurse |
    Where-Object { $_.FullName -like '*win-x64*' } | Select-Object -First 1 -ExpandProperty FullName
foreach ($file in @($projectFile, $python, $editor, $ubt, $dotnet)) {
    if (!$file -or !(Test-Path -LiteralPath $file)) { throw "Missing prerequisite: $file" }
}
$server = $null
$oldProvider = $env:COSMO_AI_PROVIDER
Push-Location $projectRoot
try {
    if (!$SkipBuild) {
        & $dotnet $ubt VFXEditor Win64 Development "-Project=$projectFile" -WaitMutex -NoHotReloadFromIDE -EnablePlugin=CodexBlueprintTools
        if ($LASTEXITCODE -ne 0) { throw 'Unreal build failed.' }
    }
    & $python -m pytest External/CosmoSystem/tests dubhack_gestures -q "--basetemp=Saved/TestTemp/Verify_$([Guid]::NewGuid().ToString('N'))" --tb=short
    if ($LASTEXITCODE -ne 0) { throw 'Python tests failed.' }
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 18765)
    try { $listener.Start() } finally { $listener.Stop() }
    $env:COSMO_AI_PROVIDER = 'local'
    $server = Start-Process -FilePath $python -ArgumentList @('-u','-m','companion','serve','--host','127.0.0.1','--port','18765') -WorkingDirectory (Join-Path $projectRoot 'External\CosmoSystem') -WindowStyle Hidden -PassThru
    $ready = $false
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        try {
            $response = Invoke-RestMethod 'http://127.0.0.1:18765/health' -TimeoutSec 1
            if ($response.ok) { $ready = $true; break }
        } catch { Start-Sleep -Milliseconds 100 }
    }
    if (!$ready) { throw 'Temporary COSMO service failed to start.' }
    $logPath = Join-Path $projectRoot 'Saved\Logs\CharacterAI_RuntimeTest.log'
    & $editor $projectFile -run=CodexCompleteCharacter -RuntimeTest -EnablePlugins=CodexBlueprintTools -unattended -nop4 -nosplash -nullrhi "-abslog=$logPath"
    if ($LASTEXITCODE -ne 0) { throw "Unreal integration tests failed. See $logPath" }
    Select-String -LiteralPath $logPath -Pattern 'CHARACTER_TEST_RESULT'
} finally {
    if ($server) {
        & taskkill.exe /PID $server.Id /T /F 2>$null | Out-Null
    }
    $env:COSMO_AI_PROVIDER = $oldProvider
    Pop-Location
}
