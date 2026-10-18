param(
    [string]$EngineRoot='D:\APP\APP_Game\Unreal Engine\UE_5.7',
    [switch]$Build,
    [switch]$RegenerateAssets,
    [switch]$Preview,
    [switch]$Bootstrap
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$project=Join-Path $root 'VFX.uproject'
$editor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$python=Join-Path $root '.venv\Scripts\python.exe'
$logs=Join-Path $root 'Saved\Logs'
if($Build) {
    $dotnet=Get-ChildItem (Join-Path $EngineRoot 'Engine\Binaries\ThirdParty\DotNet') -Filter dotnet.exe -Recurse | Where-Object {$_.FullName -like '*win-x64*'} | Select-Object -First 1 -ExpandProperty FullName
    & $dotnet (Join-Path $EngineRoot 'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll') UnrealEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE -EnablePlugins=CodexBlueprintTools
    if($LASTEXITCODE -ne 0) { throw 'UI build failed' }
}
if($RegenerateAssets) {
    & $editor $project -run=CodexUISetup -EnablePlugins=CodexBlueprintTools -unattended -nop4 -nosplash -nullrhi "-abslog=$logs\UI_Setup.log"
    if($LASTEXITCODE -ne 0) { throw 'UI asset generation failed' }
}
& $editor $project -run=CodexUISetup -VerifyAnchors -EnablePlugins=CodexBlueprintTools -unattended -nop4 -nosplash -nullrhi "-abslog=$logs\UI_Anchors.log"
if($LASTEXITCODE -ne 0 -or !(Select-String "$logs\UI_Anchors.log" -Pattern 'UI_ANCHOR_RESULT PASS' -Quiet)) { throw 'UI anchor verification failed' }
& $editor $project /Game/UI/Maps/Lvl_MainMenu -game -UISkipTutorialTest -RenderOffscreen -ForceRes -ResX=1920 -ResY=1080 -windowed -nosplash -nop4 -unattended "-abslog=$logs\UI_SkipTutorial.log"
if($LASTEXITCODE -ne 0) { throw 'Skip tutorial flow crashed' }
foreach($marker in @('UI_CHOICE_MODAL PASS','UI_CHOICE_FOCUS PASS','UI_SKIP_RESULT PASS')) {
    if(!(Select-String "$logs\UI_SkipTutorial.log" -Pattern $marker -Quiet)) { throw "Missing skip tutorial evidence: $marker" }
}
& $editor $project -run=CodexUISetup -VerifyGestures -EnablePlugins=CodexBlueprintTools -unattended -nop4 -nosplash -nullrhi "-abslog=$logs\UI_GestureMatcher.log"
if($LASTEXITCODE -ne 0 -or !(Select-String "$logs\UI_GestureMatcher.log" -Pattern 'UI_GESTURE_MATCHER_RESULT PASS' -Quiet)) { throw 'Gesture matcher verification failed' }
$fixture=Start-Process -FilePath $python -ArgumentList @('-u',"`"$PSScriptRoot\ui_voice_fixture.py`"") -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput "$logs\UI_Fixture.log" -RedirectStandardError "$logs\UI_Fixture_Error.log"
try {
    # Wait for actual readiness; a delay alone is not proof that the server is ready.
    $ready=$false
    for($attempt=0;$attempt -lt 50;$attempt++) {
        if($fixture.HasExited) { throw 'UI fixture exited; check UI_Fixture_Error.log and port 18766' }
        if((Test-Path "$logs\UI_Fixture.log") -and (Select-String "$logs\UI_Fixture.log" -Pattern UI_FIXTURE_READY -Quiet)) { $ready=$true; break }
        Start-Sleep -Milliseconds 100
    }
    if(!$ready) { throw 'UI audio fixture did not become ready' }
    & $editor $project /Game/UI/Maps/Lvl_MainMenu -game -UIFlowTest -RenderOffscreen -ForceRes -ResX=1280 -ResY=720 -windowed -nosplash -nop4 -unattended "-abslog=$logs\UI_Flow.log"
    if($LASTEXITCODE -ne 0) { throw 'UI flow crashed' }
    $flow=Get-Content "$logs\UI_Flow.log" -Raw
    foreach($marker in @('UI_OPENING_IDLE PASS','UI_CHOICE_CONTINUE PASS','UI_OPENING_BLACK PASS','UI_OPENING_REVEAL audio_ready=1 ui_and_voice_same_frame=1','UI_VOICE_FAILURE_HOLD PASS','UI_TUTORIAL_COMPLETE pages=10','UI_PAUSE_RESULT PASS','UI_FREE_CHAT_RESULT PASS','UI_FLOW_RESULT PASS')) {
        if(!$flow.Contains($marker)) { throw "Missing runtime evidence: $marker" }
    }
    if(([regex]::Matches($flow,'UI_TUTORIAL_PLAYED page=-?\d+ audio_bytes=48000 native=1')).Count -ne 19) { throw 'Expected intro plus nine instructions and nine feedback lines' }
    if(([regex]::Matches($flow,'UI_TUTORIAL_GESTURE page=')).Count -ne 9) { throw 'Expected nine confirmed tutorial gestures' }
    foreach($page in 0..8) { if(!$flow.Contains("UI_TUTORIAL_WAIT_GESTURE PASS page=$page")) { throw "Tutorial did not wait for gesture on page $page" } }
    & $editor $project /Game/UI/Maps/Lvl_MainMenu -game -UIHoldTest -RenderOffscreen -ForceRes -ResX=1024 -ResY=768 -windowed -nosplash -nop4 -unattended "-abslog=$logs\UI_Hold.log"
    if($LASTEXITCODE -ne 0 -or !(Select-String "$logs\UI_Hold.log" -Pattern 'UI_HOLD_RESULT PASS' -Quiet)) { throw 'No-voice hold check failed' }
    if($Preview) {
        & $editor $project /Game/UI/Maps/Lvl_MainMenu -game -UIPreview -RenderOffscreen -ForceRes -ResX=1920 -ResY=1080 -windowed -nosplash -nop4 -unattended "-abslog=$logs\UI_Preview.log"
        if($LASTEXITCODE -ne 0 -or !(Select-String "$logs\UI_Preview.log" -Pattern 'UI_PREVIEW_RESULT PASS' -Quiet)) { throw 'UI preview failed' }
    }
    if($Bootstrap) {
        $previousProvider=$env:COSMO_AI_PROVIDER
        try {
            $env:COSMO_AI_PROVIDER='mock'
            & $editor $project /Game/UI/Maps/Lvl_MainMenu -game -UIMenuServiceTest -RenderOffscreen -ForceRes -ResX=1280 -ResY=720 -windowed -nosplash -nop4 -unattended "-abslog=$logs\UI_MenuBootstrap.log"
            if($LASTEXITCODE -ne 0 -or !(Select-String "$logs\UI_MenuBootstrap.log" -Pattern 'UI_MENU_BOOTSTRAP_RESULT PASS' -Quiet)) { throw 'Bundled menu service startup check failed' }
        } finally { $env:COSMO_AI_PROVIDER=$previousProvider }
    }
    Write-Output 'UI_VERIFY_RESULT PASS (synthetic local audio; real Grok service not tested)'
} finally { if(!$fixture.HasExited) { Stop-Process -Id $fixture.Id } }
