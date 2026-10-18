param(
    [string]$EngineRoot='D:\APP\APP_Game\Unreal Engine\UE_5.7',
    [switch]$SkipBuild,
    [switch]$ImportSources,
    [switch]$RebuildLevel
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$projectFile=Join-Path $projectRoot 'VFX.uproject'
$editor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$ubt=Join-Path $EngineRoot 'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll'
$dotnet=Get-ChildItem (Join-Path $EngineRoot 'Engine\Binaries\ThirdParty\DotNet') -Filter dotnet.exe -Recurse |
    Where-Object { $_.FullName -like '*win-x64*' } | Select-Object -First 1 -ExpandProperty FullName
foreach($file in @($projectFile,$editor,$ubt,$dotnet)) {
    if(!$file -or !(Test-Path -LiteralPath $file)) { throw "Missing prerequisite: $file" }
}
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'Saved\Logs') -Force | Out-Null
Push-Location $projectRoot
try {
    if(!$SkipBuild) {
        & $dotnet $ubt UnrealEditor Win64 Development "-Project=$projectFile" -WaitMutex -NoHotReloadFromIDE -EnablePlugins=CodexBlueprintTools
        if($LASTEXITCODE -ne 0) { throw 'Unreal build failed.' }
    }
    if($ImportSources) {
        $script=Join-Path $PSScriptRoot 'import_alien_planet.py'
        $log=Join-Path $projectRoot 'Saved\Logs\Planet_Import.log'
        & $editor $projectFile -run=pythonscript "-script=$script" -unattended -nop4 -nosplash -nullrhi '-ExecCmds=Interchange.FeatureFlags.Import.PNG 0,Interchange.FeatureFlags.Import.FBX 0' "-abslog=$log"
        if($LASTEXITCODE -ne 0) { throw "Scene import failed: $log" }
    }
    if($RebuildLevel -or $ImportSources) {
        # Regeneration replaces this generated map. The normal balance data asset is preserved.
        $log=Join-Path $projectRoot 'Saved\Logs\Planet_Setup.log'
        & $editor $projectFile -run=CodexPlanetSetup -EnablePlugins=CodexBlueprintTools -unattended -nop4 -nosplash -nullrhi "-abslog=$log"
        if($LASTEXITCODE -ne 0) { throw "Scene generation failed: $log" }
    }
    $log=Join-Path $projectRoot 'Saved\Logs\Planet_Verify.log'
    & $editor $projectFile -run=CodexPlanetSetup -Verify -EnablePlugins=CodexBlueprintTools -unattended -nop4 -nosplash -nullrhi "-abslog=$log"
    if($LASTEXITCODE -ne 0) { throw "Playable scene verification failed: $log" }
    Select-String -LiteralPath $log -Pattern 'PLANET_TEST_RESULT'
} finally { Pop-Location }
