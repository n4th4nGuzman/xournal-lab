# Paridad con Xournal++ (original): funciones, menús, barras y opciones

Objetivo: que nuestra app se organice como el Xournal++ original (barra de colores a un lado, configuración del lápiz, cuadro de herramientas, selector de capa...)
**sin quitar nada de lo que ya tenemos**: solo reorganizar, reubicar y añadir. Xournal++ es GPL: aquí se replica qué hace, dónde está y cómo se comporta;
no se copia su código. Fuentes consultadas: el manual y el código fuente público de xournalpp (`src/core/model/StrokeStyle.cpp`, `LineStyle.cpp`,
`control/xojfile/SaveHandler.cpp`, `view/StrokeViewHelper.cpp`; menús y barras de `ui/main.glade` / `ToolbarDefinition`), y las capturas de la versión de Windows
que aportó el usuario (barra de colores a la izquierda, barra de herramientas, cuadro flotante, menú de estilos de línea del lápiz).

Estados: **ya-tenemos** (existía antes de este trabajo; se indica dónde) · **añadido** (hecho ahora; se indica dónde y la prueba) ·
**parcial** (hay una parte; se dice cuál falta) · **no-aplica** (no tiene sentido aquí; razón) · **falta** (no hecho; razón).
Las pruebas `xpp ...` son de `app/chrome/selftest_xpp.cpp` (dentro de `xl_app --selftest`).

## 1. Barra de colores y botones de vista (captura a)

| # | Elemento del original | Estado | Dónde / prueba | Nota |
|---|---|---|---|---|
| 1 | Columna vertical de colores (negro, azul claro, verde, verde claro, azul, gris, rojo, magenta, naranja, amarillo, blanco) | añadido | `MainWindow::setColorSidebar`, `xpp colour sidebar:*` | Usa la paleta en uso (la de Preferencias), no una fija: con la paleta por defecto salen los 5 colores de siempre. Las 11 de la captura se obtienen eligiendo esa paleta. |
| 2 | Cuadro de color personalizado | añadido | idem, `customColor` | El mismo botón que ya había en la fila de estilo. |
| 3 | Botones de vista bajo los colores: dos páginas, presentación, zoom 100 %, ajustar, alejar, acercar | añadido | idem, `view.twoPages/presentation/zoom100/fitWidth/zoomOut/zoomIn` | Son las mismas acciones (mismos atajos) que los menús. |
| 4 | Se puede mostrar/ocultar | añadido | Ver > Barra de colores; `MainWindow::setColorSidebar(bool)` | Gancho para Preferencias: `setColorSidebar`, `setColorSidebarRight`, señal `colorSidebarChanged`. Se recuerda en `settings()` ("ui/colorSidebar", "ui/colorSidebarRight") solo si `persistence()`; por defecto: activada, a la izquierda. |
| 5 | Zurdos: a la derecha | añadido | Ver > Barra de colores a la derecha; `setColorSidebarRight` | El original solo la tiene a la izquierda (o flotante); esto es un añadido. |
| 6 | Sin duplicar colores | añadido | `xpp colour sidebar: one set of dots` | Con la barra lateral activada los puntos salen de la fila superior (mismas acciones, un solo juego). Desactivada, vuelven. |
| 7 | Respeta el tema (clásico / neon) | añadido | capturas `sidebar_left_*` | Mismo pintor de colores que la fila superior. |
| 8 | Modo presentación la oculta y la restaura | añadido | `xpp colour sidebar: presentation mode...` | |

## 2. Menús

| # | Menú / entrada del original | Estado | Dónde | Nota |
|---|---|---|---|---|
| 1 | Archivo: Nuevo, Abrir, Abrir reciente, Guardar, Guardar como | ya-tenemos | `mainwindow.cpp` | |
| 2 | Archivo: Nuevo desde plantilla | falta | | Xournal++ guarda plantillas de página; aquí hay "tipos de papel" y la bienvenida con tarjetas. No hay plantillas de usuario. |
| 3 | Archivo: Exportar como PDF / como PNG | ya-tenemos | `app/pdf` (`file.exportPdf`, `file.exportAs`) | PDF vectorial y PNG con rangos de páginas. |
| 4 | Archivo: Exportar como SVG / .xoj / HTML | falta | | Solo PDF y PNG. SVG no está en Qt Gui sin el módulo Qt SVG, que esta compilación no lleva. |
| 5 | Archivo: Imprimir | ya-tenemos | `file.print` | |
| 6 | Archivo: Guardar copia, Abrir copia de recuperación, Cerrar, Salir | ya-tenemos (superconjunto) | | El original no tiene "copia de recuperación" como entrada. |
| 7 | Editar: Deshacer, Rehacer, Cortar, Copiar, Pegar, Eliminar, Buscar | ya-tenemos | | |
| 8 | Editar: Mover selección arriba/abajo, al frente/atrás | ya-tenemos (en parte) | `sel.front`, `sel.back` | Orden de apilado sí. "Mover a la capa de arriba/abajo": falta (se cambia de capa cortando y pegando). |
| 9 | Editar: Preferencias | ya-tenemos | `edit.prefs` | Se reorganiza aparte. |
| 10 | Ver: Páginas emparejadas | añadido | Ver > Dos páginas, `DocumentView::setPairedPages`, `xpp two pages:*` | Filas de dos páginas (1-2, 3-4...), cada una con su tamaño, ajustar al ancho y a la página cuentan el par, la rueda pasa de par en par en la vista de una pantalla. Falta la opción "primera página sola". |
| 11 | Ver: Modo presentación | ya-tenemos | `Chrome::setPresenting` (F5) | |
| 12 | Ver: Pantalla completa | ya-tenemos + añadido botón | F11; botón en la fila 1 (`view.fullscreen`) | |
| 13 | Ver: Zoom (acercar, alejar, 100 %, ajustar al ancho, ajustar a la página) | ya-tenemos | | |
| 14 | Ver: Mostrar barras de herramientas / barra de menú / barra lateral | ya-tenemos | `view.toolbars`, `view.menubar`, `view.sidebar` | |
| 15 | Ver: vista continua / de una página | ya-tenemos | `view.continuous` | |
| 16 | Navegación: primera, anterior, siguiente, última, ir a la página | ya-tenemos | | |
| 17 | Navegación: página siguiente / anterior con anotaciones | añadido | `nav.nextAnnotated/prevAnnotated`, Ctrl+Alt+Re Pág / Av Pág | Sin prueba propia del recorrido (solo la existencia de la acción). |
| 18 | Navegación: siguiente / anterior capa | parcial | selector de capa | Se elige la capa en el selector de la fila 1; sin atajo de teclado de "capa siguiente". |
| 19 | Diario (Journal): nueva página antes/después/al final, eliminar, mover | ya-tenemos | | |
| 20 | Diario: formato de papel, color de fondo, aplicar a todas | ya-tenemos | | |
| 21 | Diario: tipos de fondo liso, rayado, cuadriculado, puntos | ya-tenemos | `journal.paper:*` | Además tipos especiales propios (`xlPaper`). |
| 22 | Diario: fondos isométricos (puntos / cuadrícula) | añadido | `journal.paper:isodotted/isograph`, `core/paper.cpp` | Se guardan con el valor `style` que usa Xournal++. El espaciado exacto del original no está documentado: aproximado. La app Electron los verá como papel liso (estilo desconocido = liso). |
| 23 | Diario: cargar fondo PDF | ya-tenemos | `file.openPdf` | |
| 24 | Herramientas: lápiz, goma, resaltador, texto, imagen, seleccionar (rectángulo, región, objeto), espacio vertical, mano | ya-tenemos | `tools/`, `selection/`, `texttools/` | |
| 25 | Herramientas: formas (rectángulo, elipse, línea, flecha) | ya-tenemos | `texttools/shapetool.cpp` | |
| 26 | Herramientas: flecha doble, sistema de coordenadas | añadido | `ShapeKind::DoubleArrow/Axes`, `xpp shapes:*` | Menú de la herramienta Formas. El "sistema de coordenadas" del original es un L con flechas; para gráficas con rango, marcas y cuadrícula ya existe el plano cartesiano. |
| 27 | Herramientas: spline / curva guiada | falta | | Sin especificación clara de su comportamiento; la mano alzada con el reconocedor cubre el uso. |
| 28 | Herramientas: reconocedor de formas | ya-tenemos | `tool.recognizer` | |
| 29 | Herramientas: regla (línea recta guiada) | parcial | Formas > Línea (Mayús = ángulos de 15°) | No hay una regla/escuadra dibujada en la página sobre la que apoyar el lápiz. |
| 30 | Herramientas: transportador, escuadra de geometría (Geodreieck) y compás | falta | | El original los pinta y deja arrastrarlos. Aquí solo hay transportador de rotación en la selección. Pendiente: necesita una capa de superposición interactiva propia (fuera de la ruta del lápiz) y no se hizo. |
| 31 | Herramientas: puntero láser | ya-tenemos | `handlasertool.cpp` | |
| 32 | Herramientas: reproducir objeto (audio) / grabar audio | no-aplica | | Ver sección 6: Qt6 Multimedia no está en esta compilación y el formato guarda marcas de tiempo + audio; los elementos `<audio>`/`<timestamp>` de un archivo se conservan intactos al guardar. |
| 33 | Herramientas: seleccionar texto de un PDF / resaltar texto del PDF | falta | | Requiere la capa de texto de Qt Pdf (no se usa). |
| 34 | Plugins | no-aplica | menú Plugins (informativo) | Xournal++ carga plugins Lua; esta app no. |
| 35 | Ayuda: manual, acerca de | ya-tenemos | | |

## 3. Barra de herramientas (captura b)

| # | Botón / control del original | Estado | Dónde |
|---|---|---|---|
| 1 | Guardar, abrir (y nuevo) | ya-tenemos | fila 1 (Nuevo está en Archivo y en la bienvenida, no en la barra: ver `ui.md`) |
| 2 | Exportar PDF, imprimir | ya-tenemos | Archivo (no tienen botón propio) |
| 3 | Cortar, copiar, pegar | ya-tenemos | fila 1 |
| 4 | Imagen, forma/texto | ya-tenemos | fila 2 |
| 5 | Lápiz con menú desplegable (grosores, estilos de línea, relleno) | añadido | `setToolMenu("pen")`, `xpp pen dropdown:*` |
| 6 | Goma con menú (tamaños, tipos) | añadido | `xpp eraser dropdown:*` |
| 7 | Resaltador con menú (tamaños, opacidad, relleno) | añadido | `xpp highlighter dropdown:*` |
| 8 | Herramientas de selección | ya-tenemos | fila 2 |
| 9 | Espacio vertical, mano | ya-tenemos | fila 2 |
| 10 | Menú desplegable de formas | ya-tenemos | caret de Formas (4 + 2 nuevas) |
| 11 | Regla / giro (transportador) | parcial | ver Menús #29-30 |
| 12 | Audio | no-aplica | ver sección 6 |
| 13 | Caja "Página [1] de 1", navegación | ya-tenemos | fila 1 |
| 14 | Menú de nueva página (antes, después, al final) y borrar página | ya-tenemos | fila 1 (tres botones) |
| 15 | Selector de capa "Capa Capa 1 ▾" | añadido | `makeLayerSelector`, `xpp layers:*` | Elegir capa, ocultar/mostrar, nueva, renombrar, eliminar; el panel de capas sigue como estaba. |
| 16 | Pantalla completa | añadido | botón en la fila 1 (F11) |
| 17 | Dos filas con iconos neon | se mantiene | |

## 4. Cuadro flotante (captura c)

| # | Elemento | Estado | Nota |
|---|---|---|---|
| 1 | Ventanita flotante con lápiz, goma, resaltador, regla, deshacer, zoom... | ya-tenemos (distinto) | `tool.toolbox` (tecla Q o botón 1 del lápiz): el cuadro de herramientas de `settings/toolboxwidget.cpp`, que se edita aparte. No se tocó para no pisar ese trabajo; los desplegables del lápiz/goma/resaltador de este trabajo no aparecen dentro del cuadro. |
| 2 | Círculo / flecha / línea / ejes / sigma | ya-tenemos | el cuadro tiene sus propias herramientas editables; flecha doble y ejes están en el menú de Formas pero no en el cuadro por defecto. |
| 3 | Control de zoom del cuadro | falta | |
| 4 | Puntos de tamaño y colores en el cuadro | ya-tenemos | |

## 5. Lápiz: estilo de línea, relleno, goma, resaltador (captura d)

| # | Opción del original | Estado | Dónde / prueba | Nota |
|---|---|---|---|---|
| 1 | Estilo estándar (continuo) | añadido | `xpp pen: the standard style writes no style attribute` | No escribe atributo: los archivos no cambian. |
| 2 | Discontinua | añadido | `style="dash"`, patrón 6/3 en unidades de página | La app dibuja y exporta (PNG/PDF) el patrón: `xpp line style:*`. El trazo que se está dibujando en vivo se ve continuo hasta que se levanta el lápiz (no se tocó la ruta del lápiz). |
| 3 | Discontinua / punteada | añadido | `style="dashdot"`, 6/3/0,5/3 | idem |
| 4 | Punteada | añadido | `style="dot"`, 0,5/3 | idem |
| 5 | Patrón personalizado `cust: a b c` | añadido (lectura) | `dashOf` en `core/render_stroke.cpp`, `xpp line style: ...custom` | Se dibuja si un archivo lo trae; la interfaz no lo crea. Un patrón mal formado se dibuja continuo. |
| 6 | Relleno del trazo (`fill`) | añadido | `fill` en el trazo, `render_stroke.cpp`, menú del lápiz (0 / 25 / 50 / 75 %) | Rellena el polígono que el trazo dibuja, con el color del trazo y esa transparencia. |
| 7 | Estilo y relleno para formas | añadido | formas usan el estilo y el relleno del lápiz | |
| 8 | Grosores (muy fino ... muy grueso) | ya-tenemos | presets de tamaño + menú nuevo | |
| 9 | Resaltador: tamaños, opacidad, relleno | añadido (opacidad) | menú del resaltador | Atributo de color con alfa, como ya hacía. |
| 10 | Goma: tamaños fino / medio / grueso | ya-tenemos + menú | | |
| 11 | Tipos de goma: estándar (parcial), borrar trazo | ya-tenemos | dos herramientas (`eraser`, `eraserPart`), el menú permite pasar de una a otra | |
| 12 | Goma "blanqueadora" (whiteout) | falta | | Escribe trazos blancos opacos; el visor no los replica fiel ("erasing strokes are not replayed" en `render_stroke.cpp`). Requeriría tocar el contrato del renderizador. |
| 13 | Presión del lápiz, suavizado | ya-tenemos | `tools/inktool.cpp`, Lápiz y tableta | |
| 14 | Configuración del lápiz y la tableta | ya-tenemos | caret > "Lápiz y tableta…" | Sigue a un clic: ahora dentro del desplegable del lápiz. |

## 6. Otras funciones del original

| # | Función | Estado | Nota |
|---|---|---|---|
| 1 | Capas: nueva, eliminar, renombrar, ocultar, orden | ya-tenemos | panel + selector |
| 2 | Capas: seleccionar entre capas, mover selección a otra capa | falta | |
| 3 | Fondos de página: liso, rayado, cuadriculado, puntos | ya-tenemos | |
| 4 | Fondo PDF y de imagen | ya-tenemos | |
| 5 | Audio: grabar mientras se escribe y reproducir al tocar un trazo | no-aplica | Pendiente de decisión: Qt6 Multimedia no está instalado en esta compilación (MSYS2 `qt6-multimedia`), la reproducción por trazo exige el visor de marcas de tiempo de cada trazo y un dispositivo de audio que no se puede probar sin micrófono. Los `<timestamp>`/`<audio>` se conservan al guardar (`RawNode`). |
| 6 | Texto: fuente, tamaño, color, negrita, editor in situ | ya-tenemos | `texttools` (con Markdown) |
| 7 | LaTeX | ya-tenemos | |
| 8 | Insertar imagen (archivo, portapapeles, arrastrar) | ya-tenemos | |
| 9 | Ayudas de dibujo: reconocedor, Mayús en formas | ya-tenemos | |
| 10 | Zoom: 100 %, ajustar ancho/página, rueda, pellizco | ya-tenemos | |
| 11 | Exportar e imprimir | ya-tenemos | |
| 12 | Autoguardado | ya-tenemos | |
| 13 | Página de bienvenida, temas | ya-tenemos (propios) | |

## Recuento

91 filas: **ya-tenemos 50 · añadido 26 · parcial 3 · no-aplica 4 · falta 8** (una fila con "ya-tenemos + añadido" cuenta en la primera palabra que aparece).

Pruebas: `xl_app --selftest` pasa 1461 / 0 fallos (76 son `xpp ...`); ctest 25/25. Ida y vuelta con la app Electron
(`tools/interchange/run_interchange.ps1 -Build build-side2 -Extra PARIDAD/xpp_shots/xpp_style.xopp`): el archivo con `style` (dash, dashdot, dot,
`cust:`), `fill` y fondo isométrico da el mismo volcado en C++ y en Electron y sobrevive a que cada app lo vuelva a guardar (IDENTICAL en los tres
sentidos). Eso comprueba que Electron conserva los atributos; no que Electron los **dibuje**: su renderizador de trazos no se tocó (se ve el dash/dot si lo
implementa; `fill`, `cust:` y los fondos isométricos probablemente los muestre lisos).

Capturas en `PARIDAD/xpp_shots/`: `before_*` (antes), `cpp_neon_1920.png` / `cpp_classic_1920.png` (después, con la barra de colores), `sidebar_left_*`,
`sidebar_right_classic.png`, `two_pages.png`, `pen_dropdown.png` (comparar con la captura d del usuario), `eraser_dropdown.png`, `highlighter_dropdown.png`,
`cpp_styles_file.png` (los cuatro estilos, el relleno y el fondo isométrico dibujados desde un archivo en formato Xournal++).

## Sin verificar (decirlo claro)

- Lápiz físico: todo se probó con eventos sintéticos de ratón/lápiz; el trazo en vivo con estilo discontinuo se ve continuo hasta que se levanta el lápiz.
- Xournal++ oficial abriendo nuestros archivos: no hay una copia instalada aquí. Los atributos (`style="dash|dashdot|dot"`, `fill`) son los que escribe su
  `SaveHandler`, pero no se comprobó que los dibuje igual (p. ej. el largo exacto de las rayas con trazos gruesos).
- Pantalla de alta densidad (DPI > 100 %) y varios monitores: la disposición de dos páginas y la barra lateral solo se miraron a escala 1.
- Aspecto en Windows real de los menús desplegables: capturas con la plataforma `offscreen` (que dibuja con las mismas hojas de estilo pero sin el
  gestor de ventanas); la pulsación larga sobre el botón del lápiz **no** abre el menú (a propósito, ver abajo).
- La paleta de la barra de colores es la que el usuario tiene en Preferencias (cinco puntos por defecto), no los once de la captura.

## Decisiones

- El menú del lápiz/goma/resaltador se abre con la esquina (caret), no con pulsación larga: un `QToolButton` con menú lo abre tras mantener pulsado y eso rompía
  el arrastre del editor de la caja de herramientas y cualquier pulsación sostenida del lápiz.
- El selector de capa y el botón de pantalla completa están al final de la fila de herramientas, no en la fila 1: la fila 1 ya no cabía a 1400 px en el tamaño normal
  de iconos y las barras habrían pasado al tamaño compacto.
- Cambio deliberado en pruebas: `pen button: a click on its arrow opens ...` ahora abre el desplegable del lápiz; la entrada "Lápiz y tableta…" abre el diálogo
  (antes el caret lo abría directamente). Se mantiene a un clic más.
