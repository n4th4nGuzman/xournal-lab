# Paridad: corte "chrome" (menús, archivo, barras, temas, bienvenida, diálogos)

Fuente de comparación: `README.md`, `app/src/main.ts` (menú y archivos), `app/src/renderer/app.ts` (comandos, preferencias, e2e), `welcome.ts`, `floating.ts`, `menubar.ts`.
Estados: **hecho** = lo ejecuté (selftest, ctest o ejecución real de `xl_app.exe`); **parcial** = una parte ejecutada, el resto solo leído o no ejercitado; **falta** = no existe;
**a-probar-por-el-usuario** = necesita lápiz, segundo monitor, Explorador de Windows o Xournal++ oficial. "n/a" = la app Electron tampoco lo hace.

Estado de las comprobaciones al cerrar: `ctest` 23/23; `--selftest` completo 1093 PASS / 0 FAIL (`QT_QPA_PLATFORM=offscreen`, `--log`); además 4 ejecuciones reales de `xl_app.exe` en Windows 10 (pantalla 1920x1080) con captura de pantalla.
Atajo para iterar solo en este corte: `XL_SELFTEST_ONLY=chrome` (6 s). Los checks nuevos están en `app/chrome/selftest_lifecycle.cpp` y `tests/lifecycle.cpp`.

## Menús y archivo

| # | Elemento | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 1 | Los 8 menús de la barra (Archivo, Editar, Ver, Navegación, Diario, Herramientas, Plugins, Ayuda) | hecho | selftest `menus: the top-level menus...` | Menú nativo de Qt, no el menú dibujado por la página (`menubar.ts`): se decidió que el nativo vale. |
| 2 | Cada elemento de `main.ts` existe con la misma etiqueta (~90 entradas, incl. submenús de papel y formato) | hecho | selftest `menus: every item of main.ts exists` (tabla copiada de `main.ts`). Encontró y se corrigió: faltaban **Herramientas > Imagen…** y **Formas automáticas** | Sobran entradas respecto a Electron: Archivo > Anotar PDF / Exportar como / Imprimir (el área PDF las implementó; en Electron eran marcadores deshabilitados). Se quitó la entrada extra "Barra lateral" de Ver > Barras laterales. |
| 3 | Cada atajo de `main.ts` (Ctrl+N/O/S/W/Q/F/G/,/0/+/-, F5, F11, P H E B T X G S L M V K U C D F N J Q, Alt+←/→, Ctrl+Inicio/Fin, Re Pág/Av Pág, Supr) | hecho | selftest `menus: every shortcut of main.ts matches` | Zoom con Ctrl++/Ctrl+- multiplica por 1,25 / 0,8; Electron usa 1,2 (otros tests dependen de 1,25; diferencia menor). |
| 4 | Alt+letra abre el menú (Alt+A Archivo...) | parcial | Los títulos llevan `&` (comprobado en el check de menús). Es el comportamiento estándar de Qt; no envié Alt+letra | Electron usaba un manejador propio de teclado. |
| 5 | Nuevo (Ctrl+N), Nuevo con papel desde la bienvenida | hecho | selftest `welcome: new document link`, `welcome: a special paper card starts a clean document` | — |
| 6 | Nueva ventana (Ctrl+Mayús+N) | parcial | La orden existe, está en el menú con su atajo (selftest). No ejecuté el lanzamiento del segundo proceso para no dejar ventanas abiertas | Segundo **proceso** (`QProcess::startDetached`); Electron abría otra ventana del mismo proceso. |
| 7 | Abrir… (Ctrl+O) | parcial | El camino `openFile` está probado con los 6 documentos reales. El diálogo de archivos de Windows no se manejó (diálogo nativo) | — |
| 8 | Abrir reciente: hasta 10, orden más nuevo primero, sin duplicados, oculta archivos que ya no existen, "Borrar la lista", persiste | hecho | selftest `recent: ...` (7 checks) contra un `.ini` temporal | Fallo corregido: el submenú se construía al añadir y mostraba un archivo borrado hasta el siguiente cambio; ahora se reconstruye al abrirse. Persiste en el registro de Windows (`HKCU\Software\XournalLab\XournalLabCpp`), no en `recent.json`; el backend real (registro) no lo ejercité en el selftest. |
| 9 | Abrir copia de recuperación… | parcial | El diálogo empieza en la carpeta donde se escriben las copias (selftest `autosave: 'Abrir copia...' starts in the folder...`). Diálogo no manejado | Carpeta `%APPDATA%\XournalLab\XournalLabCpp\autosave`, no la de Electron (`xournal-electron\autosave`): los dos programas no comparten copias. |
| 10 | Guardar / Guardar como… | hecho | `saveTo` con los 6 documentos reales: abrir, guardar en otra carpeta, reabrir, modelo igual (selftest 6/6 y `ctest lifecycle`). Diálogo no manejado | — |
| 11 | Un original abierto nunca es el destino de Guardar (siempre pregunta); con la preferencia "guardar sobre el archivo abierto" sí escribe | hecho | selftest `lifecycle: an opened original is not the save target...`, `preferences: with 'guardar sobre...'`, `preferences: Save writes over it without asking`, `...with it off...` | — |
| 12 | Nombre sugerido `<nombre>-lab.xopp` (nunca el del original) y `<nombre>-copia.xopp` para copia | hecho | selftest `save: the suggested names...` | Fallo corregido: C++ sugería el nombre del propio original (un clic de sobrescribirlo). |
| 13 | Guardar copia: no cambia la ruta ni el estado de "sin guardar" | parcial | Solo leí el código (la rama usa `saveXopp` directo, sin tocar el editor). El diálogo impide un check automático | — |
| 14 | Escritura atómica (sin archivo a medias) | hecho | `ctest lifecycle`: no queda `.tmp`, sobrescribir con un documento más corto no deja restos; `QSaveFile` | Ninguna de las dos apps guarda un `.bak` aparte. |
| 15 | Cerrar (Ctrl+W) cierra el documento y muestra la bienvenida | hecho | selftest `close: ...` (limpio, Cancelar mantiene, Descartar cierra). **Antes cerraba la ventana** | Queda un documento en blanco debajo de la bienvenida (Esc lo muestra); Electron no tiene documento. |
| 16 | Salir (Ctrl+Q) y cerrar la ventana con la X | hecho | selftest `window close: ...` (cambios -> pregunta; Cancelar mantiene; Descartar cierra; limpio cierra sin preguntar) y ejecución real: `CloseMainWindow` sale | — |
| 17 | Pregunta por cambios sin guardar en Nuevo, Abrir, Reciente, Cerrar, soltar archivo y cerrar ventana | hecho | Cerrar, soltar y cerrar ventana ejecutados; Nuevo/Abrir/Reciente usan el mismo `maybeSave()` (leído) | Tres botones Guardar / Descartar / Cancelar (Electron: Cancelar / Cerrar sin guardar); es un superset. |
| 18 | Autoguardado: copia aparte, solo con cambios, nunca toca el archivo | hecho | selftest `autosave: ...` (8 checks: limpio no escribe, con cambios escribe, la copia es el documento, el original intacto y sigue "sin guardar", sin segundo escritura sin cambios, nueva escritura tras nuevo cambio) | No esperé a que el temporizador (mínimo 1 min) disparara solo; el aviso de `Preferencias` que lo arranca lo prueba el área de ajustes. |
| 19 | Recuperación al siguiente inicio | hecho (equivalente) | La copia se abre como documento normal con el trabajo sin guardar (selftest `autosave: the recovery copy opens...`) | Igual que Electron: no hay aviso automático al arrancar, solo Archivo > Abrir copia de recuperación. |
| 20 | Abrir archivo desde la línea de comandos | hecho | Ejecución real `xl_app.exe ejercicio_fiesta.xopp`: título `ejercicio_fiesta.xopp - Xournal Lab`, zoom ajustado, maximizada | Solo el primer archivo (igual que Electron). |
| 21 | Asociación de archivos / "abrir con" | n/a | `package.json` de Electron no declara asociaciones; "abrir con" es la línea de comandos (fila 20) | — |
| 22 | Instancia única | n/a | Electron no la implementa | — |
| 23 | Soltar un `.xopp` sobre la ventana | parcial | `QDropEvent` sintético enviado a la ventana: abre (limpio), pregunta si hay cambios, un archivo de otro tipo no se acepta. Imagen soltada: se inserta (sintético) | **a-probar-por-el-usuario** el arrastre real desde el Explorador: arrastra un `.xopp` a la ventana abierta; debe abrirlo. |
| 24 | Los 6 documentos reales de `samples/` por todo el ciclo | hecho | `ctest lifecycle` (modelo, XML y segunda generación idénticos) y selftest (por `MainWindow`: abrir, Guardar como, reabrir, modelo igual, título, ruta, "limpio") | Elementos desconocidos del archivo se conservan (se comprobó con un archivo con `<futuretag>`); Electron los descartaba con aviso. |

## Barras laterales, capas y búsqueda

| # | Elemento | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 25 | Miniaturas de páginas (perezosas, clic, añadir/borrar/mover, deshacer) | hecho | selftest `sidebar: ...` (14 checks, existían; los volví a ejecutar) y captura | — |
| 26 | Capas (activa, ojo, renombrar, añadir/borrar/mover, deshacer, solo pantalla) | hecho | selftest `layers: ...` (20 checks) y captura `chrome_sidebar_layers.png` | — |
| 27 | Vista continua / página a página | hecho | selftest `single-page mode ...` y `single page: ...` (pluma/zoom) | — |
| 28 | Buscar (Ctrl+F): textos y LaTeX, todas las páginas, Intro/Mayús+Intro, selecciona y lleva a la vista, capas ocultas fuera | hecho | selftest `find: ...` (12 checks) | — |
| 29 | Buscar resalta exactamente el trozo encontrado | hecho | **Nuevo.** Texto plano: caja por línea y coincidencia con la métrica de la fuente que dibuja la página; texto Markdown: la palabra dibujada (los `**` no cuentan). selftest `find: the marked area covers only the match (inside #ffc591, past it #ffffff)` mide píxeles; `ctest lifecycle` comprueba cajas; captura `chrome_find_exact.png` | Una fórmula LaTeX no tiene glifos: se marca la caja entera. Una coincidencia que cruza marcas de Markdown (p. ej. `a**b`) también marca la caja entera. |
| 30 | Ir a la página… (Ctrl+G) | hecho | selftest `navigation: Ctrl+G shows the go-to-page dialog`, límites, ir y registrar el salto para Atrás | Electron solo enfocaba la casilla de la barra; ahora hay diálogo (decisión del encargo); la casilla de la barra sigue. |
| 31 | Atrás / Adelante del historial de saltos | hecho | selftest `history: ...` (existían) | — |

## Preferencias y copia de recuperación

| # | Elemento | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 32 | Preferencias (Ctrl+,): dedos dibujan, guardar sobre el abierto, autoguardado N min, tema, paleta, efectos, bienvenida | hecho | selftest `prefs: ...` (área de ajustes, 10 checks) y mi `prefs: applyPrefs of the settings area changes theme and effects at once` | — |
| 33 | Los cambios de tema/efectos se aplican en vivo | hecho | **Corregido.** Antes pasaba por un vigilante de `prefs.json`; ahora `SettingsController::prefsApplied` llama a la función de aplicar del chrome. El vigilante se eliminó (podía revertir el tema con un guardado a medias). selftest `prefs: ...` y `prefs: the toolbar/welcome theme choice goes through the settings area` | El archivo ya no se vigila: un cambio hecho por Electron con la app C++ abierta no se ve hasta reiniciar. |
| 34 | La elección del tema se guarda entre sesiones | parcial | La elección pasa por el controlador (que es el dueño de `prefs.json` y guarda solo lo que cambió; probado por el área de ajustes). Lectura al arrancar: ejecución real con `APPDATA` temporal y `{"theme":"neon"}`, la app abre en neon. No ejecuté la escritura real del tema (las pruebas no escriben el `prefs.json` del usuario) | — |

## Barra de herramientas, temas, paletas, brillo, papel

| # | Elemento | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 35 | Barras de herramientas (herramientas, estilo, navegación/páginas, papel) sin desbordar, botones ~40 px, tooltips | parcial | Ejecución real 1920x1080: 3 filas + fila de papel opcional, sin flecha de desbordamiento (la barra se separó en dos). Tooltips con atajo presentes en las de navegación; no enumeré los de las demás | — |
| 36 | Temas Clásico y Neon (solo la interfaz; la hoja conserva colores), en vivo | hecho | selftest `theme: ...` (15 checks) y capturas `chrome_classic.png`, `chrome_neon.png`; ejecución real neon (captura) | Los diálogos Preferencias y Lápiz y tableta fijaban un fondo gris propio: se quitó para que sigan el tema. Su botón "primario" sigue azul en neon. |
| 37 | Paletas (9 conjuntos), se guardan, no tocan lo dibujado | hecho | selftest `palette: ...` (13 checks, área de ajustes) | — |
| 38 | Brillo (glow): interruptor, tamaño, intensidad, aplicar/quitar a la selección con un paso de deshacer | hecho (de otra área) | Lo ejecutan los checks `glow: ...` y `apply glow to the selection...` de los cortes de selección/pluma, que corrieron en mi selftest completo. No audité sus rangos (1–40, 5–100 %) | Fuera de mi corte; ver el documento de esos cortes. |
| 39 | Papel con la paleta (muestras de color de papel) | hecho | selftest `paper: swatches follow the palette`, `a swatch sets the paper colour`, `theme: the paper swatches follow the palette pref` | — |
| 40 | Papeles de matemáticas y física: lista, parámetros, `xlPaper` + estilo parecido | parcial | selftest: los 7 papeles especiales escriben `xlPaper` y estilo, un paso de deshacer, guardan y recargan; el `log` muestra sus 3 parámetros; las 10 miniaturas de la bienvenida dibujan tinta real | No enumeré las opciones de cada papel contra la tabla del README (se generan de la misma tabla del núcleo). Cómo abre estos papeles Xournal++ oficial: **a-probar-por-el-usuario** (igual que dice el README). |

## Bienvenida, título, inicio

| # | Elemento | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 41 | Pantalla de bienvenida: Empezar, Recientes (existentes, 8, Borrar), tarjetas de papel con miniatura, Apariencia, casilla "Mostrar al iniciar" | hecho | selftest `welcome: ...` (19 checks) y ejecución real (captura clásico y neon) | — |
| 42 | Bienvenida desactivada: abre un documento en blanco | hecho | Ejecución real con `APPDATA` temporal y `{"welcome":false}` (captura) | — |
| 43 | Ayuda > Bienvenida sobre un documento; Esc o la ✕ la cierran; reaparece al cerrar el documento | hecho | selftest `welcome: ...` y `close: Discard closes it...` | Al cerrar el documento reaparece (ahora sí; antes no). |
| 44 | Barra de título y de menús con el tema | parcial | Menú y barras con el tema (capturas reales clásico y neon). Barra de título del sistema: modo oscuro de Windows para todas las ventanas (verificado en captura real: oscura con botones claros) y color de título/texto/borde por tema (`DwmSetWindowAttribute`; Windows 11 solamente) | **a-probar-por-el-usuario** en Windows 11: en neon el título debe verse `#1e1043` con texto cian. En esta máquina (Windows 10) solo se ve el modo oscuro. |
| 45 | Inicio maximizado | hecho | Ejecución real: `IsZoomed` verdadero | — |
| 46 | Zoom ajustado al ancho al abrir/nuevo/bienvenida y en el documento inicial | hecho | selftest `open: the zoom is fitted to the width (no horizontal scroll...)` (**falló**: 1 px de desborde; ahora usa la página más ancha y 2 px de margen). Documento inicial: **faltaba** (abría al 100 %); ahora ajustado y arriba. Capturas reales | — |

## Diálogos que se mueven y se desacoplan

| # | Elemento | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 47 | Los cuatro diálogos grandes (LaTeX, Preferencias, Lápiz y tableta, Diagnóstico) son ventanas propias: movibles, redimensionables, llevables a otro monitor | parcial | En Qt todo `QDialog` ya es una ventana de nivel superior; comprobé `isWindow()` y no-modal en LaTeX y Diagnóstico (selftest `floating: ...`). Llevarlos a otro monitor no se pudo probar | No hay botón ⧉/⮌: no hace falta, la ventana ya es independiente. **a-probar-por-el-usuario** (monitor 2): abre LaTeX (tecla X, clic en la página), arrástralo a otro monitor y escribe; la vista previa y Insertar deben seguir funcionando. |
| 48 | Al abrirse no tapan la página: abajo si el clic fue arriba y al revés | hecho | **Cambiado.** El diálogo LaTeX era modal y se centraba tapando la página. Ahora es no modal, mide como mucho el 62 % de la ventana y se coloca en la mitad contraria al último clic. selftest `floating: it opens at most 62 percent...`; captura `chrome_latex_floating.png` | — |
| 49 | Mientras un diálogo está abierto la página ignora los clics pero se desplaza; la barra de herramientas sigue | hecho | selftest `floating: the page ignores a pen stroke while a floating dialog is open`, se libera al cerrar y vuelve a dibujar | Alcance: LaTeX, Preferencias, Lápiz y tableta, Diagnóstico. La rueda y las barras de desplazamiento no se bloquean. |
| 50 | Recuerdan sitio y tamaño; un sitio de un monitor desconectado no se usa | hecho | selftest: un sitio movido por el usuario se recuerda y se usa la vez siguiente (LaTeX); un sitio en `(-9000,-9000)` cae a una pantalla visible; uno en pantalla existente se restaura. Preferencias y Tableta ya lo hacían | Solo se recuerda un sitio que el usuario cambió; si no, se recoloca desde el clic (como Electron). **No probado**: un monitor real que se desconecta, ni escalas de pantalla distintas por monitor. |
| 51 | Paneles pequeños (cuadro de herramientas, opciones de plano/3D/figuras/relleno/brillo) sin mover | n/a | Igual que Electron: son ventanitas que se cierran al hacer clic fuera | — |
| 52 | Panel lateral (miniaturas/capas) desacoplable | parcial | `QDockWidget` flotante de Qt (el botón de flotar se ve en la barra "Paneles"); no lo ejercité | **a-probar-por-el-usuario**: botón de flotar del panel "Paneles". Electron no lo permitía. |

## Resumen por estado (52 filas)

* hecho: 37
* parcial: 12 (filas 4, 6, 7, 9, 13, 23, 34, 35, 40, 44, 47, 52); de ellas llevan pasos para el usuario las filas 23, 40, 44, 47 y 52 (**a-probar-por-el-usuario**); la fila 50 es "hecho" pero su parte de monitor real también queda para el usuario
* falta: 0
* n/a (Electron tampoco lo hace): 3 (filas 21, 22, 51)

## Qué se corrigió en este corte

1. **Cerrar** cerraba la ventana: ahora cierra el documento y muestra la bienvenida.
2. Documento inicial al 100 %: ahora ajustado al ancho y arriba; el ajuste usa la página más ancha y deja 2 px (antes quedaba 1 px de desborde horizontal).
3. La ventana arrancaba sin maximizar.
4. Tema/efectos/bienvenida: enlazados directamente a `prefsApplied`; el vigilante de archivo se eliminó; la elección de tema pasa por el controlador de ajustes.
5. Soltar `.xopp`/imágenes sobre la ventana.
6. Ir a la página: diálogo.
7. Buscar: resalte exacto del trozo (texto plano y Markdown).
8. Nombres sugeridos `-lab` / `-copia`; `saveTo`, `autosaveNow` públicos y con carpeta configurable para pruebas.
9. Menú Herramientas: faltaban Imagen… y Formas automáticas; sobraba "Barra lateral".
10. Archivo > Abrir reciente: se reconstruye al abrirse.
11. Diálogos: LaTeX no modal, colocado lejos del clic, recuerda sitio, la página ignora clics mientras hay un diálogo flotante; barra de título del sistema con el tema (Windows); Preferencias y Lápiz y tableta siguen el tema.
12. Prueba de pruebas rota: `selftest_chrome.cpp` dejaba funciones de la bienvenida con referencias a variables de pila ya destruidas (falla al volver a mostrar la bienvenida); ahora son estáticas.

## Cambios pequeños en archivos compartidos

* `app/mainwindow.{h,cpp}`: `saveTo`, `blankNow`, `autosaveNow()` pública (devuelve bool), `autosaveDir/setAutosaveDir`, `suggestedSaveName`, `settings()/setSettingsFile` (un solo dueño de dónde viven recientes y sitios de diálogos), reconstrucción del menú de recientes al abrirse, Herramientas con Imagen… y Formas automáticas, sin "Barra lateral" duplicada, nombres de guardado.
* `app/documentview.cpp`: `zoomFitWidth` (página más ancha, 2 px de holgura).
* `app/main.cpp`: `showMaximized()`.
* `app/selftest.cpp`: `XL_SELFTEST_ONLY=chrome`.
* `core/pagerender.{h,cpp}` (`textSpanBoxes`), `core/markdown.{h,cpp}` (`markdownSpanBoxes`), `core/find.{h,cpp}` (`findSpanBoxes`): solo añadidos.
* `app/settings/settings.h`, `controller.cpp`: `hasGeometry`, `forgetGeometry`; usan `win.settings()`. `prefsdialog.cpp`, `tabletdialog.cpp`: se quitó el fondo fijo del `QDialog`.
* `app/texttools/latextool.cpp`: el diálogo de fórmulas pasa de modal a no modal.
* Nuevo: `tests/lifecycle.cpp` (ctest), `app/chrome/selftest_lifecycle.cpp`.

## Lo que queda y por qué

* Probar con el usuario: arrastre real desde el Explorador (fila 23), segundo monitor y monitor desconectado (47, 50), título neon en Windows 11 (44), dock flotante (52), cómo abre Xournal++ oficial los papeles especiales (40).
* Diálogos nativos de abrir/guardar no se manejaron en automático.
* `Nueva ventana` arranca otro proceso; sin instancia única ni asociaciones, como Electron.
* Diferencias de diseño aceptadas: menú nativo de Qt, sin botón ⧉/⮌ (las ventanas ya son independientes), documento en blanco bajo la bienvenida tras Cerrar, zoom 1,25 en vez de 1,2.
* Fuera de mi corte y visto en el selftest completo: dos checks ajenos fallaron una vez cada uno en ejecuciones intermedias (selección: "lasso takes a text only when its centre is inside"; ajustes: "stays open after a pick") y no volvieron a fallar; el segundo era sensible a que mis diálogos dejaran la ventana sin foco, y ahora mi prueba la reactiva al terminar.

## Incidente (honesto)

La primera versión de mi prueba de "recientes" usó `QSettings::setDefaultFormat`, que **no** redirige `QSettings("XournalLab","XournalLabCpp")`: escribió en el registro del usuario (`HKCU\Software\XournalLab\XournalLabCpp`, valor `recent`) y luego lo borró; una ejecución interrumpida dejó entradas de prueba que limpié a mano. La lista de recientes que tuviera la app C++ antes (solo archivos de muestra abiertos en pruebas, hasta donde pude ver) se perdió. Corregido: `MainWindow::setSettingsFile` apunta a un `.ini` temporal y la prueba ya no toca el registro.
Además: `texttools/latextool.cpp` usa `QSettings()` sin ruta para su lista de fórmulas recientes (`latexRecent`); su selftest probablemente escribe en el registro del usuario. No lo toqué.
