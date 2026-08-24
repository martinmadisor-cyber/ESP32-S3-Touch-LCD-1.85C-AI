# Backend de voz del asistente

Corre en el PC, no en el equipo. Recibe el microfono por WebSocket,
transcribe con Whisper local, pide la respuesta al modelo de chat, la
sintetiza con Kokoro y le pasa al equipo la URL de un MP3 para reproducir.

    tu voz -> Whisper (local) -> DeepSeek -> Kokoro -> el equipo baja el MP3

## Como se levanta

    DEEPSEEK_API_KEY=<la clave>  VOICE_PORT=8771  VOICE_STT_MODEL=small  python server.py

`VOICE_PORT` y `VOICE_STT_MODEL` **no son opcionales en la practica**: sin
ellas arranca en 8765 con el modelo `base`, y el firmware busca el 8771.

| Variable | Por omision | Para que |
|---|---|---|
| `DEEPSEEK_API_KEY` | — (obligatoria) | clave del modelo de chat |
| `VOICE_PORT` | 8765 | WebSocket; **el firmware espera 8771** |
| `VOICE_HTTP_PORT` | 8772 | desde donde el equipo baja el MP3 |
| `VOICE_SERVER_IP` | autodetectada | IP que se le anuncia al equipo |
| `VOICE_STT_MODEL` | base | modelo de Whisper (`small` va mejor en espanol) |
| `VOICE_CHAT_MODEL` | deepseek-chat | modelo de chat |
| `VOICE_TTS_VOICE` | ef_dora | voz de Kokoro |

## Cosas que ya costaron caro

- **Los dos puertos necesitan regla de firewall** (8771 y 8772). Sin la del
  8772 el equipo se cuelga entero esperando la descarga, con la pantalla
  muerta y sin ningun log.
- El MP3 va a **44,1 kHz estereo** a proposito: esta placa reproduce MP3
  limpio pero destroza el WAV, y los 24 kHz nativos de Kokoro salian
  lentos y apagados.
- Las respuestas tienen que ser de **una sola frase**: el tiempo de Kokoro
  crece con el largo y con dos frases el equipo se cansa de esperar y se
  desconecta.
- La limpieza de `speech/` ordena **por fecha, no por nombre**. El nombre
  es una marca de tiempo truncada que no crece, y ordenando por nombre el
  servidor llego a borrar el audio que acababa de generar: el equipo lo
  pedia y recibia un 404.
