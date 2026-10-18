param([string]$EngineRoot = 'D:\APP\APP_Game\Unreal Engine\UE_5.7')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$python = Join-Path $projectRoot '.venv\Scripts\python.exe'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$fixture = $null
Push-Location $projectRoot
try {
    foreach ($port in @(18766,18767)) {
        $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $port)
        try { $listener.Start() } finally { $listener.Stop() }
    }
    $fixture = Start-Process -FilePath $python -ArgumentList @('-u','Scripts\realtime_av_fixture.py') -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput 'Saved\Logs\RealtimeAV_Fixture.log' -RedirectStandardError 'Saved\Logs\RealtimeAV_FixtureError.log'
    $ready = $false
    for ($attempt=0; $attempt -lt 60; $attempt++) {
        try {
            if ((Invoke-RestMethod 'http://127.0.0.1:18767/health' -TimeoutSec 1).camera_ready) { $ready=$true; break }
        } catch { Start-Sleep -Milliseconds 100 }
    }
    if (!$ready) { throw 'Offline AV fixture did not start.' }
    $log = Join-Path $projectRoot 'Saved\Logs\RealtimeAV_RuntimeTest.log'
    & $editor (Join-Path $projectRoot 'VFX.uproject') -run=CodexRealtimeAV -RealtimeAVTest -EnablePlugins=CodexBlueprintTools -unattended -nop4 -nosplash -nullrhi -nosound "-abslog=$log"
    if ($LASTEXITCODE -ne 0) { throw "Offline UE AV test failed. See $log" }
    Select-String -LiteralPath $log -Pattern 'REALTIME_AV_TEST_RESULT'
} finally {
    if ($fixture) { & taskkill.exe /PID $fixture.Id /T /F 2>$null | Out-Null }
    Pop-Location
}
