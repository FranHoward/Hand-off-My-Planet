param(
    [string]$EngineRoot = 'D:\APP\APP_Game\Unreal Engine\UE_5.7',
    [switch]$SkipBuild,
    [switch]$RebuildAssets,
    [switch]$ImportSources
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'VFX.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$ubt = Join-Path $EngineRoot 'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll'
$dotnet = Get-ChildItem (Join-Path $EngineRoot 'Engine\Binaries\ThirdParty\DotNet') -Filter dotnet.exe -Recurse |
    Where-Object { $_.FullName -like '*win-x64*' } | Select-Object -First 1 -ExpandProperty FullName
foreach ($file in @($projectFile, $editor, $ubt, $dotnet)) {
    if (!$file -or !(Test-Path -LiteralPath $file)) { throw "Missing prerequisite: $file" }
}
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'Saved\Logs') -Force | Out-Null
Push-Location $projectRoot
try {
    if (!$SkipBuild) {
        & $dotnet $ubt UnrealEditor Win64 Development "-Project=$projectFile" -WaitMutex -NoHotReloadFromIDE -EnablePlugins=CodexBlueprintTools
        if ($LASTEXITCODE -ne 0) { throw 'Unreal build failed.' }
    }
    if ($ImportSources) {
        $importScript = Join-Path $PSScriptRoot 'import_creature_animations.py'
        $importLog = Join-Path $projectRoot 'Saved\Logs\CreatureAnimation_Import.log'
        & $editor $projectFile -run=pythonscript "-script=$importScript" -unattended -nop4 -nosplash -nullrhi '-ExecCmds=Interchange.FeatureFlags.Import.PNG 0,Interchange.FeatureFlags.Import.FBX 0' "-abslog=$importLog"
        if ($LASTEXITCODE -ne 0) { throw "Animation import failed. See $importLog" }
    }
    if ($RebuildAssets -or $ImportSources) {
        # Explicit opt-in: regeneration replaces the generated AnimGraphs and COSMO notify tracks.
        $generateLog = Join-Path $projectRoot 'Saved\Logs\CreatureAnimation_Generate.log'
        & $editor $projectFile -run=CodexCreatureAnimations -EnablePlugins=CodexBlueprintTools -unattended -nop4 -nosplash -nullrhi "-abslog=$generateLog"
        if ($LASTEXITCODE -ne 0) { throw "ABP generation failed. See $generateLog" }
    }
    $verifyLog = Join-Path $projectRoot 'Saved\Logs\CreatureAnimation_Verify.log'
    & $editor $projectFile -run=CodexCreatureAnimations -Verify -EnablePlugins=CodexBlueprintTools -unattended -nop4 -nosplash -nullrhi "-abslog=$verifyLog"
    if ($LASTEXITCODE -ne 0) { throw "Animation verification failed. See $verifyLog" }
    Select-String -LiteralPath $verifyLog -Pattern 'CREATURE_TEST_RESULT'
} finally {
    Pop-Location
}
