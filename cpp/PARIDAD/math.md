# Paridad: área «math» (plano, espacio 3D, figuras, etiquetas, recta real, diagramas de conjuntos, relleno y Riemann)

Auditoría. Referencia: README.md (secciones indicadas), `app/src/renderer/{editor,selection,plane,space3,figures,mathobj,numberline,venn,fill,repair,genobj}.ts` y los bloques e2e de `app.ts` (S3, MATH, FIG, PL, PF, F, LABELS, BACK).
Estados: **hecho** = lo ejecuté; **parcial** = ejecutado en parte o con una desviación declarada; **falta** = no está; **a-probar-por-el-usuario** = necesita lápiz/tableta real, Xournal++ oficial u ojos humanos.

## Cómo se verificó (repetible)

| Qué | Cómo |
|---|---|
| Cadena real selección → manejadores generados, los 22 objetos (plano, 3D, las 16 figuras, recta, Venn de 2 y de 3) | `XL_SELFTEST_ONLY=chain QT_QPA_PLATFORM=offscreen build\xl_app.exe --selftest --log x.log` (también corre al final del `--selftest` completo). `app/mathobjects/chainselftest.cpp`. Eventos de ratón reales sobre el `viewport` (`QMouseEvent`), no llamadas directas. ~1740 comprobaciones. El oráculo de cada paso es la transformación que el gesto aplicó (calculada en la prueba, no por el código probado) y además se comparan los puntos de los trazos. |
| Interoperabilidad C++ → Electron y Electron → C++ | `tools\interchange\` (`math_entry.ts`, `math_main.cjs`, `math_compare.mjs`, `math_builddiff.mjs`): el parser/lectores del propio Electron (`planeSpecOf`, `space3SpecOf`, `figureSpecOf`, `readMath`, `repairGenerated`) corren dentro de Electron sobre el archivo guardado por C++ tras mover/girar/espejar/redimensionar/arrastrar anclas: 88 comprobaciones, 0 fallos. A la inversa, Electron construye los 22 objetos y los guarda; C++ los abre: lo reclama el manejador correcto, lee los mismos puntos de control, un clic lo selecciona y se puede arrastrar un ancla (`XL_CHAIN_ELECTRON=archivo`). |
| Los constructores de C++ dan lo mismo que los de Electron | Mismo spec e id en ambos, cada uno guarda un `.xopp`, el parser de Electron vuelca los dos y se comparan (`math_builddiff.mjs`): 22 objetos, diferencias ≤ 5e-6 (redondeo a 8 cifras) salvo la posición x de los textos centrados (hasta 0,4 pt: otra métrica de fuente, no cambia el objeto) y 0,0005 de grosor en una rejilla 3D. |
| Aspecto | Capturas de Electron (`start.mjs --page N --screenshot`, con `--js` de espera) y de C++ (`xl_render_page`, `xl_imgcrop`) del mismo `.xopp` en plano, 3D y Venn 3: mismos trazos y etiquetas (se miró a ojo; recta y figura de caja solo por números). |
| Suite completa | ctest 23/23 y `--selftest` completo 1093 PASS / 0 FAIL en mi compilación (`build-paridad-math`). |

## Tabla

### Plano cartesiano a voluntad

| # | Elemento | Estado | Verificación / notas |
|---|---|---|---|
| P1 | Herramienta `C`, botón, flecha de opciones, menú | hecho | selftest objetos «tool selected (key C)»; el menú sale del registro de herramientas (comprobado en recta/diagrama) |
| P2 | Arrastrar recuadro crea; un clic solo no inserta; tecla y panel; capa oculta lo rechaza | hecho | selftest objetos |
| P3 | Rango y escala por eje, ajuste a múltiplos, casillas cuadradas, rangos inválidos rechazados con aviso | hecho | panel (selftest objetos) + constructor idéntico al de Electron |
| P4 | Solo ejes; marcas, números, flechas, nombres, subcuadrícula 2/4/5/10 | hecho | panel (capturas `objects_panel_plane.png`, botones grandes) |
| P5 | Color, grosor y brillo del lápiz; «Estilo del lápiz» repinta; deshacer | hecho | `styleChecks` (6 tipos de objeto): el brillo sobrevive a la reconstrucción, el estilo del lápiz da color y brillo a todos los trazos y textos, deshacer lo revierte. La rejilla clara nunca brilla (igual que Electron) |
| P6 | Un clic o un rectángulo que lo toque selecciona **todo** el plano | hecho | cadena (clic en un trazo) + selftest selección |
| P7 | Redimensionar con las asas reconstruye en un solo paso; marcas, números y flechas conservan su tamaño | hecho | cadena: tamaños de texto = los de un constructor nuevo con el marco nuevo (la fórmula `clamp(celda·0,36; 6,5; 11)` es la de Electron: el número crece algo con la celda, no con el factor). Deshacer/rehacer exactos |
| P8 | Mover, espejar H/V, girar 180° (dos R): sigue siendo un plano editable | hecho | cadena: el marco leído = el marco transformado; puntos del primer trazo = los mapeados; deshacer/rehacer exactos |
| P9 | Girar 90° o un ángulo cualquiera con asa | parcial | **Desviación a propósito**: Electron lo sigue reclamando y, como lee el marco de las cajas de los ejes, un giro de 90° da un marco colapsado y un cambio de tamaño/panel lo destruiría. En C++ un plano que ya no tiene ejes horizontal y vertical **no se reclama**: queda como tinta (el README dice «sigue siendo tinta»), se mueve/escala como tinta y el panel no lo edita (`objects/handlers.cpp: axesAligned`, `objects/hub.cpp`). Comprobado: no se colapsa y deshacer es exacto |
| P10 | Deshacer/rehacer de cada gesto (mover, girar, espejar, redimensionar) | hecho | cadena + cadena completa (todo deshacer = objeto recién creado; todo rehacer = estado final) |
| P11 | Copiar/pegar/duplicar: objeto nuevo con id propio, editable, independiente | hecho | cadena: duplicado, copiar+2 pegados (3 ids distintos), editar la copia no toca el original, deshacer |
| P12 | Guardar y reabrir; sigue editable; atributos `xlPlane/xlRole/xlPlaneSpec` | hecho | `saveXopp`→`loadXopp` = mismo documento; clic en cada objeto reabierto lo selecciona como su tipo; Electron lo lee (88/88) |
| P13 | Panel edita en vivo el seleccionado; sin selección fija el próximo | hecho | selftest objetos |
| P14 | Arrastre proporcional desde el punto inicial (también hacia arriba-izquierda) | hecho | selftest objetos + `planeToolChecks` |
| P15 | Flechas/marcas más finas, ejes simples de lado a lado | hecho | constructor idéntico al de Electron (builddiff) |
| P16 | Esquinas de los trazos generados siguen siendo esquinas (no redondeadas) | hecho | `planeToolChecks`: el vértice de una L generada queda entintado y el interior de la esquina no (con suavizado por puntos medios no ocurriría) |
| P17 | El relleno y Riemann ignoran ejes y rejilla; el imán de la base se pega al eje, no a la rejilla | hecho | `fillChecks` (clic en un círculo cruzado por un plano rellena el círculo entero, no la hoja; Riemann sube hasta la curva, alturas ±4; base arrastrada a 3 pt del eje queda exactamente en él) |
| P18 | Ajustes persistidos en prefs.json | hecho | selftest objetos («the plane settings are saved through the prefs API») |
| P19 | Aspecto igual al de Electron | parcial | Captura lado a lado igual. En **ambos** la rejilla se ve más oscura en segmentos alternos del borde del marco: los conectores del zigzag se superponen a la línea del borde del otro trazo (alfa doble). No es una diferencia de C++; arreglarlo cambia la estructura del objeto (ver «Huecos») |
| P20 | Xournal++ oficial lo muestra como tinta común | a-probar-por-el-usuario | Abrir un `.xopp` guardado por la app (p. ej. el de `XL_CHAIN_SAVE`) en Xournal++: deben verse trazos y textos normales. No hay Xournal++ aquí |

### Espacio cartesiano 3D con anclas

| # | Elemento | Estado | Verificación / notas |
|---|---|---|---|
| S1 | Herramienta `D`, arrastrar recuadro, clic solo no inserta, defaults (z arriba, y derecha, x hacia ti) | hecho | selftest objetos |
| S2 | Seis anclas (cada mitad de cada eje); arrastrar una gira/estira solo esa mitad; un paso de deshacer | hecho | selftest objetos + cadena (ancla arrastrada con el puntero real termina donde se soltó; deshacer exacto) |
| S3 | Mayús conserva el largo y solo gira | hecho | `shiftChecks` (Mayús se mantiene durante el arrastre, no al pulsar: Mayús+pulsar empieza otra selección, como en Electron) |
| S4 | Panel: rango, escala, ángulo/largo de cada mitad, nombres, cuadrículas xy/xz/yz, marcas/números/flechas, Vista por defecto, Estilo del lápiz, mínimos inválidos | hecho | selftest objetos + `styleChecks` |
| S5 | Un solo objeto: mover, redimensionar (rebuilds, rangos intactos), espejar, girar (30° y 90°); la geometría se relee de los ejes | hecho | cadena (los seis pasos); las anclas siguen coincidiendo con los extremos de los ejes tras mover+girar (captura `build-paridad-math/shots/chain_space3.png`) |
| S6 | Copiar/pegar/duplicar, guardar/reabrir, deshacer | hecho | cadena |
| S7 | Espacio antiguo sin `n` se abre con ejes rectos | parcial | cubierto por el test de core (`test_plane_figures`), no por la cadena |
| S8 | El relleno ignora el espacio 3D | parcial | rol `grid3`/ejes tratados como generados en `core/fill.cpp` (tests de core); no ejecuté un relleno sobre un espacio 3D real |

### Figuras para matemáticas y física

| # | Elemento | Estado | Verificación / notas |
|---|---|---|---|
| F1 | Galería de 16 figuras en 4 grupos | hecho | selftest objetos + capturas |
| F2 | Arrastrar dibuja, clic hace tamaño por defecto, Mayús 15°, un paso de deshacer | hecho | selftest objetos |
| F3 | Polígonos/curva por puntos: clic por vértice, doble clic/Intro termina, Retroceso, Esc, cambiar de herramienta | hecho | selftest objetos |
| F4 | Anclas por punto de control; sólido: origen mueve todo, otras 3 cambian aristas | hecho | selftest objetos + cadena (las 16 figuras: ancla arrastrada con puntero real, puntos de control = los mapeados) |
| F5 | Opciones: puntas, trazo continuo/rayas/puntos, lados/meridianos, paralelos, aristas ocultas, rótulo; se aplican a la figura seleccionada o a la próxima | hecho | selftest objetos (panel) |
| F6 | Un solo objeto: seleccionar, mover, redimensionar, girar, espejar; tras ello las anclas coinciden y se sigue editando | hecho | cadena para las 16 figuras: 6 pasos × (reclamo, puntos, undo/redo) + ancla después de transformar |
| F7 | Copiar/pegar/duplicar (ids nuevos), guardar/reabrir, `repairGenerated` tras quitar posiciones de etiquetas | hecho | cadena (copia independiente); reabrir y clic las 16 figuras; reparación: mismos puntos de control |
| F8 | Las líneas de una figura son tinta normal para el relleno | hecho | `fillChecks`: clic dentro de un polígono rellena solo su interior |
| F9 | Estilo del lápiz, brillo | hecho | `styleChecks` (caja y flecha) |
| F10 | Vistas extremas de sólidos / círculos de cilindro–cono–esfera tras transformación afín no uniforme | parcial | tras las transformaciones de la cadena siguen editables y el constructor es idéntico al de Electron; el aspecto de las partes ocultas con vistas extremas no se revisó a ojo |

### Etiquetas al guardar y reabrir

| # | Elemento | Estado | Verificación / notas |
|---|---|---|---|
| L1 | Cada texto generado se guarda con `font`, `size`, `x`, `y`, `color` | hecho | `reabrir = mismo documento` para los 22 objetos y selftest objetos |
| L2 | Al abrir un archivo con etiquetas sin posición se rehace plano/3D/figura; aviso «Se rehicieron N objeto(s)…» | hecho | cadena (quitar atributos a los 3 tipos con texto y reparar: todos reparados, segunda pasada = 0, mismos puntos de control); `mainwindow.cpp` llama a `repairGenerated` y avisa |
| L3 | Lo mismo con tu archivo real `Downloads\complejos.xopp` (solo lectura) | hecho | `XL_CHAIN_FILE`: 3 objetos generados (1 plano, 2 figuras), 22 textos sin posición, C++ rehace 1 objeto y quedan 0; Electron ve los mismos 3 objetos y 22 textos |
| L4 | Reparación por Electron sobre un archivo de C++ | hecho | `math_compare.mjs`: mismo número de objetos reparados, idempotente, mismos puntos |

### Recta real y diagramas de conjuntos

| # | Elemento | Estado | Verificación / notas |
|---|---|---|---|
| R1 | Herramientas `N` y `J`, flechas de opciones, panel que se abre solo, un clic no inserta, Mayús 15°, 400 por defecto | hecho | selftest mathobjects (≈ 90 comprobaciones) |
| R2 | Lenguaje de conjuntos, errores en español, botones de símbolos, hasta 3 filas, extremos abiertos/cerrados/corchetes, infinito, agujero | hecho | selftest mathobjects + `test_sets*` |
| R3 | Anclas de la recta (extremos del eje; Mayús = ángulo) | hecho | mathobjects + cadena + `shiftChecks` (el extremo arrastrado con Mayús queda a múltiplo de 15°) |
| R4 | Diagrama: 2 y 3 conjuntos, sombreado exacto, trama densa, elementos, universo opcional, anclas (esquinas, centros, radios) | hecho | selftest mathobjects + cadena (2 y 3 conjuntos con los 6 pasos) |
| R5 | Mover, girar, espejar, redimensionar, copiar/pegar, deshacer, guardar/reabrir como un objeto | hecho | cadena; `repairGenerated` no se aplica a recta/diagrama (nunca tuvieron el defecto, igual que Electron) |
| R6 | Relleno trata la trama como papel y los círculos como paredes | hecho | selftest mathobjects + `fillChecks` |

### Relleno (cubo de pintura) y Riemann

| # | Elemento | Estado | Verificación / notas |
|---|---|---|---|
| B1 | Herramienta `U`, clic, línea arrastrada (izquierda/derecha), degradado lineal/radial, opacidad, cerrar huecos, imán, mantener la línea, deshacer | hecho | selftest mathobjects + `test_fill` (core) |
| B2 | Riemann: reglas (superior, inferior, punto medio, izquierdo, derecho), cantidad o ancho, contornos, columnas sin curva con aviso, clic sin base avisa | hecho | selftest mathobjects (superior, inferior, ancho, sin contorno, sin curva, clic) + `test_fill` (las 5 reglas contra fórmula, base recta o curva) |
| B3 | Qué se guarda: PNG bajo los trazos (`xlFill`), contornos `xlRiemann`, un paso de deshacer | hecho | selftest mathobjects (guardar/reabrir) |
| B4 | Rendimiento de un clic en una hoja densa | parcial | Medido: clic en una hoja con 8000 trazos cortos ≈ 275 ms (todo en `runFill`, que rasteriza todas las paredes; en el selftest de mathobjects, otra distribución de trazos, 37 ms de relleno y 316 ms de liberación). Es un clic, no un evento de puntero, pero crece con el número de trazos: se podría limitar el rasterizado a la zona alcanzable (no hecho) |
| B5 | Relleno con el papel de matemáticas/física de fondo | parcial | por diseño solo mira trazos de la capa (el fondo nunca es pared); no ejecuté un relleno sobre una página con papel especial |

### Papeles (solo en lo que toca al dibujo)

| # | Elemento | Estado | Verificación / notas |
|---|---|---|---|
| H1 | Papel con la paleta y papeles de matemáticas y física | falta de esta área | pertenecen al área de papel/chrome; aquí solo se comprobó que el relleno y los objetos generados no dependen del papel |

## Qué se corrigió

1. **Plano girado 90° (o un ángulo cualquiera)** se reclamaba y, al redimensionarlo o editarlo en el panel, se reconstruía a partir de un marco colapsado (17 × 16 pt en la prueba). Ahora no se reclama (`PlaneHandler::claims` exige ejes horizontal y vertical) y `Hub::selectedPlane` obedece al manejador. Prueba: `chainselftest.cpp` («a turned plane … keeps a real size», «turned 180 degrees it is a plane again»).
2. Se añadió **la cadena completa** selección → manejadores para los 22 tipos de objeto (hasta ahora solo había un manejador de mentira en el selftest de selección y una selección de sustitución en el de mathobjects), con deshacer/rehacer, copiar/pegar/duplicar, guardar/reabrir y reparación; más estilo/brillo, Mayús en anclas, relleno y Riemann sobre planos/figuras/diagramas reales, gestos de la herramienta de plano y esquinas nítidas.
3. **Interoperabilidad** probada en ambos sentidos con los lectores del propio Electron (`tools/interchange/math_*`), incluida la reparación, y paridad numérica de los constructores.
4. Encontré que no había ningún otro fallo en la cadena: las otras diferencias que aparecieron al principio eran fallos de mi prueba (puntos de agarre que coincidían con anclas o asas; Mayús pulsado junto con el clic empieza una selección nueva, como en Electron; las etiquetas del plano y del 3D cambian de tamaño con la celda, como en Electron).

## Ediciones pequeñas en archivos compartidos

- `app/selftest.cpp`: `#include` de `mathobjects/chainselftest.h`, rama `XL_SELFTEST_ONLY=chain` y una llamada al final del selftest completo.
- `app/objects/handlers.cpp`, `app/objects/hub.cpp`: lo del plano girado (arriba).
- Archivos nuevos: `app/mathobjects/chainselftest.{h,cpp}`, `tools/imgcrop.cpp` (recorta/amplía PNG), `tools/interchange/{math_entry.ts,build_math_bundle.mjs,math_main.cjs,math_compare.mjs,math_builddiff.mjs}`.

## Huecos y desviaciones conocidas

- **Rejilla del plano más oscura en tramos del borde** (alfa doble): idéntica en Electron. Arreglarla sin cambiar el formato exige que los conectores del zigzag no repasen el borde; hoy cada dirección es un único trazo (2 trazos en total, como en Electron). Posible arreglo con trazo extra o anchuras por segmento = estructura distinta de la de Electron. No lo hice.
- La trama del Venn se ve más clara en C++ que en Electron a 150 % (líneas de 0,3 pt con antialias distinto); la geometría es idéntica. Tema del renderizador.
- Un plano girado deja de ser editable con el panel (ver P9).
- Los asas/anclas tienen una zona de agarre de 8–10 px (igual que Electron); con un dedo puede ser justo, con lápiz bien. Está en «a probar».
- El relleno sobre hojas muy densas tarda ≈ 0,3 s por clic (B4).

## A probar por el usuario (con tableta/lápiz real o Xournal++)

1. **Lápiz real, anclas y asas**: con el lápiz, crear un plano (`C`), un 3D (`D`), una figura (`F`) y un diagrama (`J`) arrastrando; con la selección, agarrar un ancla (círculo) y una asa de esquina con la punta del lápiz. Si cuesta acertar, avisar: la zona de agarre es de 8–10 px (`kHandlePx`, `anchorHitPx`).
2. **Mayús en el arrastre de un ancla** con el teclado de la tableta (ACK05 o el teclado normal): pulsa el ancla, luego mantén Mayús mientras arrastras.
3. **Xournal++ oficial**: guarda una página con un plano, un 3D, una figura de cada grupo, una recta y un diagrama; ábrela en Xournal++ y comprueba que se ven como tinta normal (las aristas ocultas con `style="dash"`) y que los textos están en su sitio.
4. **Tu archivo `complejos.xopp`**: ábrelo (debe avisar «Se rehicieron 1 objeto(s)…», no se toca el disco hasta que guardes) y compara con la app Electron.
5. **Comparar con Electron** (si quieres verlo tú): `cd G:\xournal-lab\app; node start.mjs "%TEMP%\xl_math_inter\electron.xopp"` frente a abrir el mismo archivo en `build-paridad-math\xl_app.exe` (se regenera con `bash`/PowerShell según `tools\interchange\math_main.cjs make`).
6. **Pantalla de alta densidad / segundo monitor**: no probado (DPR 1 en las capturas sin pantalla).
