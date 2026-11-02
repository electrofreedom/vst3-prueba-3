@echo off
REM Copia el plugin a la carpeta VST3 estandar de Windows (requiere ejecutar como administrador).
setlocal
set SRC=build\ReverseVocalReverb_artefacts\Release\VST3\Reverse Vocal Reverb.vst3
set DST=%CommonProgramFiles%\VST3\Reverse Vocal Reverb.vst3
if not exist "%SRC%" (echo [ERROR] No existe "%SRC%". Ejecuta build_windows.bat primero. & exit /b 1)
xcopy "%SRC%" "%DST%" /E /I /Y
if errorlevel 1 (echo [ERROR] No se pudo copiar. Ejecuta este archivo como administrador. & exit /b 1)
echo Instalado en "%DST%".
echo En Ableton Live 12: Preferences ^> Plug-ins ^> activa "Use VST3 Plug-In System Folders" y pulsa "Rescan".
endlocal
