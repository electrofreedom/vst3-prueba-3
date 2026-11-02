# Reverse Vocal Reverb (VST3)

Plugin de audio VST3 en **C++ / JUCE 8** que captura la primera frase vocal, le aplica una reverb densa (FDN original, inspirada en el carácter envolvente de las salas suaves, sin copiar ningún código ni interfaz de terceros), **invierte** la cola completa y la reproduce como un *swell* que llega a su máximo justo cuando empieza la voz original (o la siguiente frase).

## Cómo funciona (y por qué tiene latencia)

Una inversión perfecta de algo que todavía no ha llegado es imposible, así que el plugin usa **pre-roll con latencia compensada**:

1. La **voz seca se retarda** una cantidad igual a la duración del swell (`SWELL TIME`). El plugin informa esa latencia al DAW, que la compensa en todo el proyecto, por lo que la voz sigue alineada con el resto de pistas.
2. En cuanto el detector ve el inicio de una frase, el swell ya preparado empieza a sonar **`SWELL TIME` antes** de que la voz retardada salga por la salida: el máximo de la reverb inversa coincide con el comienzo de la voz.
3. La primera frase se captura (búfer interno, 30 ms de pre-roll), se reverbera con su cola completa y se invierte en un **hilo de fondo** (estado `PROCESSING`). Ese swell suena antes de la frase siguiente.

```
 entrada ─┬─► [retardo = SWELL TIME] ─────────────────────────────► DRY ─┐
          │                                                              ├─► salida
          └─► VoiceDetector ─► CaptureBuffer ─► ReverbEngine ─► reverse ─┘
                (frase 1)         (≤ 8 s)         (FDN, hilo bg)   swell → pico = inicio de la voz
```

## Modo EXPORT: sin latencia, el swell como archivo de audio

Si no quieres que el plugin retarde la voz, usa **MODE → EXPORT (NO LATENCY)**:

1. El plugin queda con **latencia 0** y la voz pasa intacta. Reproduce tu voz una vez: captura la primera frase sola (o pulsa CAPTURE).
2. Cuando el estado sea **READY**, pulsa **EXPORT WAV**. Guarda un WAV de 24 bits en `Documentos\Reverse Vocal Reverb\` (el nombre incluye la duración, por ejemplo `RVR_swell_2.00s_....wav`).
3. Arrastra el archivo desde la zona **DRAG WAV TO YOUR DAW** (o desde la carpeta con **OPEN FOLDER**) a una pista de audio de Ableton.
4. Coloca el clip de modo que su **final** (el punto de máxima intensidad) caiga justo donde empieza la voz. Con **TAIL = 0 ms** el archivo termina exactamente en ese punto; con TAIL > 0 el pico queda a `SWELL TIME` desde el inicio del archivo.
5. Después puedes quitar el plugin de la pista: el WAV ya no depende de él.

La duración del archivo la fija `SWELL TIME` (o `TEMPO SYNC`); los ajustes de reverb, ATTACK y TAIL se aplican al exportar.

## Controles

| Control | Función |
|---|---|
| **MODE** | `AUTO` detecta y captura sola · `MANUAL` botones/automatización · `MIDI TRIGGER` notas MIDI · `EXPORT` sin latencia, sólo captura y exporta WAV |
| **AUTO CAPTURE** | `ONCE`: captura la primera frase y la usa en las siguientes · `EVERY PHRASE`: cada frase se recaptura y alimenta el swell de la siguiente |
| REVERB TIME 0,5–12 s · ROOM SIZE · DIFFUSION · PRE-DELAY | Reverb (RT60, tamaño, densidad/suavidad, separación) |
| LOW CUT · HIGH CUT | Filtros dentro de la realimentación y en la salida de la reverb |
| STEREO WIDTH | Anchura M/S (0 = mono, 200 %) |
| REVERSE LENGTH 0,25–8 s | Duración máxima de la frase capturada (por defecto 2 s) |
| TRIGGER THRESHOLD · PHRASE GAP · LOCKOUT | Umbral, silencio que cierra la frase y bloqueo temporal anti-recaptura |
| **SWELL TIME** 0,1–12 s | Duración del swell = **latencia** del plugin. Con **TEMPO SYNC** se elige 1/4, 1/2, 1, 2 o 4 compases (usa BPM y compás del DAW; sin tempo usa los segundos libres) |
| REVERSE AMOUNT · ATTACK/FADE-IN · TAIL/FADE-OUT | Intensidad, velocidad del crecimiento y final tras el pico |
| **ARRIVAL OFFSET** ±500 ms | Mueve el punto de llegada respecto al inicio de la voz (palabra, golpe de batería…) |
| GRID ARRIVAL (Manual/MIDI) | `BEAT`/`BAR`: el pico cae en la siguiente línea de rejilla del DAW |
| MIX · DRY LEVEL · WET LEVEL | `MIX` 0 % = 100 % seco (voz intacta, solo retardada) |
| **CAPTURE / REVERSE / RESET** | Captura manual · dispara el swell · borra el búfer. Son parámetros automatizables |

Indicador de estado: `WAITING → CAPTURING → PROCESSING → READY`. Si cambias parámetros de la reverb después de capturar, el swell se re-renderiza solo (≈250 ms tras soltar el control).

### MIDI TRIGGER
`C1 (36)` capturar/terminar captura · `D1 (38)` reproducir el swell · `E1 (40)` reset. **Ableton Live** no enruta MIDI a plugins de efecto de audio: allí usa el modo `MANUAL` y automatiza los botones CAPTURE/REVERSE/RESET desde clips de automatización. En DAWs que sí lo permiten (Reaper, Cubase, Studio One, FL Studio) las notas funcionan.

## Compilar en Windows (64 bits)

Requisitos: **Visual Studio 2022** (carga de trabajo "Desarrollo para el escritorio con C++"), **CMake ≥ 3.22**, **Git**.

```bat
git clone <este-repo> ReverseVocalReverb && cd ReverseVocalReverb
build_windows.bat
```
El script descarga JUCE 8.0.8 (FetchContent), compila el VST3, compila y ejecuta las pruebas y deja el plugin en
`build\ReverseVocalReverb_artefacts\Release\VST3\Reverse Vocal Reverb.vst3`.
Para usar una copia local de JUCE: `build_windows.bat -DJUCE_PATH=C:\dev\JUCE`.

**Sin instalar nada:** sube la carpeta a GitHub; el flujo `.github/workflows/build.yml` compila el VST3 en Windows y lo publica como artefacto descargable.

## Instalar y probar en Ableton Live 12

1. Ejecuta `install_vst3.bat` **como administrador** (copia a `C:\Program Files\Common Files\VST3`).
2. Live 12 → *Preferences → Plug-ins* → activa **Use VST3 Plug-In System Folders** → **Rescan**.
3. Pon *Reverse Vocal Reverb* en una pista de voz. Live compensa la latencia (visible en el delay de la pista).
4. Modo `AUTO`: reproduce un clip con una frase, un silencio de ≥ 0,4 s y una segunda frase. La primera se captura; antes de la segunda oirás el swell creciendo hasta la entrada de la voz.
5. Si el pico no coincide con tu voz, ajusta `ARRIVAL OFFSET`; si la voz empieza con una respiración, sube `TRIGGER THRESHOLD`.
6. Al exportar/bounce el plugin trabaja en modo determinista (espera al render), así que el audio exportado coincide con la escucha.

Notas: cambiar `SWELL TIME` (o el tempo con sync activo) cambia la latencia y Live recalcula la compensación; haz el ajuste fino con el transporte parado. La captura no se guarda en el proyecto: tras reabrirlo el estado es `WAITING` (los parámetros sí se restauran).

## Pruebas

```
ctest --test-dir build -C Release --output-on-failure
```
* `ReverseVocalReverbTests` (sin JUCE): reverb (RT60, estabilidad, correlación L/R, ruido blanco 20 s a RT 12 s), inversión y ventanas sin discontinuidades, detector (frases, pausas, ruido de fondo), tempo/rejilla, y la cadena completa **a 44,1/48/96 kHz, mono/estéreo y búferes de 1 a 4096 muestras**: voz seca bit-exacta al 100 % seco, pico del swell en el inicio de la voz (±5 ms), RESET, modo manual, offset, cuantización a compás.
* `ReverseVocalReverbProcessorTest`: instancia el `PluginProcessor` real de JUCE: latencia declarada, MIDI, presets, guardado/restauración de estado, bypass, layouts mono/estéreo.

## Estructura

```
Source/
  EffectCore.*        núcleo en tiempo real (retardo seco, estados, voces, mezcla, hilo de render)
  VoiceDetector.*     amplitud + transitorios, histéresis, silencio entre frases, lockout
  CaptureBuffer.h     historial + búfer lineal preasignado
  ReverbEngine.*      FDN 8 líneas, Hadamard, difusores, ER, modulación Hermite, filtros en el lazo
  ReverseProcessor.*  reverb → inversión → pico; ventana con fundidos
  TempoSync.h         compases ↔ segundos, cuantización a rejilla
  PluginProcessor.*   parámetros, MIDI, host/transporte, latencia, presets, estado
  PluginEditor.*      interfaz oscura, estado, envolvente del clip invertido
Tests/                pruebas automáticas
```
