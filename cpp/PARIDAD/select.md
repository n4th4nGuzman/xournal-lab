# Paridad: corte "select" (cuadro de herramientas, selección, portapapeles, deshacer/rehacer, teclado)

Fuente de comparación: `README.md` (secciones "Cuadro de herramientas flotante", "Cuadro de herramientas: zonas, separadores, catálogo y arrastrar desde la barra", el capítulo de selección de "Usar la aplicación"), `app/src/renderer/{app.ts,editor.ts,selection.ts,toolbox.ts,icons.ts,prefs.ts}` y `app/src/main.ts` (`sanitizePrefs`).
Estados: **hecho** = lo ejecuté (selftest, ctest, node o ejecución real de `xl_app.exe`); **parcial** = una parte ejecutada, el resto solo leído o no ejercitado; **falta** = no existe;
**a-probar-por-el-usuario** = necesita lápiz/tableta, ratón real con arrastre del sistema, segundo monitor o Xournal++ oficial. "n/a" = la app Electron tampoco lo hace.

Estado al cerrar: `ctest` 23/23; `--selftest` completo con `QT_QPA_PLATFORM=offscreen --log` 1100 PASS / 0 FAIL (incluye 172 checks nuevos de este corte).
Atajo para iterar solo en este corte: `XL_SELFTEST_ONLY=parity` (40 s). Los checks nuevos están en `app/selection/parity_selftest.cpp`.

**Cómo se probó el teclado y el ratón.** El archivo nuevo no manda eventos a un widget elegido: usa `qt_handleKeyEvent` y `qt_handleMouseEvent`, los dos puntos de entrada que exporta Qt6Gui y que usa el propio QTest. Así la tecla pasa por el **mapa de atajos de Qt**, `ShortcutOverride`, el foco y la captura del ratón, que es lo que pasa con un teclado real (los tests anteriores enviaban `QKeyEvent` a un widget y se saltaban todo eso; así se encontró que Re Pág / Av Pág estaban rotos). Sigue siendo la plataforma `offscreen`: **no es un lápiz ni un ratón físico**, y esa plataforma no tiene arrastrar y soltar del sistema (ver filas 11–12).

## A. Cuadro de herramientas flotante

| # | Elemento | Estado | Cómo se verificó | Desviaciones / notas |
|---|---|---|---|---|
| 1 | Abrir con **Q**; Q otra vez lo cierra; Q no cambia de herramienta | hecho | parity `keys: Q opens / does not change the tool / closes again` (tecla real por el mapa de atajos; la segunda Q la recibe el propio cuadro, que tiene su ventana) | — |
| 2 | **Esc** cierra; un clic fuera cierra; Mayús+Q no lo abre | hecho | parity `keys: Esc closes`, `Shift+Q does not open`, `toolbox: a real click outside closes the box` (ratón por la ruta de plataforma) | Ctrl+Q es "Salir" en esta app (igual que el menú de Electron); no abre el cuadro. |
| 3 | Herramientas ▸ Mostrar el cuadro de herramientas | hecho | parity `tool box: Herramientas > Mostrar ... opens it` (activa la orden del menú; no hice clic en la entrada) | — |
| 4 | **Botón lateral 1 del lápiz** abre el cuadro donde tocas | parcial | parity: un `PointerEvent` de lápiz con el botón 1 abre el cuadro (`tool box: the pen's side button (barrel 1) opens it`); no medí que la posición coincida con el punto | **a-probar-por-el-usuario** con la tableta: *Herramientas ▸ Lápiz y tableta*, botón lateral 1 = "Cuadro de herramientas"; toca con el botón pulsado: debe abrirse bajo la punta. Si el controlador de XP-Pen reasigna el botón a clic derecho o a una tecla, puede que no llegue como botón. |
| 5 | Tecla de la tableta/mando "Mostrar u ocultar el cuadro" | parcial | Las órdenes `toolbox.show` por teclas asignadas están en el selftest de ajustes (corrida completa, pasa). Mía: una tecla propia asignada a Q gana a abrir el cuadro | **a-probar-por-el-usuario** con el ACK05 real (Ctrl+Alt+Mayús+número). |
| 6 | Un atajo propio sobre la Q tiene prioridad | hecho | parity `tool box: a binding of the user on Q wins over opening the box` (Q→Mano) | — |
| 7 | Un clic elige la herramienta, la activa se marca, se cierra; "Cerrar al elegir" apagado lo deja abierto | hecho | parity `a real click on a button picks the tool and closes the box`, `with 'Cerrar al elegir' off the box stays open` (ratón real) + checks de ajustes (`the current tool is marked`) | — |
| 8 | Modo **Editar**: reordenar, sacar, añadir; zonas (hasta 6 con 20 botones), separadores, catálogo por categorías, buscador sin mayúsculas/acentos, atenuados los ya puestos, Restaurar, Desactivar | hecho | checks del área de ajustes (corrida completa) y parity: `a drop from the catalogue on a zone adds the button`, `... moves it to the end`, `... another zone`, `... on the catalogue takes it out`, `a drag that is not one of the box's buttons is not accepted` — los drops llegan a los widgets como `QDragEnter/Move/DropEvent` | — |
| 9 | En edición el cuadro no se cierra al clic fuera ni al pulsar un botón (no elige); se mueve arrastrando el título | hecho | parity `in edit mode a click on a button picks nothing and a click outside does not close the box`, `the title bar drags the box` (ratón real) | — |
| 10 | Desactivar el cuadro (botón y menú): Q, botón del lápiz y menú no hacen nada y no cambian de herramienta | hecho | parity `switched off, Q opens nothing and does not change the tool`, `the menu entry does not open it either`, `the pen's side button does nothing`, `the menu entry switches the box off and on again` | — |
| 11 | **Arrastrar de verdad** (ratón o lápiz) un botón del catálogo / de la barra al cuadro, y sacarlo soltándolo fuera | a-probar-por-el-usuario | No se pudo: la plataforma `offscreen` no tiene arrastrar y soltar (el `QDrag` vuelve en el acto con "ignorado"). Hecho en su lugar: revisión del código de arrastre (mime, `QDrag::exec`, `onIgnored` = quitar solo cuando nadie aceptó, igual que `dropEffect === "none"` en `toolbox.ts`), todos los drops sobre los widgets y el mime de cada botón de la barra | Pasos: *Editar*; arrastra "Espacio vertical" del catálogo a una zona (aparece una barra azul donde caerá); arrastra un botón de la zona fuera del cuadro (desaparece); arrastra el botón de la **barra de la ventana** (p. ej. el lazo, o ⟳ de la fila de colores) al cuadro. |
| 12 | Arrastrar desde un **menú** al cuadro (extra, no existe en Electron) | a-probar-por-el-usuario | Solo revisión del filtro de eventos del menú. | Es una extensión de la app C++. |
| 13 | Los botones de la barra de la ventana se pueden arrastrar al cuadro (cualquier fila: herramientas, colores, zoom, páginas, selección) | parcial | **Corregido**: el filtro solo miraba la primera barra; ahora vale cualquier `QToolBar` de la ventana (filas de estilo y de páginas incluidas). Verificado `itemOfAction` de cada acción; el gesto real es la fila 11 | Sin botón en el catálogo (igual que Electron): Nuevo, Markdown, Papel/Tema, Capas/Miniaturas. |
| 14 | **Los 51 botones del catálogo hacen algo** (ids de órdenes reconciliados) | hecho | parity `every catalogue item maps to a registered, enabled command` + ejecuta cada herramienta, páginas (nueva, antes, al final, eliminar, primera/anterior/siguiente/última), zoom, recog, brillo, rotar/espejar/frente/fondo/duplicar/copiar/pegar/cortar con el botón del cuadro | **Corregido**: `bar:recog`→`tool.recognizer`, `bar:front/back/rotL/rotR/flipH/flipV`→`sel.*` (estaban en ids inexistentes y salían atenuados); `runAction` trataba `tool.recognizer` como herramienta que no existe. Abrir, Guardar, Imagen y Lápiz y tableta abren diálogos modales: solo comprobé que existen y están activos. |
| 15 | El **catálogo es idéntico** al de Electron (ids, orden, etiquetas, grupos) | hecho | `PARIDAD/select_tools/check_prefs.cjs`: carga `TOOLBOX_CATALOG` de `toolbox.ts` (esbuild) y lo compara con el volcado del C++: 51 = 51, mismos ids/etiquetas/grupos | — |
| 16 | Posición: centrado en el puntero, primera fila bajo él, dentro de la pantalla | hecho | parity `opens centred on the pointer` | — |
| 17 | Iconos de los botones del cuadro | hecho | Captura `parity_toolbox_edit.png`. **Corregido**: "Lápiz y tableta" y "Brillo" salían con una letra; el lazo y el espacio vertical también (ahora iconos dibujados como los de `icons.ts`) | Los iconos son QPainter, no los SVG de Electron (Qt Svg no está en el build). |
| 18 | Se recuerda entre sesiones; **`prefs.json` compartido** | hecho | Ver sección E | — |

## B. Selección (README "Usar la aplicación")

| # | Elemento | Estado | Cómo se verificó | Desviaciones / notas |
|---|---|---|---|---|
| 19 | Seleccionar: rectángulo, clic, **Mayús+arrastrar empieza una selección nueva** dentro de otra | hecho | selftest de selección (101 checks, ya existentes) + parity `Shift+drag inside a selection starts a new selection ... nothing moved` | — |
| 20 | Mover arrastrando, 8 asas, proporción forzada si hay texto, un solo paso de deshacer | hecho | selftest de selección (`move`, `resize`, `undo exact`) | — |
| 21 | **Lazo**: trazos con algún punto dentro; textos e imágenes por el **centro** | hecho | parity `the lasso takes a text only when its centre is inside` (ratón por la ruta real; mismo criterio que `inPolygon` de `geometry.ts`) | — |
| 22 | Rotar libre con el asa, Mayús = 15°, **transportador**, ángulo acumulado, espejar arrastrando al otro lado | hecho | selftest de selección (`rotation handle`, `Shift snaps`, `full turn`, `mirror by drag`); la captura `selection_rotate.png` muestra el transportador | El dibujo del transportador no se compara con píxeles de Electron. |
| 23 | **R / Mayús+R / F / Mayús+F** y los botones ⟲ ⟳ ⇋ ⇵ | hecho | parity: tecla real (R no abre el plano, F no abre la figura con algo seleccionado; sin selección F sí es figura, R no hace nada). **Nuevo**: los botones están en la barra (fila de colores) con icono y su consejo | Los botones faltaban en la barra (solo existían las órdenes). |
| 24 | **Ctrl+A / C / X / V / D / Ctrl+↑ / Ctrl+↓** | hecho | parity con teclas reales (`keys: Ctrl+A ... Ctrl+Down`) | — |
| 25 | Pegar desplaza 12 en diagonal en la misma página; repetidos escalonan | hecho | selftest de selección (`paste on the same page offsets by 12`, `a second paste shifts again`) | — |
| 26 | Portapapeles: copiar deja texto plano en el del sistema; un formato propio viaja entre ventanas; Ctrl+V pega una **imagen del sistema** (PNG) | hecho | parity `clipboard: ...` (5 checks; la "otra ventana" es un `QMimeData` con los mismos bytes) | Electron: portapapeles interno; aquí además hay formato propio y texto. **a-probar-por-el-usuario**: copiar en una ventana y pegar en una segunda instancia real. |
| 27 | Espacio vertical (V): debajo se mueve, no se aplasta ni sale de la página | hecho | selftest de selección (7 checks) + cadena de deshacer (parity) | — |
| 28 | Supr / Retroceso borran, Esc deselecciona | hecho | parity (teclas reales) | — |
| 29 | **Brillo**: botón, panel (interruptor, tamaño, intensidad, vista previa) y "aplicar / quitar a la selección" | hecho | **Faltaba por completo**: la app C++ no tenía botón ni panel (solo las órdenes `sel.glowApply/Remove`). Nuevo `app/selection/glowpanel.cpp`: parity `glow: ...` (abre con clic real, controles, aplica glow=10 glowAlpha=0.9, deshace, rehace, quita) y captura `parity_glow_panel.png` | La vista previa se dibuja con el mismo renderizador de trazos de la página. Se guarda en prefs por el flujo normal de estilo. |
| 30 | **Deshacer / rehacer de cada operación** | hecho | parity, dos cadenas con cada operación hecha por su vía real (teclas, ratón) y comparando el documento entero: *selección* (rotar 90°, mover, redimensionar, rotar con asa, duplicar, al frente, al fondo, espejar, empujar, borrar, pegar, cortar, espacio vertical) y *otras* (lápiz, resaltador, forma, goma entera, goma parcial, texto, página nueva, mover página, estilo de papel): deshacer todo da el original exacto y rehacer cada paso da cada estado | — |
| 31 | **Flechas** empujan 1 (10 con Mayús); una ráfaga = un solo paso de deshacer | hecho | parity (`twelve arrow presses in a burst are one undo step`, 1,7 s después son pasos distintos, otra edición en medio corta la ráfaga) | Electron no tiene flechas: es extensión. **Corregido**: cada pulsación de la repetición era un paso. |
| 32 | Seleccionar desde código/pegar/deshacer **lleva la vista al elemento** | hecho | parity `reveal: selecting it from code scrolls it into view` | Solo cuando nada del marco se ve (no mueve la página bajo el lápiz si ya se ve). |
| 33 | La selección **sigue a sus elementos** al deshacer/rehacer y ante cualquier edición de la página | hecho | parity `identity: ...` (7 checks, con trazos idénticos que la huella anterior no distinguía) | **Corregido (e)**: antes se reconocía por huella (nº de puntos, color) y trazos iguales se confundían. Ahora `Editor` informa de cada empalme (`Selection::onSplices`, O(log n) para un trazo nuevo) y las referencias siguen al elemento; la huella queda de guarda para ediciones que no pasan por las primitivas. |
| 34 | Insertar / mover / borrar una página suelta la selección | hecho | parity `structure: ...` | **Error evitado**: seleccionar y pulsar "Insertar página antes" dejaba `page_` apuntando a otra página (lectura fuera de rango al repintar). |
| 35 | Rendimiento: arrastrar 11 033 trazos | hecho | selftest completo: primer movimiento **147 ms** (antes 630 ms), 59 movimientos p50 0,3 ms, soltar 45 ms | El trabajo es del área "pen" (dibuja el mapa de bits en hilos y lo compone); yo solo lo verifiqué. |
| 36 | Asas más grandes con lápiz (×1,5) y dedo (×2,5) | parcial | Implementado (`grab_` según el dispositivo de la última pulsación); la prueba con un `PointerEvent` de lápiz no la escribí | **a-probar-por-el-usuario**: con el lápiz, agarra una asa a ~10 px de distancia. |
| 37 | Reconstrucción de objetos generados tras mover/rotar/espejar | parcial | Solo hay reconstrucción tras **redimensionar** (selftest). La del resto es del área "math" | Lo que necesité del área "math": nada en `selectionimpl.*` salvo que, si añade `swapGenerated` dentro de `quick()` o `finishTransform()`, debe llamar a `setItems(...)` después (mi `onSplices` da por perdidos los elementos sustituidos) y no dejar `own_` en `true` durante el cambio. |

## C. Teclado en conjunto (`app.ts` frente a los atajos de C++)

| # | Elemento | Estado | Cómo se verificó | Desviaciones / notas |
|---|---|---|---|---|
| 38 | Teclas de herramienta P H E B S L G T X V K U C D F N J M | hecho | parity: las 18 por el mapa de atajos real | — |
| 39 | Conflictos: R/F con la selección activa frente a F = figura; T, Q, P, R, F dentro del cuadro de texto y del buscador | hecho | parity `typing p q t r f in the box does not change the tool`, `typing p q h r in the search box goes into it` | Un atajo ambiguo en Qt no dispara nada: parity comprueba que ningún atajo está en dos órdenes activas. |
| 40 | **Re Pág / Av Pág** | hecho | parity `PageDown goes to the next page` / `PageUp goes back`. **Corregido**: `QKeySequence("PageUp")` no es una tecla válida (Qt escribe `PgUp`): el atajo estaba muerto en `mainwindow.cpp` y `chrome.cpp`; el check del área chrome comparaba cadenas y no lo vio. Ahora `QKeySequence(Qt::Key_PageUp)` y un check "todo atajo se interpreta" | — |
| 41 | Ctrl+Inicio/Fin, Ctrl+0, Ctrl++ / Ctrl+= / Ctrl+−, Ctrl+Z, Ctrl+Y, Ctrl+Mayús+Z | hecho | parity (teclas reales) | Zoom ×1,25 (Electron ×1,2). Ctrl++ con un teclado real manda Mayús: Qt lo resuelve por la tecla nativa; mi prueba sintética lo manda sin Mayús. |
| 42 | Ctrl+F abre la búsqueda | hecho | parity `Ctrl+F opens the search bar` | — |
| 43 | **Ctrl+G** | parcial | Abre un diálogo "Ir a la página" (parity lo acepta) | Desviación (corte chrome): Electron enfoca el cuadro de página de la barra. |
| 44 | Los 29 atajos de `app.ts`/`main.ts` están en las mismas órdenes | hecho | parity `keys: the commands carry the shortcuts of the Electron app (29 of 29)` | — |
| 45 | F11, F5, Alt+←/→ | parcial | Solo comprobé que las órdenes llevan el atajo; no los ejecuté (pantalla completa / presentación / historial) | — |
| 46 | Barra espaciadora = mano | n/a | Electron no tiene esa tecla | — |

## D. Barra de herramientas

| # | Elemento | Estado | Cómo se verificó | Desviaciones / notas |
|---|---|---|---|---|
| 47 | Orden de las herramientas = el de Electron | hecho | parity `the tools come in the order of the Electron toolbar` y `no two tools share an order value` | **Corregido (b)**: select y lazo empataban en `order` y salían por orden alfabético (lazo antes que seleccionar). Orden nuevo: select 10, lasso 11, vspace 12, hand 13, laser 25, figure 47. |
| 48 | **Panel de opciones de cada herramienta** (flecha del icono) | hecho | parity `tool options: the panel behind the caret of each tool opens with its controls` (7 paneles) + capturas `parity_options_*.png` | **Corregido, error grave**: los 7 paneles (formas, relleno, plano, 3D, recta, conjuntos, figuras) se abrían **vacíos** (caja de 58×33): un menú pide el widget de un `QWidgetAction` al añadirlo, y aquí se creaba después. Arreglado en `mainwindow.cpp` (quita y vuelve a añadir la acción al crear el widget). Observación: el panel de figuras mide 725 px de alto y no se desplaza; en una pantalla de ≤ 768 px se cortará (**parcial**, no es de este corte). |
| 49 | Puntos de tamaño: clic derecho / Alt+clic guarda el tamaño | hecho | parity `size dots: a real right click stores the current size` | Necesitó el filtro corregido (fila 13). |

## E. Preferencias compartidas (`prefs.json` del Electron)

| # | Elemento | Estado | Cómo se verificó | Desviaciones / notas |
|---|---|---|---|---|
| 50 | Lo que escribe la app C++ **conserva todo** lo que escribió Electron, incluidas las cadenas JSON anidadas (plane, space3, fill, tablet, sizes, toolbox) y una clave desconocida | hecho | parity `prefs: ...` (9 checks, archivo temporal y `XL_PARITY_OUT` para dejar el resultado): las 18 claves siguen, `plane`/`space3`/`fill` byte a byte, `zzFuture` idéntica, tablet/toolbox/sizes siguen siendo texto | — |
| 51 | Un valor que otro (Electron, el área chrome) cambió en disco entre medias **no se revierte** | hecho | parity `a value somebody else changed on disk meanwhile is not reverted` | — |
| 52 | Electron **puede leer** el archivo que escribió C++ | hecho | `node PARIDAD/select_tools/check_prefs.cjs <dir>` (54 comprobaciones): `sanitizePrefs` de `dist/main.js` deja cada clave igual; `sanitizeToolbox`, `sanitizeTablet` y `sanitizeSizes` (bundle esbuild de `src/renderer`) aceptan el contenido sin cambiarlo | Electron reescribe solo con sus claves conocidas (descarta las desconocidas al guardar): eso es de Electron, no de la app C++. |
| 53 | Diseño del cuadro, tamaño del lápiz y presets persisten | hecho | Mismo test: el diseño con zonas se guarda como texto JSON y se reconoce; tamaño 4,25 y la tecla asignada vuelven | No ejercité el cierre y reapertura de la aplicación real con su `%APPDATA%` (nunca lo toco en pruebas). |

## Resumen de estados

53 filas: **hecho 43** · **parcial 7** (4, 5, 13, 36, 37, 43, 45) · **a-probar-por-el-usuario 2** (11, 12; además la parte con dispositivo real de 4, 5 y 36, ya contadas como parcial) · **falta 0** · **n/a 1** (46).

## Qué se corrigió en este corte

1. `bar:*` del cuadro: ids de órdenes que no existían (`tool.recog`, `selection.*`) y `runAction` que trataba `tool.recognizer` como herramienta.
2. Select y lazo empataban en orden; orden de herramientas ya igual al de Electron.
3. **Los paneles de opciones de herramienta salían vacíos** (`mainwindow.cpp`).
4. **Re Pág / Av Pág no funcionaban** (cadena de atajo inválida en `mainwindow.cpp` y `chrome.cpp`).
5. Botones de selección en la barra (cortar, copiar, pegar, duplicar, frente, fondo, rotar ×2, espejar ×2) con iconos propios; iconos de lazo, espacio vertical, "Lápiz y tableta" y "Brillo".
6. **Botón y panel de Brillo** (no existían).
7. La selección sigue a sus elementos por empalmes del `Editor` en vez de por huella; insertar/mover páginas suelta la selección (había una lectura fuera de rango).
8. Las ráfagas de flechas son un solo paso de deshacer; la vista sigue a la selección cuando queda fuera de pantalla; asas con zona de agarre mayor para lápiz y dedo.
9. Filtro de arrastre desde la barra: ahora vale cualquier fila de la ventana.

## Ediciones pequeñas en archivos compartidos

`app/selection.h` (virtual `onSplices` + `ElementSplice`), `app/editor.cpp` (5 llamadas a `selection_->onSplices` en las primitivas de elementos), `app/mainwindow.cpp` (panel de opciones, Re Pág/Av Pág), `app/chrome/chrome.cpp` (Re Pág/Av Pág), `app/settings/controller.cpp` (ids de `kBarActions`, `runAction`, filtro de arrastre), `app/settings/settingsselftest.cpp` (id `sel.rotR`), `app/selftest.cpp` (enganche de `parity`), `app/tools/handlasertool.cpp` y `app/objects/tools.cpp` (valor de `order`), `app/selection/*` (míos).

## Pendiente y por qué

- Arrastre real con ratón/lápiz (filas 11-12) y todo lo de la tableta (4, 5, 36): necesitan el sistema de ventanas real y el dispositivo.
- Panel de figuras demasiado alto para pantallas pequeñas (fila 48) y Ctrl+G con diálogo (fila 43): son de otros cortes.
- Fallo intermitente al **cerrar** el selftest (1 vez de ~12 ejecuciones, `Chrome::eventFilter` leyendo un objeto ya destruido; `%TEMP%\xournal-lab-crash.log`); no lo pude reproducir con el binario final. Lo dejo anotado para el área chrome.

## Cómo repetir la comprobación contra Electron

```
cd G:\xournal-lab\app
node_modules\.bin\esbuild ..\cpp\PARIDAD\select_tools\entry.ts --bundle --platform=node --format=cjs --outfile=..\cpp\PARIDAD\select_tools\ts_modules.cjs
set XL_PARITY_OUT=C:\ruta\salida
set QT_QPA_PLATFORM=offscreen
set XL_SELFTEST_ONLY=parity
..\cpp\build\xl_app.exe --selftest --log parity.log
node ..\cpp\PARIDAD\select_tools\check_prefs.cjs C:\ruta\salida
```
