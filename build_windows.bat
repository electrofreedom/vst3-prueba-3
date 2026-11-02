@echo off
REM Compila el VST3 de 64 bits en Windows (Visual Studio 2022 + CMake 3.22 o superior).
REM Ejecutar desde "x64 Native Tools Command Prompt for VS 2022" o desde un CMD normal.
setlocal
where cmake >nul 2>nul || (echo [ERROR] CMake no esta en el PATH. Instalalo desde https://cmake.org/download/ & exit /b 1)

cmake -S . -B build -G "Visual Studio 17 2022" -A x64 %*
if errorlevel 1 exit /b 1

cmake --build build --config Release --target ReverseVocalReverbTests ReverseVocalReverbProcessorTest ReverseVocalReverb_VST3 --parallel
if errorlevel 1 exit /b 1

echo.
echo === Ejecutando pruebas automaticas ===
ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 (echo [AVISO] Alguna prueba fallo; revisa la salida anterior.)

echo.
echo VST3 generado en:
echo   build\ReverseVocalReverb_artefacts\Release\VST3\Reverse Vocal Reverb.vst3
echo Para instalarlo ejecuta install_vst3.bat como administrador.
endlocal
