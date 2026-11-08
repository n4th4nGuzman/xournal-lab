# Paridad: Preferencias de Xournal++ (prefs_xpp)

Referencia: el código abierto de Xournal++ (`src/core/gui/dialog/SettingsDialog.cpp`, `ui/settings.glade`, `src/core/control/settings/Settings.cpp`), leído en un clon fuera del repositorio. Es GPL: aquí solo se replica el **conjunto de opciones, nombres, agrupación y valores predeterminados como comportamiento observable**; no se copió código. Capturas de nuestra ventana en `PARIDAD/prefs_shots/` (`p1.png` ... `p12.png` son la ventana completa, `p<N>_top0.png` el diálogo; N = página: 1 Cargar/guardar, 2 Sistema de entrada, 3 Ratón, 4 Lápiz, 5 Pantalla táctil, 6 Ver, 7 Escala, 8 Área de dibujo, 9 Valores predeterminados, 10 Grabación de audio, 11 LaTeX, 12 Idioma; se regeneran con `xl_app --uishot out.png --size 1400x900 --theme classic --open prefs:N`).

Estados:
- **ya-tenemos**: ya existía en la app; se reubicó donde lo pondría el original.
- **añadido**: no existía; ahora está conectado a un comportamiento (el selftest `prefsselftest.cpp` lo ejercita).
- **adaptado**: existe con otra forma o valor predeterminado (se explica).
- **no-aplica**: no tiene sentido en Windows/Qt o en esta app; el control se ve **desactivado con el motivo escrito debajo** («No disponible: …»).
- **falta**: se podría hacer y no está hecho; el control se ve desactivado con el motivo.

Todos los ajustes nuevos viven en **un único objeto aditivo `xpp` dentro de `prefs.json`** (`core/xpppref.*`). La app Electron lo ignora al leer y lo descarta al escribir el archivo ella misma (igual que toda clave que no conoce: su `sanitizePrefs` no copia claves ajenas); esta app C++ conserva las claves desconocidas y no escribe `xpp` mientras todo esté en sus valores predeterminados (un `prefs.json` sin tocar se reescribe idéntico). Las copias de seguridad no cambian.

**Recuento** (109 filas; una fila agrupa los controles de un mismo grupo del original cuando comparten estado): añadido 52 · adaptado 16 · no-aplica 15 · falta 15 · ya-tenemos 11. Las 12 páginas, los 12 grupos de «Ver» y todos los controles del original están en el diálogo; los 30 de estado no-aplica/falta salen desactivados con su motivo (`PrefsDialog::disabledKeys()`, comprobado por el selftest).

## Cargar/guardar

| Original (etiqueta) | Clave | Pred. original | Significado | Estado | Dónde / por qué |
|---|---|---|---|---|---|
| Activar el autoguardado | autosaveEnabled | sí | autoguardado periódico | ya-tenemos | `Prefs::autosaveMinutes > 0` (casilla + intervalo); pred. de la app: desactivado (se conserva lo que había) |
| Cada N minutos | autosaveTimeout | 3 | intervalo | adaptado | `xpp.autosaveInterval` 1..120, se recuerda aunque se desactive; la copia va aparte, nunca sobre el archivo |
| Nombre predeterminado al guardar | defaultSaveName | `%F-Note-%H-%M` | nombre propuesto para un documento nuevo | añadido | `xpp.saveName`, pred. `%F-Nota-%H-%M`; `MainWindow::suggestedSaveName` |
| Marcadores disponibles (%a %A %b %B %c %d %F %H %I %j %m %M %p %S %U %w %W %x %X %y %Y %Z %%) | — | — | sustituciones strftime, caracteres ilegales → `_` | añadido | `expandNameTemplate` (días y meses en español) |
| Nombre predeterminado al exportar a PDF | defaultPdfExportName | `%{name}_annotated` | nombre propuesto en «Exportar como PDF» | añadido | `xpp.pdfName`, pred. `%{name}_anotado`; con `%{name}` (PDF de fondo, o el nombre del documento si no hay) y `%{file}` |
| Cargar automáticamente los cuadernos | autoloadPdfXoj | sí | al abrir un PDF abre el .xopp que está junto a él | añadido | solo en «Anotar PDF…» (no al abrir un PDF por otras vías) |
| Cargar automáticamente el archivo más reciente al iniciar | autoloadMostRecent | no | arranca con el último archivo | añadido | `SettingsController::autoloadMostRecent`; solo en ejecución real y sin archivo en la línea de órdenes (no se pudo probar con el archivo de recientes real) |
| (nuestra) Mostrar la página de bienvenida | welcome | sí | — | ya-tenemos | reubicada aquí |
| (nuestra) Guardar sobre el archivo abierto sin preguntar | saveOverOpened | no | — | ya-tenemos | reubicada aquí |

## Sistema de entrada

| Original (etiqueta) | Clave | Pred. original | Significado | Estado | Dónde / por qué |
|---|---|---|---|---|---|
| Dispositivos de entrada: clase por dispositivo (Ratón / Lápiz / Borrador / Pantalla táctil / Desactivado) | inputDeviceClasses | por dispositivo | cómo se trata cada dispositivo | añadido | tabla con `QInputDevice::devices()`; clave = nombre + tipo de Qt; DocumentView aplica la clase a eventos de tableta y de ratón. Con un ratón real y el dispositivo «core pointer» del selftest; **sin probar con una tableta real** |
| Zona de prueba del dispositivo | — | — | dibujar para ver la clase | adaptado | botón «Probar el lápiz…» → «Lápiz y tableta» (hoja de prueba y presión en vivo) |
| Dibujar fuera de la ventana | inputSystemDrawOutsideWindow | sí | el trazo sigue fuera del borde | añadido | `xpp.drawOutside`; apagado, el trazo termina en el borde |
| Unir los eventos de los botones con los de la punta | inputSystemTPCButton | Windows: sí | botones del lápiz junto a la punta | no-aplica | con Qt los botones siempre llegan con los eventos de la punta; control marcado y desactivado |
| Inferir la presión | pressureGuessing | no | adivinar la presión por la velocidad cuando el dispositivo no la da | añadido | `xpp.pressureGuess`; algoritmo propio (más lento = más firme, suavizado) |
| Método de promediado | stabilizerAveragingMethod | ninguno | ninguno / aritmético / gaussiano según velocidad | añadido | `InputFilter`; algoritmos propios |
| Preprocesador | stabilizerPreprocessor | ninguno | ninguno / zona muerta / inercia | añadido | idem |
| Tamaño del búfer | stabilizerBuffersize | 20 (2..100) | puntos del promedio | añadido | |
| σ | stabilizerSigma | 0,5 (0,05..5) | anchura del gaussiano | añadido | |
| Radio de la zona muerta | stabilizerDeadzoneRadius | 1,3 (0,1..50) | radio sin movimiento | añadido | píxeles de pantalla |
| Arrastre | stabilizerDrag | 0,4 (0..1) | inercia | añadido | |
| Masa | stabilizerMass | 5 (1..30) | inercia | añadido | |
| Detección de cúspides | stabilizerCuspDetection | sí | giros bruscos reinician el promedio | añadido | |
| Finalizar el trazo | stabilizerFinalizeStroke | sí | termina donde estaba el lápiz | añadido | |

## Ratón

| Original (etiqueta) | Clave | Pred. original | Significado | Estado | Dónde / por qué |
|---|---|---|---|---|---|
| Botón izquierdo: herramienta, color, grosor | buttonConfig[MOUSE_LEFT] | sin cambio | herramienta mientras se mantiene | añadido | `xpp.mouseLeft`; `Editor::toolForGesture` y estilo prestado durante el gesto |
| Botón central | buttonConfig[MOUSE_MIDDLE] | mano | | añadido | `xpp.mouseMiddle`, pred. mano (arrastra la página, como siempre) |
| Botón derecho | buttonConfig[MOUSE_RIGHT] | sin cambio | | añadido | `xpp.mouseRight` |
| 4.º botón | buttonConfig[MOUSE_4] | sin cambio | | añadido | `xpp.mouse4` (una tecla de «Lápiz y tableta → Teclas» sobre ese botón manda) |
| 5.º botón | buttonConfig[MOUSE_5] | sin cambio | | añadido | `xpp.mouse5` |
| Lista de herramientas (lápiz, borrador, marcador, texto, imagen, láser, espacio vertical, selecciones, mano, caja flotante, texto de PDF…) | — | — | | adaptado | las herramientas de esta app con botón, «Mano» y «Cuadro de herramientas flotante»; láser y selecciones son los de esta app (no las variantes «multicapa» ni «texto de PDF») |
| Tipo de dibujo (línea, rectángulo, elipse, flecha, …) | drawingType | sin cambio | forma que dibuja el botón | adaptado | se elige la herramienta de forma correspondiente en la lista de herramientas |
| Tipo de borrador (estándar, blanqueo, borrar trazo) | eraserType | sin cambio | | adaptado | «Goma (trazo entero)» / «Goma (parcial)» son dos herramientas; el blanqueo no existe → falta |
| Dispositivo por botón | device | — | | no-aplica | los botones se atribuyen por la clase del dispositivo (página Sistema de entrada) |

## Lápiz

| Original (etiqueta) | Clave | Pred. original | Significado | Estado | Dónde / por qué |
|---|---|---|---|---|---|
| Activar la sensibilidad a la presión | pressureSensitivity | sí | grosor según la presión | añadido | `xpp.pressureSens` (apagado: grosor constante) |
| Presión mínima | minimumPressure | 0,05 (0,01..1) | suelo de la presión | añadido | `xpp.minPressure` |
| Multiplicador de presión | pressureMultiplier | 1 (0,5..4) | escala | añadido | `xpp.pressureMult`; el resultado se limita a 1 (la curva de presión de la app va de 0 a 1) |
| Ignorar los primeros N eventos | numIgnoredStylusEvents | desactivado | evita artefactos al inicio del trazo | añadido | `xpp.ignoreFirst` / `ignoreFirstN` (1..1000); el toque cuenta como el primero; solo lápiz |
| Botón 1 / Botón 2 / Borrador: herramienta | buttonConfig[STYLUS_ONE/TWO/ERASER] | sin cambio | | ya-tenemos | `TabletSettings::barrel/barrel2/eraser` (comparten datos con «Lápiz y tableta»; solo se aplica lo que este diálogo cambió) |
| Botón 1 / 2 / Borrador: color y grosor | idem | sin cambio | | añadido | `xpp.stylus1/2/stylusEraser` |
| El borrador del lápiz es visible (nunca, siempre, al flotar, al tocar) | eraserVisibility | siempre | círculo del borrador | añadido | `xpp.eraserVisibility`; **sin probar con el extremo borrador de un lápiz real** |
| (nuestra) Aspecto del lápiz normal | penLook | prototipo | | ya-tenemos | reubicada aquí |
| (nuestra) Curva de presión, hoja de prueba, teclas del mando | — | — | | ya-tenemos | siguen en «Lápiz y tableta…» (botón «Curva de presión y prueba…») |

## Pantalla táctil

| Original (etiqueta) | Clave | Pred. original | Significado | Estado | Dónde / por qué |
|---|---|---|---|---|---|
| Activar el dibujo táctil | touchDrawing | no | los dedos dibujan | ya-tenemos | `Prefs::fingerDraw` |
| Reconocimiento interno de la mano | touch.disableTouch | no | ignorar dedos con el lápiz cerca | adaptado | `xpp.handRecognition`, pred. sí (la app ya lo hacía) |
| Tiempo de espera | touch.timeout | 1 s | | adaptado | `xpp.handTimeout` 0,5..10 s, pred. 0,7 s (el valor que ya tenía) |
| Método de desactivación (Automático / X11 / Personalizado) | touch.method | automático | | no-aplica | la app decide por los eventos del lápiz de Qt; X11 y órdenes externas no existen en Windows |
| Órdenes personalizadas de activar y desactivar (+ Probar) | touch.cmdEnable / cmdDisable | — | | no-aplica | idem |
| Activar los gestos de zoom (requiere reiniciar) | zoomGesturesEnabled | sí | pellizco | añadido | `xpp.zoomGestures`; se aplica al momento |
| Empezar el zoom tras una distancia (%) | touchZoomStartThreshold | 0 | | añadido | `xpp.zoomStartThreshold` |
| Desactivar el desplazamiento inercial de GTK | gtkTouchInertialScrolling | no | | adaptado | `xpp.disableInertia` desactiva la inercia propia de la app (la de GTK no existe) |

## Ver

| Original (etiqueta) | Clave | Pred. original | Significado | Estado | Dónde / por qué |
|---|---|---|---|---|---|
| Mostrar la barra de menús al iniciar | menubarVisible | sí | | añadido | `xpp.menubarAtStart`; **F10** la muestra u oculta (acción `view.menubar`, también en Ver) |
| Mostrar la ruta del archivo en la barra de título | filepathShownInTitlebar | no | | añadido | `MainWindow::titleBarText` |
| Mostrar el número de página en la barra de título | pageNumberShownInTitlebar | no | | añadido | idem (` · pág. 1/3`) |
| Color del borde de la página actual | selectionBorderColor | rojo | | adaptado | `xpp.colorBorder`; sin valor = sin borde (la app no lo dibujaba); capa sobre la página |
| Color de fondo entre páginas | backgroundColor | gris | | añadido | `xpp.colorBackground` (plano; reemplaza el degradado del tema) |
| Color de la selección | selectionMarkerColor | azul acero | | adaptado | `xpp.colorSelection`: marco y asas de la selección |
| Color de la selección activa (resultados de búsqueda…) | activeSelectionColor | verde | | adaptado | `xpp.colorActiveSelection`: el resultado de búsqueda en curso |
| Usar los iconos de stock (requiere reiniciar) | useStockIcons | no | | no-aplica | un único juego de iconos |
| Tema de iconos (Color / Lucide) | iconTheme | color | | no-aplica | un único juego de iconos (control desactivado) |
| Variante del tema (sistema / claro / oscuro) | themeVariant | sistema | | no-aplica | la app tiene dos temas (clásico y neon: el selector «Tema» de la misma página); no sigue al sistema |
| (nuestra) Tema, Efectos | theme, effects | — | | ya-tenemos | reubicadas bajo «Apariencia» |
| Mostrar sombra detrás de las páginas | showPageShadow | sí | | añadido | `xpp.pageShadow` |
| Recolorear el área de dibujo / vistas previas (colores oscuro y claro) | recolor.* | no | | falta | la app no recolorea páginas; control desactivado |
| Icono del cursor para el lápiz (sin icono / punto / lápiz grande / sistema) | stylusCursorType | punto | | adaptado | `xpp.cursorIcon` con dos opciones más: el valor 0 (predeterminado) es un círculo del ancho real del trazo, con la flecha del sistema fuera de la página, y «Cruz» (valor 5) sigue disponible |
| Resaltar la posición del cursor | highlightPosition | no | | añadido | `xpp.highlightCursor` (capa transparente sobre la página) |
| Radio / color / ancho del borde / color del borde | cursorHighlightRadius, Color, BorderWidth, BorderColor | 30 / amarillo 50 % / 0 / azul 50 % | | añadido | `xpp.highlight*` (el color admite transparencia) |
| Mostrar la barra lateral en el lado derecho | sidebarOnRight | no | | añadido | `xpp.sidebarRight` (mueve el dock) |
| Mostrar la barra de desplazamiento vertical a la izquierda | scrollbarOnLeft | no | | añadido | `xpp.scrollbarLeft` (disposición derecha-izquierda del área, la barra horizontal conserva su sentido) |
| Numeración de páginas de la barra lateral (ninguna / debajo / círculo / cuadrado) | sidebarNumberingStyle | debajo | | añadido | `xpp.sidebarNumbering`; el círculo y el cuadrado se dibujan sobre la miniatura |
| Ocultar la barra de desplazamiento horizontal / vertical | scrollbarHideType | ninguna | | añadido | `xpp.hideHScroll/hideVScroll` |
| Desactivar que las barras se desvanezcan | disableScrollbarFadeout | no | | no-aplica | las barras de Qt en Windows no se desvanecen |
| Desplazamiento al arrastrar una selección contra el borde: velocidad base, multiplicador | edgePanSpeed, edgePanMaxMult | 20 / 5 | | falta | la selección no desplaza la página sola; controles desactivados |
| Pantalla completa: barra de menús / herramientas / barra lateral | fullscreenViewModeAttributes | no / sí / sí | | añadido | `xpp.fs*`; al entrar en pantalla completa (F11 o su botón) y no estar en presentación |
| Modo presentación: barra de menús / herramientas / barra lateral / ir a pantalla completa | presentationViewModeAttributes | no / no / no / sí | | añadido | `xpp.pr*`; tras entrar en presentación (F5) se muestran las partes elegidas y, sin «pantalla completa», la ventana no la usa (`XppApplier::applyPresentationParts`) |
| Seleccionar la barra de herramientas (presentación) | selectedToolbar | — | | no-aplica | una sola barra de herramientas |
| Precargar páginas antes / después | preloadPagesBefore/After | 3 / 5 | | no-aplica | tesela propia con caché acotada; control desactivado |
| Limpiar las páginas en caché al desplazarse | eagerPageCleanup | sí | | no-aplica | idem |

## Escala

| Original (etiqueta) | Clave | Pred. original | Significado | Estado | Dónde / por qué |
|---|---|---|---|---|---|
| Velocidad de un paso de zoom (%) | zoomStep | 10 | | adaptado | `xpp.zoomStep`; pred. **20 %** (el paso que la app ya tenía: ×1,2) |
| Velocidad con Ctrl + rueda (%) | zoomStepScroll | 2 | | adaptado | `xpp.zoomStepScroll`; pred. **19,72 %** por muesca (= exp(0,0015·120) de siempre) |
| Forzar el zoom al ancho al cargar | forceZoomToFitOnLoad | no | | añadido | `xpp.forceFitOnLoad` |
| Calibración de los DPI (automática / regla) | displayDpi | automática | | falta | el 100 % de la app es un píxel por punto; controles desactivados |

## Área de dibujo

| Original (etiqueta) | Clave | Pred. original | Significado | Estado | Dónde / por qué |
|---|---|---|---|---|---|
| Añadir espacio horizontal adicional (izq., der.) | addHorizontalSpace (+Left/Right) | no, 150 px | | añadido | `xpp.addHSpace`, `hSpaceLeft/Right` (`DocumentView::setExtraSpace`) |
| Añadir espacio vertical adicional (arriba, abajo) | addVerticalSpace (+Above/Below) | no, 150 px | | añadido | `xpp.addVSpace`, `vSpaceAbove/Below` |
| Desplazamiento ilimitado | unlimitedScrolling | no | | adaptado | una pantalla entera de espacio por cada lado |
| Desplazamiento de la primera página (páginas emparejadas) | numPairsOffset | 1 | | falta | la vista de dos páginas empareja 1-2, 3-4…; control desactivado |
| Añadir una última página vacía (desactivado / al desplazarse al final / al dibujar) | emptyLastPageAppend | desactivado | | añadido | `xpp.lastPageAppend`; excluye páginas con fondo PDF |
| Modificadores según la dirección del trazo (radio) | drawDirModsEnabled / Radius | no / 50 | experimental | falta | controles desactivados |
| Conservar el grosor de línea al redimensionar | restoreLineWidthEnabled | no | | falta | la selección escala siempre el grosor |
| Tolerancia de ajuste de rotación / a la cuadrícula / tamaño / ajuste de formas reconocidas | snapRotationTolerance, snapGridTolerance, snapGridSize, snapRecognizedShapesEnabled | 0,3 / 0,5 / 14,17 / no | | falta | la rotación de la selección se ajusta a pasos fijos con Mayús; sin cuadrícula; controles desactivados |
| Tamaño mínimo del trazo del reconocedor de formas | strokeRecognizerMinSize | 40 | | falta | sin umbral configurable en «Formas automáticas» |
| Acción al toque: activar | strokeFilterEnabled | no | | añadido | `xpp.tapEnabled` (`InputFilter`: el trazo corto y rápido se descarta) |
| Límite de tiempo / de longitud (mm) / de frecuencia | strokeFilterIgnoreTime / IgnoreLength / SuccessiveTime | 150 ms / 1 mm / 500 ms | | añadido | `xpp.tapTime/tapLength/tapSuccessive` |
| Mostrar el cuadro de herramientas flotante | doActionOnStrokeFiltered | no | | añadido | `xpp.tapToolbox` |
| Intentar seleccionar primero un objeto | trySelectOnStrokeFiltered | no | | falta | la selección no ofrece buscar el objeto bajo un punto; control desactivado |
| Volver a dibujar el fondo PDF cuando el zoom cambie más de (%) | pageRerenderThreshold | 5 | | no-aplica | el fondo PDF se rasteriza en un hilo aparte a la medida del zoom |

## Valores predeterminados

| Original (etiqueta) | Clave | Pred. original | Significado | Estado | Dónde / por qué |
|---|---|---|---|---|---|
| Herramienta predeterminada: herramienta, color, grosor | buttonConfig[DEFAULT] | lápiz, negro, fino | el botón «Predeterminado» | añadido | `xpp.defaultTool`; acción `tool.default` (Herramientas → Herramienta predeterminada) |
| Puntero láser: tiempo de desvanecimiento | laserPointerFadeOutTime | 500 ms | | adaptado | `xpp.laserFadeMs` 0..4000; pred. **1400 ms** (el de la app Electron, que ya tenía) |
| Sangrar con espacios / número de espacios | useSpacesForTab / numberOfSpacesForTab | no / 4 | Tab en el texto | añadido | `xpp.tabSpaces/tabSpaceCount`; **sin probar con el texto real escrito a mano en la herramienta de texto** (solo por lectura del código) |
| (nuestra) Paleta de colores | palette | clásica | | ya-tenemos | reubicada aquí |
| (nuestra) Tamaños de las herramientas | sizes | | | ya-tenemos | cinco valores por herramienta como lista; también clic derecho / pulsación larga en la barra |
| (nuestra) Cuadro de herramientas activado | toolbox | | | ya-tenemos | reubicada aquí |

## Grabación de audio

| Original (etiqueta) | Clave | Pred. original | Significado | Estado | Dónde / por qué |
|---|---|---|---|---|---|
| Desactivar el audio | disableAudio | no | | falta | la app no graba audio; todos los controles de la página se ven desactivados con el aviso |
| Carpeta de almacenamiento | audioFolder | | | falta | idem |
| Dispositivo de entrada / de salida | audioInputDevice / OutputDevice | sistema | | falta | idem |
| Frecuencia de muestreo | audioSampleRate | 44100 | | falta | idem |
| Ganancia | audioGain | 1 | | falta | idem |
| Tiempo de búsqueda predeterminado | defaultSeekTime | 5 s | | falta | idem |

## LaTeX

| Original (etiqueta) | Clave | Pred. original | Significado | Estado | Dónde / por qué |
|---|---|---|---|---|---|
| Activar la herramienta LaTeX | latex.enabled | sí | | no-aplica | siempre disponible: las fórmulas las compone el motor integrado (sin instalar LaTeX) |
| Orden de generación, plantilla global, archivo de plantilla, prueba | latex.genCmd, globalTemplatePath… | pdflatex… | | no-aplica | idem; el texto TeX queda en el archivo y la app Electron lo reabre |

## Idioma

| Original (etiqueta) | Clave | Pred. original | Significado | Estado | Dónde / por qué |
|---|---|---|---|---|---|
| Idioma (requiere reiniciar) | preferredLocale | sistema | | no-aplica | la interfaz está solo en español; control con un único valor, desactivado |

## Otras claves de `Settings.cpp` que no son del diálogo

Estado interno del original (no son opciones): `font`, `mainWndWidth/Height`, `maximized`, `lastSavePath/OpenPath/ImagePath`, `showSidebar/sidebarWidth`, `showToolbar`, `numColumns/numRows/viewFixedRows/layout*`, `showPairedPages`, `presentationMode`, `fullscreenActive`, `sizeUnit`, `pageTemplate`, `pluginEnabled/Disabled`, `colorPalette`, `activeViewMode`, `pdfPageCacheSize`. Esta app guarda su propio estado de ventana y rutas (`QSettings`) y no las lee del `settings.xml` de Xournal++: **no-aplica**.

## Pendiente que no se hizo (honesto)

- Sin probar: tabletas reales (clases de dispositivo, eraser visibility con el extremo borrador, estabilizador con presión real), pantallas táctiles reales (zoom con umbral, inercia), DPI altos y varios monitores, el arranque con el archivo más reciente con un archivo de recientes real, el Tab con espacios escrito en la herramienta de texto.
- La app Electron descarta el objeto `xpp` cuando es ella la que guarda `prefs.json`.
