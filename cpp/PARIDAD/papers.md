# Paridad C++ / Electron: papeles y fondos, paneles de opciones, relleno y barra

Auditoría de papeles. Fuente de verdad: `G:\xournal-lab\README.md` («Papel con la paleta», «Papeles para matemáticas y física», «Relleno», «Barra de herramientas») y `app/src/renderer/{paper,render,journal,welcome,palettes,app}.ts`.

Estados: **hecho** = lo ejecuté; **parcial** = existe con una diferencia explicada; **falta**; **a-probar-por-el-usuario** = necesita algo que no tengo (lápiz real, segundo monitor, Xournal++ oficial, una pantalla de 768 px real).

## Cómo se comparó (papeles)

`tools/paperparity/` carga el **código de papel de Electron** (`paper.ts`, `render.ts`, `journal.ts`, `serialize.ts`) en una ventana oculta de Electron: escribe, para cada papel, el `.xopp` que escribe Electron y el PNG que dibuja su renderizador (canvas). Luego el renderizador C++ (`xl_render_page`) abre **ese mismo `.xopp`** y se compara píxel a píxel. 85 casos (los 5 estilos clásicos sobre blanco y oscuro, y cada opción de cada papel especial; las opciones por defecto además sobre papel oscuro `#120a26`) a 2x, y 27 de ellos a 1x guardados como oro en `tests/data/paper_parity/` (`ctest test_paper_parity`).

## 1. Papeles y fondos

| # | Ítem | Estado | Evidencia | Desviaciones |
|---|---|---|---|---|
| 1.1 | Catálogo: 7 papeles especiales (mm, cart, trig, polar, log, iso, cornell): id, etiqueta, `style` de reserva, cada parámetro, cada opción, su orden (el primero = por defecto) | hecho | `test_paper_parity` compara el catálogo C++ con el JSON que vuelca Electron (`kinds.json`): idéntico | — |
| 1.2 | Estilos clásicos liso/rayado (con margen)/reglado/cuadriculado/puntos | hecho | PNG de Electron vs C++, blanco y oscuro: 0 píxeles con diferencia > 24 en liso, rayado y reglado; cuadriculado y puntos sólo anti-aliasing de línea de 0,5 pt (max 34/255) | — |
| 1.3 | Milimetrado | hecho | max 31/255 por grosor de línea sub-píxel (Skia vs Qt), sin píxeles «lejanos» | — |
| 1.4 | Plano cartesiano: 24 combinaciones (ejes 4/1.er cuadrante x unidad 0,5/1/2 x subdivisión x números) | hecho | las 24 renderizadas y comparadas; geometría igual, restos = bordes de glifos | — |
| 1.5 | Trigonométrico (π/6, π/4) | hecho | 2 opciones, blanco y oscuro | — |
| 1.6 | Polar (ángulo 10/15/30 x anillos 1/0,5) | hecho | 6 combinaciones + oscuro | — |
| 1.7 | Isométrico (líneas/puntos x lado 0,5/1/2) | hecho | 6 combinaciones + oscuro: 0 píxeles lejanos | — |
| 1.8 | Cornell | hecho | claro y oscuro | — |
| 1.9 | **Logarítmico** (base 10/e x semilog/log-log x 1-7 ciclos = 28 combinaciones) | hecho, con diferencia **intencionada** | las 28 comparadas | **Bug de Electron, no copiado**: en `drawLog` el `else` de `if (both) for … for … if (s === strength) {…} else …` se une al `if` interior; en log-log las líneas menores verticales salen como 50 líneas lineales y en semilog no hay líneas menores verticales. C++ dibuja lo que dice el README (menores 2…9 en x también en log-log; 50 subdivisiones en semilog). Por eso `test_paper_parity` no compara log píxel a píxel; las comprobaciones de log siguen en `tests/render.cpp` y el selftest. Decisión pendiente tuya: arreglar Electron (no lo toqué) o copiar el defecto |
| 1.10 | Color de la tinta del papel (claras sobre fondo oscuro) | hecho | `_d` de cada papel por defecto comparados | — |
| 1.11 | Texto de los papeles (rótulos, números, «Tema:», π/2…) | hecho (corregido) | **Hallazgo**: la línea base «middle» de C++ caía 1 pt más abajo que en Chromium (usaba `(ascent-descent)/2` de Qt). Calibrado con las capturas de Electron (0,27 em): el error medio de los rótulos baja de 1,78 a 0,55 (Cornell), 1,46 a 0,69 (cart), 4,5 a 1,5 (log) | Los glifos nunca son idénticos al píxel (Skia vs Qt, sugerencia de fuente). Fuente: «Segoe UI» fija en C++ y `system-ui` en Electron; son la misma en Windows, no en Linux/macOS |
| 1.12 | Atributos: `xlPaper="kind;k=v;…"` + `style` de reserva; un papel clásico quita `xlPaper` | hecho | Archivos de Electron cargados por C++: `xlPaper` ida y vuelta por `parseSpec/formatSpec` y `style` = reserva, en 27 casos; selftest `lifecycle`: los 7 papeles guardan y reabren | — |
| 1.13 | Valores inválidos de `xlPaper` caen al valor por defecto, tipo desconocido = sin papel | hecho | `tests/robust_render.cpp` (valores extremos, basura) | — |
| 1.14 | Color de papel con la paleta (9 paletas; `papers` primero, luego las tintas) | hecho | comparados a mano los 9 conjuntos de `palettes.ts` con `core/palettes.cpp`: iguales; selftest chrome (muestras) | — |
| 1.15 | Selector: grupo «Matemáticas y física», opciones al lado, un paso de deshacer, «todas las páginas» | hecho | selftest chrome (`paper: …`) y `lifecycle` | Páginas con fondo PDF/imagen: C++ no cambia su `style`/`xlPaper` (Electron sí lo escribiría en el `<background>` del PDF); decisión más segura, anotada |
| 1.16 | Menú Diario → Fondo de página → Tipo de papel | hecho (corregido) | `app/mainwindow.cpp`: elegir en el menú el papel que la página ya tiene **conservaba** las opciones en Electron (`choosePaper`) y C++ las reponía por defecto; ahora igual | — |
| 1.17 | Bienvenida: 11 tarjetas con miniatura real, un clic crea documento con ese papel | hecho | selftest chrome (miniaturas, `xlPaper` de log) | — |
| 1.18 | Formato de papel (A3, A4, A5, Carta, Legal, personalizado, girar) | hecho | selftest chrome | — |
| 1.19 | Cómo abre Xournal++ oficial un fondo con `xlPaper` | a-probar-por-el-usuario | no tengo Xournal++ | El README ya lo dice |
| 1.20 | Interoperabilidad C++ → Electron de los 7 papeles | parcial | `xl_qa_gen` escribe los 7 y `tools/interchange` los lee con el `parseXopp` de Electron; no volví a ejecutar esa batería en esta pasada | — |

## 2. Paneles de opciones (popups del caret) en una pantalla de 768 px

Medidos con las ventanas reales (`XL_SELFTEST_ONLY=panelfit`, 45 comprobaciones nuevas junto con la barra): figuras 715 px (ventana 725), venn 600, plano 511, línea numérica 441, relleno 470, formas 180, espacio 3D 817 px de **ancho** x 400.

| # | Ítem | Estado | Evidencia |
|---|---|---|---|
| 2.1 | Todo panel va dentro de `ScrollingPanel` (`app/optionspanel.*`): tamaño del panel hasta el hueco que queda bajo el botón en su pantalla; más allá, barra de desplazamiento vertical u horizontal | hecho | `panelfit`: los 7 paneles abren dentro de la pantalla de prueba (800 x 800) y, con la pantalla simulada de 768, la de figuras queda en 690 px con scroll; el 3D (817 px) en 790 con scroll horizontal; al desplazar al final el último control es alcanzable |
| 2.2 | Reabrir tras mover la ventana recalcula (un `QMenu` conserva su tamaño) | hecho | el menú se vuelve a pedir al cambiar el máximo (verificado en la segunda pasada de `panelfit`) |
| 2.3 | Diálogos de Preferencias y Tableta | hecho | mínimos 470 y 142 px de alto (Tableta ya tenía scroll) |
| 2.4 | Diálogo LaTeX (700 px de alto fijo) | hecho (corregido) | ahora `min(700, alto disponible − 90)`; no verificado a mano en una pantalla de 768 real |
| 2.5 | Otros paneles (barra de papel, laterales, hoja flotante de fórmulas) | no auditado | la barra de papel sólo tiene una fila; los laterales son docks con scroll propio |

Límite: el selftest usa la plataforma `offscreen`; la barra de tareas y la decoración real de Windows no existen allí (se descuentan 20 px de margen).

## 3. Relleno (cubo) con una página densa

| # | Ítem | Estado | Evidencia |
|---|---|---|---|
| 3.1 | Un clic sobre un círculo cerrado entre 8000 trazos cortos | hecho | `test_fill_window`: ventana alrededor del clic: 7 ms (escala 2), 11 ms (3), 17 ms (4) frente a 79 / 133 / 207 ms de toda la trama (11 a 12 veces); con 5000 trazos de `test_fill`: 50 → 13 ms. El 275 ms del enunciado no lo reproduje tal cual (otra máquina y otra página): lo que mido es la proporción |
| 3.2 | Resultado idéntico | hecho | 92 rellenos (8 formas: círculo pequeño/mediano/grande mayor que la primera ventana, cajas en las esquinas y bordes, hueco de 6 pt que el imán cierra, hueco de 30 pt que no; clic en el centro, a 2 px de la pared y disperso; con y sin imán; escalas 2 y 4): máscara, caja, recuento, `gapUsed` y `moved` idénticos a la trama completa (`windowed = false`); las 100 comprobaciones previas de `test_fill` siguen pasando |
| 3.3 | Cómo | hecho | clic: la pared se dibuja sólo con los trazos cuya caja toca una ventana de ~7 cm alrededor del clic (índice de cajas), la inundación se hace en la ventana; si el relleno se acerca a un borde de la ventana que no es el de la página (franja = 2·radio máx + sondas) la ventana crece x3 y, al final, es la trama entera (= algoritmo anterior). Las paredes son exactas porque se pintan en la misma posición de dispositivo |
| 3.4 | Línea arrastrada y rectángulos de Riemann | no tocado | siguen calculando toda la trama (no era la consulta) |
| 3.5 | Formas que se escapan (abiertas) | igual que antes | se prueban ventanas crecientes y al final la trama completa: coste algo mayor (~1,3x) en ese caso |

## 4. Barra de herramientas por debajo de ~1100 px

| # | Ítem | Estado | Evidencia |
|---|---|---|---|
| 4.1 | **Hallazgo**: el chevron de desbordamiento (`qt_toolbar_ext_button`) quedaba **17 px fuera de la barra** (la regla `QToolButton{min-width:26px;…}` lo hacía de 34 px y Qt reserva 17), así que a 900 px o menos la fila de herramientas se cortaba **sin chevron visible** | hecho | regla `QToolButton#qt_toolbar_ext_button` en `app/chrome/theme.cpp`; `toolbarfit`: el chevron queda dentro de la barra a 700 px |
| 4.2 | Tres tamaños: completo (iconos 24) / compacto (20) / muy compacto (18), elegidos por ancho de ventana; las dos filas comparten línea sólo a tamaño completo | hecho | `MainWindow::arrangeToolbars/applyToolbarTier/measureToolbarTiers`; hint de la fila de herramientas 1091 / 883 / 779 px: caben las 26 botones en 1100, 1024, 900 y 800 px (comprobado) y a 700 px la fila de herramientas entra y la de estilo usa el chevron (a la vista, capturas) |
| 4.3 | Medición robusta (se vuelve a medir si cambian los botones o el estilo) | hecho | firma por número de acciones; un fallo previo (medida vieja de la fila de estilo) lo encontró el propio selftest |
| 4.4 | Fila de navegación (la del módulo chrome) | parcial | recibe el mismo tamaño pero no cuenta para decidir el tamaño; cabe a 700 px |

## 5. Resultados de la regresión

- `ctest` (`build-w4`): **25 de 25** (23 de partida + `paper_parity` + `fill_window`).
- `--selftest` completo: **1145 PASS, 0 FAIL** (1100 de partida + `panelfit` y `toolbarfit`).
- Sin verificar: lápiz o pantalla táctil reales, segundo monitor y escala de pantalla distinta, Xournal++ oficial, una pantalla de 768 px física (la altura se simuló), Qt en Linux/macOS (la fuente de los rótulos de los papeles), `tools/interchange` en esta pasada.

## 6. Archivos tocados

Nuevos: `core` no cambia salvo `paper.cpp` (línea base de rótulos) y `fill.{h,cpp}` (ventana en el clic; `computeFill` gana un parámetro por defecto `windowed = true`); `app/optionspanel.{h,cpp}`, `app/panelfit_selftest.cpp`, `tests/paper_parity.cpp`, `tests/fill_window.cpp`, `tests/data/paper_parity/`, `tools/paperparity/` (puente con Electron, sólo lectura sobre su código).
Compartidos, ediciones pequeñas: `app/mainwindow.{h,cpp}` (panel con scroll, menú que conserva opciones, tamaños de barra), `app/chrome/theme.cpp` (chevron y reglas de tamaño), `app/texttools/latextool.cpp` (alto del diálogo), `app/selftest.cpp` (dos áreas nuevas).
