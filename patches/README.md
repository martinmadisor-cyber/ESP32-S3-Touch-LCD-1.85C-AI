# Parches locales a librerias de terceros

`Arduino/` esta en el .gitignore y ademas cada libreria de ahi es un clon
git propio, asi que sus archivos **no viajan con este repo**. Los cambios
que les hacemos se guardan aqui como parche para no perderlos.

## esp32-audioI2S-parches-locales.patch

Contra `schreibfaul1/ESP32-audioI2S`, commit base `b99c042`.

Dos arreglos:

1. **`src/Audio.h` — `m_spectrum[3]` a `m_spectrum[FFT_BANDS]`.** El
   analizador llena seis bandas y `getSpectrum()` copia `sizeof(m_spectrum)`,
   asi que solo llegaban tres. Las barras de 4K, 8K y 16K de la pantalla de
   espectro leian fuera del arreglo, sobre las variables vecinas: dos parecian
   muertas y la tercera se movia por casualidad.

2. **`src/Audio.cpp` — colchon antes de empezar a reproducir un stream.**
   Arrancaba con aproximadamente un frame de los 640 KB del buffer, y la radio
   por internet se entrecortaba hasta que la conexion se estabilizaba (el
   camino HLS de la misma libreria ya esperaba 60000 bytes). Ahora pide 16 KB,
   pero si el audio tiene un largo conocido menor que eso, ese largo manda:
   sin esa salvedad, una respuesta corta del asistente no empezaria nunca.

## Como aplicarlo tras reclonar la libreria

    git -C Arduino/ESP32-audioI2S-master apply ../../patches/esp32-audioI2S-parches-locales.patch
