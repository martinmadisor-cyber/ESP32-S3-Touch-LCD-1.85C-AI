# Levanta el backend de voz con los valores que espera el firmware.
# La clave del modelo se lee de la variable de entorno DEEPSEEK_API_KEY,
# nunca de este archivo.
$ErrorActionPreference = "Stop"
if (-not $env:DEEPSEEK_API_KEY) {
    Write-Error "Falta DEEPSEEK_API_KEY en el entorno."
    exit 1
}
$env:VOICE_PORT = "8771"        # el firmware busca este puerto
$env:VOICE_STT_MODEL = "small"  # 'base' entiende bastante peor el espanol
Push-Location $PSScriptRoot
try { python server.py } finally { Pop-Location }
