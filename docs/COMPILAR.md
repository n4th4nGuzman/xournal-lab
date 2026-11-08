# Compilar y empaquetar Xournal Lab

## Aplicación C++ (principal)

**Requisitos (Windows 10/11 de 64 bits):** [MSYS2](https://www.msys2.org) con el entorno UCRT64, y dentro de él:

```
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-pdf mingw-w64-ucrt-x86_64-zlib
```

`qt6-pdf` y la impresión de Qt son opcionales: sin ellos la aplicación compila, pero los fondos PDF quedan en blanco y *Imprimir* no está disponible.

**Compilar y ejecutar** (PowerShell):

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cd cpp
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\xl_app.exe          # o: run_app.bat
```

Tras añadir archivos nuevos hay que volver a ejecutar la primera línea de `cmake` (las listas de archivos son comodines).

**Pruebas:**

```powershell
ctest --test-dir build --output-on-failure                      # pruebas unitarias
$env:QT_QPA_PLATFORM = "offscreen"                              # solo para la prueba de interfaz
build\xl_app.exe --selftest --log selftest.log                  # comprobaciones de interfaz, sin ventana
```

No definas `QT_QPA_PLATFORM` al ejecutar `ctest`: algunas pruebas asumen las métricas de texto de la plataforma real.

Algunas pruebas leen documentos de uso personal del autor que no están en este repositorio (el código las busca en una carpeta `samples\` y en rutas locales); esas no se pueden repetir tal cual. El resto de la batería funciona sin ellos.

## Paquete portátil e instalador

```powershell
cpp\package.bat -Zip          # reconstruye y deja cpp\dist\XournalLab\ (y el .zip); no necesita MSYS2 para ejecutarse
cpp\installer.bat 0.1.0-alpha.1    # además genera el instalador con Inno Setup 6
```

- `package.bat` reúne el ejecutable, las bibliotecas de Qt (`windeployqt6`) y las DLL de MSYS2 que hacen falta, comprueba que todas las importaciones se resuelven dentro de la carpeta y ejecuta el resultado una vez con un PATH sin MSYS2.
- `installer.bat` necesita [Inno Setup 6](https://jrsoftware.org/isinfo.php) (`winget install JRSoftware.InnoSetup`). Instala por usuario, sin pedir administrador; asociar los `.xopp` es opcional y viene desmarcado.
- El instalador no está firmado.

### El ícono

El ícono se dibuja en `cpp/app/icon/xournal-lab.svg`. Para regenerar el `.ico` y el PNG que usa la ventana: `python cpp\tools\make_icon.py` (necesita Inkscape y Pillow). Los resultados están versionados, así que compilar la aplicación no los necesita.

## Aplicación Electron (referencia)

```powershell
cd app
npm ci
npm start                       # o: npm start -- ruta\al\archivo.xopp
npm run typecheck
```

`npm run selftest`, `npm run e2e` y `npm run bench` ejecutan las pruebas automáticas; reciben documentos `.xopp` como argumento (no se incluyen aquí).

## Dónde leer más

- [`cpp/ARCHITECTURE.md`](../cpp/ARCHITECTURE.md): organización del código C++ y reglas de trabajo. Menciona rutas del equipo de desarrollo original (`G:\xournal-lab\…`); sustitúyelas por tu carpeta.
- [`cpp/app/API.md`](../cpp/app/API.md): herramientas, paneles, comandos, deshacer y la capa de dibujo en vivo.
- [`cpp/PARIDAD/`](../cpp/PARIDAD): qué hace cada área frente a Xournal++, qué está verificado y qué no.
