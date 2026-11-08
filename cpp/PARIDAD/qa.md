# QA y entrega (area `qa`) - evidencia

Fecha de la corrida: 2026-10-30/31. Arbol de trabajo en edicion (en varios momentos no compilaba por ediciones en curso; las cifras de abajo son de compilaciones verdes, indicadas). Todo lo que sigue se ejecuto de verdad; lo que no se pudo verificar esta en "Riesgos".

## 1. Empaquetado

- `cpp\package.bat` (llama a `package.ps1 [-Build dir] [-NoBuild] [-Zip]`): configura y compila Release, copia `xl_app.exe`, `windeployqt6`, cierra transitivamente las DLL de MSYS2 que importan todos los archivos copiados (objdump), quita plugins que la app no usa (`generic`, `networkinformation`, `tls`), **verifica que cada import se resuelve dentro de la carpeta o en System32/API sets**, y lo ejecuta una vez con un PATH sin MSYS2 ni Qt (captura de pantalla de una muestra).
- Resultado: `cpp\dist\XournalLab\` (38 archivos, **91,6 MB**; el zip `dist\XournalLab.zip` pesa **37,6 MB**). 33 MB son `libicudt78.dll` (datos ICU de este Qt de MSYS2; no se puede quitar sin otro Qt). Contiene Qt6Core/Gui/Widgets/Pdf(pdfium)/Network/PrintSupport, runtime MinGW (libgcc_s_seh, libstdc++, libwinpthread), zlib1, libpng, freetype, harfbuzz, icu, pcre2, zstd, jpeg, glib, etc., `platforms\qwindows.dll`, `imageformats\{gif,ico,jpeg,pdf}`, `styles\qmodernwindowsstyle.dll`.
- Prueba sin MSYS2 en el PATH: `xl_app.exe --screenshot out.png ejercicio_simulacro.xopp` -> exit 0, PNG de 51 KB, **1,2 s** de proceso completo (arranque + abrir + renderizar + capturar + salir). El selftest interno mide "ventana mostrada, primera vista completa" en ~200 ms.
- `cpp\dist\README.txt` (como ejecutarlo, linea de comandos, donde guarda ajustes) se copia dentro de la carpeta. `run_app.bat` no se toco y sigue usando `build\xl_app.exe` con las DLL de MSYS2.
- No se probo en otro PC fisico ni en Windows sin VC++ redistributable: la app usa UCRT (api-ms-win-crt-*, incluido en Windows 10/11); es lo unico externo.

## 2. Intercambio con la app Electron (nunca probado antes)

Metodo: `tools/interchange/run_interchange.ps1`. Un `BrowserWindow` oculto de Electron carga el **`parseXopp`/`serializeXopp` reales** de `app/src/renderer` (empaquetados con el esbuild de `app/node_modules`, solo lectura sobre `app/`), vuelca cada documento a JSON canonico; `xl_dump` hace lo mismo desde el cargador C++; `compare.mjs` compara (numeros con tolerancia 1e-6).

Tres comparaciones por documento, 9 documentos: el generado `xl_qa_gen` (2 paginas de planos con cuadricula/subcuadricula, numberline, Venn 2 y 3, ejes 3D, **los 16 tipos de figura**, texto con `< > & " '`, tildes, simbolos, saltos y tabulaciones, texto markdown `xlMd`, imagen PNG, `teximage`, trazos con glow, presion, resaltador, capas con nombre con caracteres especiales y capa vacia, **todos los papeles especiales y estilos** = 15 paginas, 154 trazos, 85 textos), las 6 muestras, y `e2e-out.xopp` y `e2e_out.xopp` (guardados por Electron):

| comparacion | resultado |
|---|---|
| (a) parseo Electron del original == parseo C++ del original | IDENTICO en 9/9 |
| (b) parseo Electron del **re-guardado por C++** == parseo Electron del original | IDENTICO en 9/9 |
| (c) C++ cargando el **re-serializado por Electron** == C++ cargando el original | IDENTICO en 9/9 |

Control negativo: antes de arreglar mi propio bug del hash los dos lados diferian y el comparador lo marco (2 diferencias), asi que detecta diferencias.

prefs.json (`tools/interchange/prefs_check.mjs` + `xl_prefsio`, solo copias temporales; el archivo real del usuario solo se lee): se extrae `sanitizePrefs` de `main.ts` y se corre sobre 7 entradas (vacio, basura, claves extra, strings enormes, JSON anidado malo, numeros limite, **copia del prefs.json real**). 14/14 OK: Electron(entrada) == Electron(guardado por C++) en todas las claves; C++ escribe todas las claves y conserva las desconocidas; lo que escribe Electron entra y sale de C++ sin cambios. Nota: Electron **descarta** las claves desconocidas al guardar (su `sanitizePrefs` no las copia), asi que `extra` solo sobrevive si el ultimo en guardar es C++.

Hallazgo de interoperabilidad (no arreglable en C++): el `parseXopp` de Electron conserva elementos desconocidos de **capa** (`unsupported`) pero descarta los desconocidos de pagina y raiz (`droppedChildren`). C++ ahora conserva los tres niveles.

## 3. Robustez

### Arreglos (perdida de datos y cierres)
1. **Perdida de datos al guardar**: el cargador *omitia* (`skipped`) todo elemento que no conocia (`<timestamp>`/`<audio>` de grabaciones, `<timeline>`, lo que añada un Xournal++ mas nuevo) y el guardado los borraba. Ahora `Layer/Page/Document::extra` (`RawNode`, arbol completo con texto mixto, prefijos y declaraciones de espacio de nombres, anidamiento maximo 200) los conserva y el escritor los devuelve en su sitio (anclados por "cuantos hermanos conocidos los precedian"; si se borran los vecinos van al final de la capa). `LoadResult::skipped` queda siempre vacio, `preserved` lista lo conservado. Test: `robust_load` (orden, anidado, texto mixto, ns, borrar vecinos, 5000 niveles -> error educado).
2. **Cierre de la aplicacion con violacion de acceso** (`0xC0000005`): reproducido con documentos de un trazo de 1 punto, 2 puntos iguales, `width=""`, 100k trazos y la imagen de 50 MB (dependia de que hubiera tiles/hilos vivos). Con la nueva `crashlog` se resolvio a `Chrome::eventFilter` (`chrome.cpp`: `win.view()->viewport()`): el filtro de eventos de toda la aplicacion seguia recibiendo los ultimos eventos mientras la ventana ya habia destruido su vista, y leia un puntero colgante. Arreglo: `QPointer<QWidget> g_viewport` en `chrome.cpp`. **Esto ocurria al cerrar la app (tambien en el uso normal)**. Despues: 16/16 documentos hostiles con exit 0.
3. `nan`/`inf`/`1e999` en coordenadas ya no se aceptan como numeros (envenenaban cajas y indices); un `+` inicial ahora se lee como numero (otros escritores lo producen).
4. Bomba de gzip: salida limitada a 1,5 GB con mensaje; `bad_alloc` al cargar se captura ("no hay memoria suficiente").
5. Tamaño de pagina hostil (`nan`, 0, negativo, 1e9, texto, ausente): `Page::width()/height()` acotan a 1..14400 pt (el atributo del archivo no se toca); antes el layout hacia `int(ceil(1e9*zoom))` y sumas que desbordaban.
6. Mensajes de error 100 % en español (antes Qt daba "Premature end of document." etc.): tabla en `spanishXmlError`; archivo vacio tiene su propio mensaje; "gzip truncado" tambien.
7. Impresion: el fondo PDF se rasterizaba al ancho en pixeles de la impresora (una impresora PDF de alta resolucion pide 1200 dpi: 140 Mpx = 560 MB por pagina). Limitado a 36 Mpx (~600 dpi en A4) en `core/pageexport.cpp` (`drawOne`). El pico de memoria del selftest paso de **1403 MB a ~620 MB** (ver 4).

### Pruebas añadidas (ctest, 23/23 pasan)
- `tests/robust_load.cpp`: archivos vacio/basura/raiz equivocada/cabecera gzip sola/directorio/inexistente; **430 entradas fuzz** (gzip truncado en puntos al azar, XML truncado, bytes alterados en XML y en gzip) sobre las 6 muestras: ninguna cuelga ni cae, todas las negativas con mensaje en español, y todo lo que carga vuelve a guardarse y a cargarse; 19 casos de numeros (nan, inf, 1e999, `+1`, `1,5`, impar, 0x10, hijo dentro de trazo, 0 y 1 punto, width vacio...); elementos desconocidos; **100k trazos** (guardar ~1,0-3,5 s, cargar ~0,5-1,3 s segun la carga de la maquina, 5,4 MB, igualdad exacta); **500 paginas** (22 ms); **imagen de 50 MB** (guardar ~2,5 s, cargar ~1 s, igualdad exacta).
- `tests/robust_render.cpp`: 30 documentos hostiles cargables a traves de `repairGenerated`, geometria (bbox, hit, transform, split), `ElementIndex` y `renderPage`/`renderElementSeq` a 3 escalas con y sin lite: trazos de 0/1 punto, anchos vacios/negativos/1e300, coordenadas 1e300 y 1.7e308, textos de tamaño 0/1e9/NaN, markdown de 5000 lineas, imagenes con base64 malo / cajas invertidas / NaN, objetos generados con definiciones rotas, tamaños de pagina absurdos, fondos pdf/pixmap inexistentes, papeles con parametros extremos. Cada paso con limite de tiempo; el mas lento 0,4 s.
- App completa: `tools/robust/run_app_fuzz.ps1` (genera 16 archivos con `gen_hostile.mjs` y los abre en `xl_app.exe --screenshot`, offscreen, con limite de 90 s): 8 `bad_*` deben decir `open failed: <mensaje en español>` y 8 `ok_*` abrir y renderizar. **16/16 OK**, el mas lento 1,7 s; pico 145 MB (100k trazos) y 416 MB (imagen de 50 MB).

### Sanitizadores
**No disponibles**: el gcc de MSYS2 UCRT64 no trae `libasan`/`libubsan` y no hay clang instalado (instalarlo son cientos de MB; no se instalo software). En su lugar: compilacion con `-Wall -Wextra -Wpedantic -D_GLIBCXX_ASSERTIONS` (chequeo de limites de `vector`/`string`) de todo el arbol (230 objetivos) y los tests de robustez, y la crashlog con direcciones resolubles. El selftest completo no se corrio con `_GLIBCXX_ASSERTIONS`; las pruebas unitarias si se compilaron con el.

## 4. Memoria y fugas

- **Fuga al abrir/cerrar** (`xl_app --reopen N archivos...`, ventana real offscreen, abrir + renderizar tiles + sustituir documento): 12 rondas de 3 muestras (la de 7722 trazos, algoritmos, simulacro): working set 51,5 -> 54,1 MB, privado 63,5 -> 65,9 MB (la deriva es ~0,2 MB/ronda y se aplana: fragmentacion, no fuga). 6 rondas de 100k trazos + imagen de 50 MB: 58,0 -> 60,7 MB. Todo lo grande se libera al cerrar el documento.
- **Cotas de cache** (leidas del codigo; LRU con presupuesto): tiles 320 MB (`TileCache`), fondos PDF 320 MB (`pdfbg`), burbujas de glow 96 MB, imagenes decodificadas 64 MB (una sola imagen mas grande se mantiene igual: por eso una imagen de 8000x8000 son ~256 MB aunque el limite diga 64), capa temporal / pagina PDF <= 24 Mpx, fondo para exportar/imprimir <= 36 Mpx. En el selftest: "tile cache 186 tiles / 150 MB". Peor caso teorico sumado ~800 MB mas el documento; el documento (XML 50 MB de imagen) cuesta ~8x su tamaño en disco (el texto base64 se guarda como QString UTF-16 + el XML descomprimido durante la carga: 416 MB de pico para un archivo de 38 MB).
- **Los 1,4 GB del selftest**: no eran la app sino la prueba de impresion de `pdfSelfTest` (muestreo del working set cada 40 ms + marcas `[memory after ...]` del log: sube 256 -> 1408 MB dentro de la seccion PDF y solo ahi). Causa: `QPrinter(HighResolution)` a PDF = 1200 dpi, y `paintPage` rasterizaba el fondo PDF a ese ancho (3 paginas x ~380 MB). Imprimir en una impresora real a 600 dpi hubiera costado ~140 MB por pagina; ya esta acotado (ver 3.7). Tras el arreglo el pico del selftest es ~620 MB (medido hasta el punto en que el selftest actual aborta, ver Riesgos).

## 5. Salud del codigo

- Compilado con `-Wall -Wextra -Wpedantic`: **15 avisos**, 0 de `-Wpedantic`. Arreglados 12: `tests/pageexport.cpp` (5 `open()` ignorados), `app/chrome/selftest_lifecycle.cpp` (5), `app/chrome/chrome.cpp` (`prefsPath` sin usar -> `[[maybe_unused]]`; cast `FARPROC`->funcion), y uno **real**: `app/objects/selftest_objects.cpp:333` guardaba una referencia a un temporal (`const auto& p = cur()->spec.pts` con `cur()` devolviendo por valor: comportamiento indefinido) -> copia. Quedan 1 aviso: `app/tools/penaudit.cpp:463` (`events` asignada y no usada; del area pen).
- Duplicados: un solo `blankDocument/blankPageLike` (docfactory) usado por editor, welcome y mainwindow (no hay dos); el pegado: `selection` registra `edit.paste` (objetos propios por MIME) y `texttools/install.cpp:221` lo engancha para imagenes del portapapeles: dos manejadores en cadena, no duplicados, pero conviene que el selftest cubra el orden (solo lo verifica cada area por su lado). El mapeo de ancho de pluma esta en `core/brush.h` (`penWidth`), y las pruebas lo reutilizan; `render_stroke.cpp` y `geometry.cpp` tienen cada uno su `baseWidth`/ancho por defecto (1.41 vs 0.0 para anchos vacios): son dos reglas distintas para el mismo caso (dibujo: 1.41; geometria: 0) y deberian unificarse con `strokeWidthAt` (no lo cambie: afecta la caja de impacto y el render; lo dejo anotado).
- `ARCHITECTURE.md` actualizado (seccion "Delivery, interchange and robustness"). `app/API.md` no cambia (la API de herramientas/paneles no se toco).

## 6. Smoke

`cpp\smoke.ps1 [-Build build-smoke] [-NoBuild] [-Interchange] [-SkipSelftest]`: compila en carpeta propia, ctest (parsea "N of M"), selftest offscreen (cuenta PASS/FAIL del log, pico de memoria muestreado), opcional interchange + prefs. Imprime una pantalla y devuelve el numero de fallos. Corrida de comprobacion: `ctest OK 23 of 23`, `interchange OK 9 documents identical both ways`, `prefs OK 12 checks`, `selftest FAIL` (ver abajo).

## Riesgos y pendientes (honesto)

1. **El selftest actual aborta** con `*** stack smashing detected ***` (exit 0xC0000409) justo despues de `keys: F with nothing selected is the figure tool`, en la seccion de `app/selection/parity_selftest.cpp` (navigation and view keys, `ed.setDocument(fixture(4))` en adelante) tras 996 PASS / 0 FAIL: es codigo nuevo y en edicion del area `select`; es un desbordamiento de buffer en pila real (en la prueba o en lo que prueba). No lo diagnostique. Un `0xC0000409` no pasa por la crashlog (fail-fast).
2. Sin sanitizadores reales (ver arriba): errores de memoria que no cuelgan ni caen pueden quedar.
3. Intercambio verificado en el **modelo** (lo que ven los dos analizadores), no pintando el mismo documento en ambas apps pixel a pixel, ni reabriendo con Electron los objetos generados para reeditarlos (su semantica `xlPlane*/xlFig/xlSet` se prueba en los tests de oro de cada modulo).
4. No hay tableta ni segundo monitor ni otro PC: el empaquetado se probo en esta maquina sin MSYS2 en el PATH, no en una maquina limpia.
5. Los elementos desconocidos se conservan en su sitio relativo, pero si el usuario edita (borra/reordena) los elementos vecinos de un `<timestamp>`/audio, estos pueden quedar desalineados; los datos no se pierden. La grabacion de audio en si no se soporta.
6. `chrome.cpp` y `selection/parity_selftest.cpp` (`names` -> `QStringList`) los toque con arreglos minimos en archivos de otras areas.
7. `dist\` y `tools/interchange/bundle.js` son salidas generadas: no versionar.

## Archivos de esta area

Nuevos: `package.bat`, `package.ps1`, `smoke.ps1`, `dist/README.txt`, `tests/robust_load.cpp`, `tests/robust_render.cpp`, `tools/dump.cpp` (`xl_dump`), `tools/qa_gen.cpp` (`xl_qa_gen`), `tools/prefsio.cpp` (`xl_prefsio`), `tools/interchange/*`, `tools/robust/*`, `app/crashlog.{h,cpp}`, `app/qa/reopen.{h,cpp}`.
Ediciones pequeñas en compartidos: `core/model.{h,cpp}` (`RawNode/RawAt/extra`, `kMaxPageSize`, `pageDimension`), `core/xopp.{h,cpp}` (conservacion, errores en español, limites), `core/pageexport.cpp` (tope del fondo), `app/main.cpp` (`installCrashLog`, `--reopen`), `app/chrome/chrome.cpp`, `app/chrome/selftest_lifecycle.cpp`, `app/objects/selftest_objects.cpp`, `app/selection/parity_selftest.cpp`, `tests/pageexport.cpp`, `ARCHITECTURE.md`.
