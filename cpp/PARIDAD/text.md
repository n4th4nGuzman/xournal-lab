# Paridad: texto, LaTeX, Markdown, imágenes, formas, reconocimiento de formas, exportar PDF

Área `text`. Estados: **hecho** (lo ejecuté yo), **parcial**, **falta**, **a-probar-por-el-usuario** (necesita lápiz/tableta real, segundo monitor o Xournal++ oficial).
Verificación: `QT_QPA_PLATFORM=offscreen build\xl_app.exe --selftest --log FILE` (eventos sintéticos por la ruta real de Qt), `ctest` (incluye `test_textfixes`, nuevo),
y las herramientas nuevas `xl_formula_compare`, `xl_export_pdf`, `xl_render_page` (ahora con el decodificador PDF instalado), más Electron real (`app/node_modules/electron`) para generar PDFs de fórmulas.

## Lista de comprobación

| # | Elemento (README / app.ts / editor.ts) | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 1 | Texto: clic crea un editor en el sitio, Ctrl+Intro / clic fuera confirma, Esc cancela, vacío = se borra, un solo paso de deshacer | hecho | selftest `text: …` (14 comprobaciones) | — |
| 2 | Texto: clic en un texto lo reabre con su contenido (el original se oculta mientras se edita) | hecho | selftest `text: a click on it re-opens…` | — |
| 3 | Texto: tamaño y color del estilo actual; fuente/zoom del editor = los de la página | hecho | selftest `text: size and colour…`, `editor sits at the text's position at zoom 160 %` | tipografía `Segoe UI` en lugar de `system-ui` (igual que antes: aproximada, README ya lo dice) |
| 4 | Texto sin confirmar se confirma antes de **guardar, exportar PDF/PNG e imprimir** (Electron: `inserts.commitText()` en cada uno) | hecho (corregido) | selftest `text: flushPending commits the text being typed`; `Editor::flushPending()` lo llaman `saveTo`, exportar y imprimir | el autoguardado NO lo confirma a propósito (no cerrar la caja mientras escribes) |
| 5 | Botón M↓ de Markdown: por defecto encendido, afecta a textos nuevos, se puede cambiar escribiendo (solo esa caja), `xlMd="1"`, deshacer | hecho | selftest `markdown: …` (8 comprobaciones) | — |
| 6 | Markdown: encabezados, negrita, cursiva, tachado, código, bloques, listas, tareas, citas, reglas, enlaces, `\*`, marca sin cierre, `snake_case` | hecho | `test_markdown` (tabla) | — |
| 7 | Markdown: `__negrita__` no cerraba tras una letra (los dos programas) | hecho (corregido en ambos) | `test_markdown` (3 casos nuevos); TS: `tsc --noEmit` limpio y comprobación con esbuild de 10 cadenas (`__bold__ x`, `a__b__c`, `foo__bar__`, …). Además `snake__case` ya no produce dos cursivas | el e2e completo de Electron no se re-ejecutó (la prueba tarda y abre ventana); el cambio es local a `inlineRuns` |
| 8 | Markdown: vista previa en vivo (el README de Electron dice que no existe) | hecho (mejora, no existía) | selftest `markdown: a live preview … shows under the editor`, `follows what is typed`, `goes away when markdown is switched off` | tarjeta bajo el editor (sobre él si no cabe); no está en Electron |
| 9 | Texto: cajas de texto no giran ni se espejan; se reubican | hecho | cubierto por el área de selección (no repetido aquí) | — |
| 10 | Imágenes: botón, Ctrl+V, arrastrar archivo; PNG base64; máx. 2000 px; atributos `left top right bottom`; seleccionada al insertar; vuelve a la herramienta anterior con Esc | hecho | selftest `image: …` (13 comprobaciones) | — |
| 11 | Pegar una imagen del portapapeles se insertaba **dos veces** (paste de la selección + manejador de imágenes) | hecho (corregido) | selftest `image: Pegar … inserts it exactly once` (antes `>=`). La selección delega ahora en `texttools::pasteImageFromClipboard` y se borró su copia duplicada del código | — |
| 12 | Formas: rectángulo, elipse, línea, flecha; Mayús = cuadrado/círculo/15°; color y grosor actuales; trazo `pen` normal; clic sin arrastre no hace nada; un solo deshacer | hecho | selftest `shape: …` (12) | — |
| 13 | Formas: esquinas **vivas** (rectángulo, triángulo, punta de flecha); la tinta libre conserva la suavidad del lápiz | hecho (corregido) | `test_textfixes` (píxel de la esquina exterior, con marca `xlShape` y sin ella; y un trazo libre denso sigue redondeado); selftest `shape: marked xlShape`, `recognizer: … xlShape` | render.ts dibuja **todos** los trazos como polilínea; aquí solo se endurecen las formas: marca `xlShape="1"` (nuevo atributo propio, Xournal++ lo ignora) para las nuevas y, en archivos sin marca (Electron/Xournal++), una heurística: ≤ 12 puntos, un solo ancho, todos los segmentos ≥ 5 u |
| 14 | Formas automáticas: línea, elipse, rectángulo, triángulo; garabato intacto; apagado = sin conversión | hecho | selftest `recognizer: …` (7) | mismas cifras que el e2e de Electron (146/10/4/8 puntos) |
| 15 | LaTeX: clic abre el diálogo con vista previa en vivo, error en rojo que bloquea Insertar, auto-cierre de llaves, Ctrl+Intro / Esc | hecho | selftest `latex: …` (~45) | el diálogo no es el flotante/desacoplable del README (sección «Diálogos que se mueven»): es modal, ver nota |
| 16 | LaTeX: paleta ampliada (388 + 78 vectorial, 11 pestañas), buscador sin tildes, Recientes, cursor en su sitio, botones dibujados como fórmula | hecho | selftest `latex: … palette …`, `test_mathlayout` (todos los botones se componen) | las etiquetas largas quedan pequeñas (también en Electron, se encogen) |
| 17 | LaTeX: opciones centrada/en línea, tamaño, «guardar como imagen simple», recuerda lo último | hecho | selftest `latex: …` | — |
| 18 | LaTeX: clic / doble clic sobre una fórmula la reabre; editar conserva `xlRot`/`xlFlip`; deshacer/rehacer exactos | hecho | selftest `latex: …` | — |
| 19 | Fórmula guardada como `<teximage text left top right bottom>` + PDF de una página (como Xournal++) | hecho | selftest + `test_texrender` | — |
| 20 | (a) **Fuente de dibujo de una fórmula**: se dibuja desde **su PDF** (lo que hizo KaTeX o el motor propio); solo si el PDF no se puede leer se vuelve a componer desde `text` | hecho | `xl_render_page` sobre `app/e2e_out.xopp` (fórmulas reales de Electron: `\int_0^1 x^2dx=\frac13`, `\alpha+\beta` se ven con el aspecto de KaTeX); `test_textfixes` | decisión: el PDF es la fuente correcta (fiel a lo que el autor vio y a su color); recomponer desde el texto cambiaría el aspecto de lo que escribió Electron |
| 21 | Color de la tinta de la fórmula (PDF rojo de Electron) | hecho | PDFs generados con Electron real en `#d32f2f`, abiertos en C++: el color se conserva (matte blanco/negro) | — |
| 22 | Giro y espejo `xlRot` / `xlFlip` en fórmulas de Electron | hecho | mismo documento, giros 0,6 rad, espejo, 90° + espejo: dibuja bien | — |
| 23 | Nitidez al acercar (Electron: re-rasteriza hasta 4096 px) | hecho (corregido) | antes C++ se quedaba en 4 px/pt; ahora se rehace desde el PDF en pasos de ×2 hasta 4096 px (y 16 Mpx), solo con el dibujo definitivo (`!lite`). `test_textfixes`: a ×12 se pide > 4 px/pt y ≤ 4096; imagen a ×8 mirada: bordes limpios | tope 16 Mpx para fórmulas altas (Electron solo topa el ancho) |
| 24 | PDFs `teximage` hechos por Electron real | hecho | 453 PDFs de la paleta generados con la ventana oculta de Electron (misma `printToPDF`) y rasterizados en C++ con pdfium (`xl_formula_compare`); y `e2e_out.xopp` abierto, visto y exportado | — |
| 25 | (b) Motor de composición propio vs KaTeX (corpus: los 453 textos únicos de la paleta) | parcial | `xl_formula_compare` genera hojas lado a lado (KaTeX-PDF vs C++); todas las 453 compilan; mirado a ojo ≈ 120 casos. Muy parecidos en fracciones, raíces, sumas, matrices, acentos, vectoriales | **Corregido**: tamaño (KaTeX pone `.katex` a 1,21 em: ahora ×1,21 al insertar), `\tag{..}` / `\tag*{..}` (se ignoraba), llaves de `\overbrace/\underbrace` (más gruesas y separadas). **Diferencias que quedan**: integrales rectas y más gruesas (Cambria Math, KaTeX las inclina), delimitadores `\left\|` algo más gruesos, `\angle` sale con un glifo distinto, `\circ` más pequeño, flechas largas más finas, la caja de KaTeX tiene un margen vertical mayor (la de C++ es ajustada) |
| 26 | Exportar a PDF: una página por página, tamaño real, trazos/texto/fórmulas como **vector** (mejor que el JPEG de Electron) | hecho | `xl_export_pdf` sobre `e2e_out.xopp`: 9 páginas, 6,3 MB; página 1 dibujada con pdfium comparada con la de Electron (misma tinta y fórmulas) | PDF mayor que el de Electron (vector frente a JPEG) |
| 27 | Exportar: rango de páginas, PNG, imprimir | hecho | selftest del área pdf (ya existente) | no existe en Electron (mejora) |
| 28 | Fondos PDF **vectoriales** en el PDF exportado | falta (inviable) | — | Qt no puede incrustar una página de otro PDF; el fondo se escribe como imagen a 100–300 ppp (diálogo). Haría falta una biblioteca de fusión de PDF (qpdf/mupdf), fuera del alcance «sin dependencias nuevas». Decisión de diseño pendiente del usuario |
| 29 | Anotar PDF: abrir, páginas del tamaño del PDF, guardar en .xopp que enlaza al PDF | hecho | selftest `Anotar PDF: …` | — |
| 30 | Anotar PDF: el título de la ventana seguía en «Sin título» | hecho (corregido) | selftest `Anotar PDF: the window title names the PDF`; `MainWindow::setDisplayName`; el nombre sugerido para guardar ya no lleva `-lab` | — |
| 31 | PDF de fondo que falta: diálogo «Buscar el PDF…» | hecho (nuevo) / diálogo a-probar | `PdfBackgrounds::missing` + `commandLocatePdf` (misma carpeta: se añade a la búsqueda; otro nombre: se reescribe la referencia con deshacer). Se compila y el camino de datos lo cubren los selftests del área pdf; el **cuadro de diálogo** no se puede ejercitar en modo sin ventana (no se muestra en `--selftest`) | pasos para probarlo abajo |
| 32 | Pegar con la selección (copiar/pegar elementos) sigue funcionando | hecho | selftests de selección (no se tocó la rama de elementos) | — |
| 33 | Fluidez con lápiz en formas / reconocedor | a-probar-por-el-usuario | solo eventos sintéticos | — |

## Pasos para lo que no pude ejecutar

* **Diálogo «Buscar el PDF…» (31):** abre un `.xopp` que enlace a un PDF, renombra o mueve el PDF y reabre el `.xopp`: debe salir «No se encuentra el PDF …» con el botón **Buscar el PDF…**; elige el archivo (en su nueva carpeta o con otro nombre) y las páginas deben dibujarse; Deshacer revierte el cambio de nombre.
* **Esquinas vivas con el lápiz (13/14):** con el reconocimiento de formas encendido, dibuja a mano un rectángulo y un triángulo; las esquinas deben ser agudas y el resto de tus trazos conservar el aspecto del lápiz.
* **Xournal++ oficial (26, 19):** abre en Xournal++ un `.xopp` guardado aquí con fórmulas y exporta a PDF allí; nunca se ha probado con la aplicación oficial (igual que en Electron).
* **Segundo monitor / diálogo flotante (15):** el diálogo LaTeX de C++ no se desacopla; es una decisión del área de ajustes, no de esta.

## Lo que quedó fuera o es decisión de diseño

* Fondos PDF vectoriales (28): ver arriba.
* Las diferencias tipográficas de (25) vienen del uso de la fuente matemática del sistema (Cambria Math) y no de KaTeX; igualarlas exactamente exigiría empaquetar las fuentes KaTeX y un motor de forma.
* Líneas de texto sin ajuste de ancho: igual que Electron (README).

## Estado de las pruebas al terminar

* `ctest --test-dir build-text`: 23/23 (incluye `textfixes`, nuevo: esquinas, `\tag`, `__negrita__`, PDFs de Electron, nitidez a ×12).
* `--selftest`: todas las comprobaciones de esta área pasan (texto, Markdown con vista previa, formas, reconocedor, imágenes, pegar una vez, LaTeX, `flushPending`, título de «Anotar PDF»).
  La ejecución completa **no termina** en este momento: se cuelga tras `glow: the toolbar has the Brillo button` (`app/selection/parity_selftest.cpp`, prueba de otra área que abre un menú con bucle propio), y aparecen dos fallos ajenos
  (`floating: … 60 %%` y `select: the lasso takes a text only when its centre is inside`). Ninguno toca archivos de esta área; el árbol estaba en edición (hubo varios fallos de compilación ajenos mientras trabajaba).
* Herramientas nuevas: `xl_formula_compare` (hojas KaTeX-PDF frente a motor propio), `xl_export_pdf` (exporta un .xopp y dibuja páginas de cualquier PDF con pdfium); `xl_render_page` ahora instala el decodificador PDF.
