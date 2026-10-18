param(
    [string]$ListenHost = "127.0.0.1",
    [ValidateRange(1, 65535)]
    [int]$Port = 8765,
    [ValidateSet('mock', 'grok')]
    [string]$Mode,
    [string]$Model,
    [ValidateRange(1, 65535)]
    [int]$VoicePort = 8766,
    [string]$VoiceModel = 'grok-voice-latest',
    [string]$Voice = 'eve',
    [switch]$DisableVoice
)

$ErrorActionPreference = 'Stop'
if ($PSBoundParameters.ContainsKey('Mode')) {
    $env:COSMO_AI_PROVIDER = $Mode
}
if ($PSBoundParameters.ContainsKey('Model')) {
    $env:XAI_MODEL = $Model
}
if ($env:COSMO_AI_PROVIDER -in @('grok', 'xai')) {
    if ([string]::IsNullOrWhiteSpace($env:XAI_API_KEY) -or [string]::IsNullOrWhiteSpace($env:XAI_MODEL)) {
        throw 'Configure XAI_API_KEY and XAI_MODEL in the Python server environment before starting Grok mode.'
    }
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$runtimeRoot = Join-Path $projectRoot 'Plugins\CosmoBridge\Resources\Runtime\Win64'
$serviceRoot = Join-Path $runtimeRoot 'app\CosmoSystem'
$python = Join-Path $runtimeRoot 'python\python.exe'
if (!(Test-Path -LiteralPath $python)) {
    throw 'Bundled runtime missing. See RUNTIME_DEPLOYMENT.md.'
}

$env:XAI_VOICE_MODEL = $VoiceModel
$env:XAI_VOICE = $Voice
$relayPort = if ($DisableVoice) { 0 } else { $VoicePort }
& $python -B -u (Join-Path $runtimeRoot 'app\cosmo_service.py') --host $ListenHost --port $Port --voice-port $relayPort --log-dir (Join-Path $projectRoot 'Saved\Logs')
exit $LASTEXITCODE
