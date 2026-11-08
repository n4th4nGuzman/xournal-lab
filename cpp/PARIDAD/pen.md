# Paridad C++ / Electron: área «lápiz» (pen)

Auditoría. Fuente de verdad: `G:\xournal-lab\README.md` (secciones «Usar la aplicación», «Puntas, presión y tableta», «Botones del lápiz», «Presets de tamaño», «Volver a la herramienta de antes», «Inicio del trazo sin punto», «Opciones de herramienta en el mismo icono», «Rendimiento», «Limitaciones conocidas») y `app/src/renderer/{editor,app,tablet,tabletui}.ts`.

Estados: **hecho** = lo ejecuté (selftest con eventos Qt sintéticos por la ruta real, ctest, capturas que miré); **parcial** = existe pero con una diferencia que se explica; **falta** = no está; **a-probar-por-el-usuario** = necesita lápiz o pantalla táctil reales, segundo monitor o Xournal++ oficial (pasos al final).
Todo lo «hecho» con eventos sintéticos significa: `QTabletEvent`/`QMouseEvent`/`QTouchEvent`/`QWheelEvent` enviados al `viewport()` real de la ventana (plataforma `offscreen`). **Un evento sintético no es un lápiz físico ni una pantalla táctil.**

Dónde están las comprobaciones: `app/tools/penaudit.cpp` (esta auditoría, `XL_SELFTEST_ONLY=penaudit`), `app/tools/penchecks.cpp` (sensación del lápiz y tiles densos, `XL_SELFTEST_ONLY=pen|penstale`), `app/selftest.cpp` (`behaviour`, `pointerTable`, `XL_SELFTEST_ONLY=pointer`), `app/settings/settingsselftest.cpp` (diálogo, botones, teclas, presets), `tests/{brush,tablet,render}.cpp` (ctest).

## Resumen de estados (53 ítems de las tablas 1 a 10)

- **hecho**: 48, de los cuales 7 (3.1 a 3.5, 4.4, 4.5) están hechos con eventos sintéticos y llevan además «a-probar» con el hardware real (se cuentan abajo en la columna del usuario).
- **parcial**: 1 (10.8, memoria: acotada salvo el historial, que lo está por número de pasos y no por bytes).
- **falta**: 0 de lo factible. Las tres que eran «falta» al empezar (giro de página con la rueda, Shift+rueda, flecha del lápiz) y el `penLook` del diálogo están hechas.
- **a-probar-por-el-usuario**: 8 en total (3.1 a 3.5, 4.4, 4.5 y 4.7): pantalla táctil, palma, inercia, pellizco, niveles de presión, botones del lápiz y controlador XP-Pen. Pasos en la sección 14.
- Fuera de esta área, solo mirados: 1.10, 1.11, 8.2.
- Comprobaciones nuevas: `penaudit` 63 PASS / 0 FAIL (zoom, una página, dedos y palma, mano, láser, resaltador y brillo en vivo, teclas, anillo, reposo, diálogo, flecha del lápiz, barra a 4 anchos, miniaturas) más el bucle de estrés `XL_STALE_LOOP`.

## 1. Herramientas de dibujo

| # | Ítem (README / Electron) | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 1.1 | Lápiz (P), sensible a la presión, paleta y grosores de las muestras | hecho | selftest: «tablet pen press/move/release adds one stroke», ancho por punto, ratón = ancho constante, deshacer/rehacer; `penFeelChecks` (capa viva = trazo final, SSIM ≥ 0,995; primer ancho = segundo; sin punto de otro grosor; ida y vuelta por archivo); ctest `brush`, `tablet` | Por defecto el ancho sale de la curva del prototipo (`size·(0,3+0,7·p^0,8)`), no de la de Electron (`max(0,05; 1,5·p)·size`); ahora se elige en el diálogo (ver 4.8) |
| 1.2 | Resaltador (H): alfa `7f`, grosores 2,83…18 | hecho | selftest + `penaudit`: la capa viva se compone con **Multiply** como el trazo final (antes era alfa normal): SSIM 0,9996 contra el trazo final sobre tinta negra, el negro sigue negro bajo el resaltado | Los bordes de la capa viva son un pelo más oscuros (solapes de piezas), como en el lápiz (`maxd` ≤ 130, media 0,25) |
| 1.3 | Goma (E): borra trazos enteros; no borra capas ocultas | hecho | selftest «eraser end of the pen erases», deshacer exacto con 20000 trazos, `eraserTable` | — |
| 1.4 | Borrar parte (B): corta los trazos | hecho | selftest `behaviour` y `eraserTable` (300 eventos sobre 20000 trazos) | — |
| 1.5 | Anillo de la goma (radio real, también sin pulsar; desaparece al salir) | hecho | `penaudit`: 72 de 72 direcciones cambian a 32 px (radio 16 a zoom 2), 0 a 20 px; al salir el puntero se va | — |
| 1.6 | Láser (K): estela roja que se desvanece (~1,4 s), solo con el lápiz/ratón pulsado, no crea nada, ignora el dedo | hecho (corregido) | `penaudit`: pasar el puntero sin pulsar no muestra nada; pulsado deja estela roja; sin elementos ni pasos de deshacer; a 0,8 s sigue, a 1,4 s ya no; un dedo no la usa | **Corregido**: antes también dibujaba al pasar sin pulsar, duraba 0,7 s y no ignoraba el dedo. Brillo aproximado con una pasada ancha tenue (en Electron es `shadowBlur`) |
| 1.7 | Mano (M): arrastrar con ratón o lápiz mueve el lienzo sin dibujar; el dedo desplaza | hecho | `penaudit`: ratón y lápiz mueven la página la distancia exacta en cada sentido y no crean nada; botón lateral 2 = mano (selftest) | — |
| 1.8 | Teclas P H E B K M | hecho | `penaudit`: cada tecla cambia a su herramienta | — |
| 1.9 | Tamaño de goma 4/8/16/32 (+56) y presets de la barra | hecho | selftest de ajustes («sizes: …», 19 comprobaciones) | — |
| 1.10 | Selector de color personalizado junto a la paleta | no auditado aquí | pertenece a las áreas de ajustes y barra; sí vi en las capturas la paleta y el botón de paletas | — |
| 1.11 | «Formas auto»: la figura limpia al terminar el trazo | cubierto por el área de texto/formas | el lápiz llama a `recognizeShape` (inktool.cpp); sus comprobaciones están en `selftest_texttools` | — |

## 2. Zoom y desplazamiento

| # | Ítem | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 2.1 | Ctrl+rueda: zoom con factor `exp(0,0015·delta)` alrededor del puntero | hecho | `penaudit`: una muesca = ×1,1971; el punto bajo el puntero se queda bajo el puntero | — |
| 2.2 | Límites 25 % a 500 % | hecho | `penaudit`: la rueda se detiene exactamente en 5 y en 0,25 | — |
| 2.3 | Ctrl+ / Ctrl− (paso 1,2), Ctrl+0 = 100 % | hecho (corregido) | `penaudit` | **Corregido**: el paso era 1,25 (y 0,8); Electron usa 1,2. Se ajustó también la comprobación de `settingsselftest` |
| 2.4 | Ajustar al ancho / a la página | hecho | `penaudit`: la página ocupa 1354 de 1386 px; la página entera cabe | — |
| 2.5 | Vista continua / una página | hecho | selftest «single-page mode shows only the current page»; `penaudit` | — |
| 2.6 | Una página: la rueda al final de la página pasa a la siguiente (y al principio, a la anterior mostrando su final), como mucho cada 350 ms | hecho (era «falta») | `penaudit`: abajo pasa a la siguiente, una segunda muesca dentro de 350 ms no, arriba vuelve y deja la parte baja | — |
| 2.7 | Shift+rueda desplaza en horizontal (lo hace el navegador) | hecho (era «falta») | `penaudit` | — |
| 2.8 | Rueda normal | hecho | `penaudit` (sube/baja) | 120 px por muesca (Qt: 3 líneas × 40); Chromium ~100 px. Diferencia mínima, sin tocar |
| 2.9 | Mientras se dibuja no se desplaza ni se hace zoom | hecho | selftest (la rueda se ignora con un gesto activo); la inercia del dedo también se detiene al empezar un trazo | — |

## 3. Dedos, palma y táctil

Electron deja el dedo al navegador (`touch-action`) y no tiene pellizco ni inercia propia; el rechazo de palma lo hace el sistema. En C++ no hay navegador, así que se implementó en `DocumentView::touchEvent`.

| # | Ítem | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 3.1 | Un dedo desplaza la página y no dibuja (preferencia «Los dedos dibujan» apagada) | hecho (corregido) + a-probar-por-el-usuario | `penaudit` con `QTouchEvent` | **Hallazgo grave**: el viewport no aceptaba eventos táctiles y los ratones emulados por Qt (`MouseEventSynthesizedByQt`) se descartan a propósito, así que en una pantalla táctil real el dedo probablemente no hacía **nada**. Ahora el viewport acepta `QTouchEvent` y los trata. Verificado con eventos sintéticos; **no con una pantalla táctil real** |
| 3.2 | Con «Los dedos dibujan» activo, un dedo dibuja (sin presión: ancho constante) | hecho + a-probar | `penaudit` | — |
| 3.3 | Rechazo de palma: un dedo que toca mientras el lápiz está cerca (cualquier evento del lápiz en los últimos 700 ms) se ignora, también los ratones que el sistema emula del táctil; el lápiz sigue dibujando con la mano apoyada | hecho (sintético) + a-probar | `penaudit` (4 comprobaciones) | Heurística propia (Electron confía en el sistema). Si el controlador no envía eventos de proximidad ni de movimiento sin contacto, la palma no se detecta |
| 3.4 | Inercia al soltar un gesto rápido | hecho + a-probar | `penaudit`: 169 px más tras levantar el dedo, se detiene sola; si el dedo se para antes de levantar no hay inercia | Extra respecto a Electron (el navegador la hace al desplazar con el dedo) |
| 3.5 | Pellizco de dos dedos: zoom alrededor del centro y desplazamiento | hecho + a-probar | `penaudit`: dedos a 100 px que acaban a 260 px → ×2,6; no dibuja; un segundo dedo durante un trazo de dedo descarta el trazo | **Extra**: Electron no tiene pellizco |
| 3.6 | Láser y anillo de goma ignoran el dedo | hecho | `penaudit` (láser) | — |

## 4. Puntas, presión y tableta

| # | Ítem | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 4.1 | Cuatro puntas (Bolígrafo, Tinta, Pluma, Pincel) guardadas como lista de grosores por punto | hecho | ctest `brush`; selftest de ajustes (cuatro tarjetas, elegir punta); los trazos se guardan como `.xopp` normales (ida y vuelta) | La tinta y el pincel terminan en punta al soltar (igual que Electron, documentado en su README) |
| 4.2 | Grosor 0,1 a 60 (deslizador y casilla) | hecho | selftest de ajustes | — |
| 4.3 | Curva de presión Bézier con dos puntos, 3 ajustes y zona muerta | hecho | ctest `tablet`; selftest (arrastrar el segundo asa cambia solo ese punto, presets) | — |
| 4.4 | Ver la presión: barras, curva aplicada, puntos recientes, inclinación, botones, dispositivo, niveles distintos, hoja de prueba | hecho (sintético) + a-probar | selftest («live readout shows pressure, tilt and device», hoja de prueba por eventos reales) | Los niveles de presión reales y la inclinación solo se ven con la tableta |
| 4.5 | Botones del lápiz: lateral 1, lateral 2, extremo borrador, con acción (lápiz, resaltador, gomas, seleccionar, lazo, relleno, deshacer, rehacer, nada, mano, cuadro de herramientas) | hecho (sintético) + a-probar | selftest (botones 2/4/32 como botón derecho/central y borrador; deshacer y rehacer al pulsar; herramienta prestada que se devuelve) | El láser no se puede asignar (igual que Electron) |
| 4.6 | Atajos de teclado asignables (teclas exprés del mando, rueda del dial, botones extra del ratón): más de 30 acciones | hecho | `penaudit`: 41 acciones; selftest de ajustes (asignar tecla, combinación física con Mayús, rueda, botón atrás, prioridad sobre los atajos normales) | Solo lo que el controlador convierte en teclas, como en Electron |
| 4.7 | Cómo se conecta el hardware (controlador XP-Pen, Windows Ink) | a-probar-por-el-usuario | — | Ver pasos |
| 4.8 | Control de la relación presión → grosor del bolígrafo (`TabletSettings::penLook`) | hecho (era «falta») | `penaudit`: el selector existe en el diálogo, empieza en «prototipo»; elegir «Electron» lo aplica y el siguiente trazo sale con `max(0,05; 1,5·p)·size`; vuelve; se guarda en prefs solo si no es el valor por defecto (selftest `penchecks`) | — |
| 4.9 | Diagnóstico del lápiz (Ayuda) | hecho | selftest del área chrome | — |

## 5. Valores por defecto y botón lateral

| # | Ítem | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 5.1 | Extremo borrador = borrar trazo entero; lateral 1 = cuadro de herramientas donde tocas; lateral 2 = mano; «Mano» en la lista de cada botón | hecho | selftest («pen barrel button 1 requests the tool box», «button 2 drags the page», «defaults barrel…») | a-probar con el controlador XP-Pen (puede reasignar los botones) |
| 5.2 | Mano con ratón/lápiz mueve la página | hecho | ver 1.7 | — |

## 6. Presets de tamaño en la barra

| # | Ítem | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 6.1 | Puntos de tamaño creciente por herramienta (lápiz, resaltador, goma entera y parcial compartidos, texto); el activo resaltado; no aparecen sin tamaño | hecho | selftest de ajustes (`sizes: …`) | — |
| 6.2 | Clic derecho o Alt+clic guarda ahí el tamaño actual; se recuerdan entre sesiones | hecho | selftest (guardar, prefs.json, solo ese botón) | Además, pulsación larga (extra) |
| 6.3 | Los presets siguen en la barra con la ventana estrecha | hecho (era «parcial») | ver 9.1 | — |

## 7. Volver a la herramienta de antes, inicio del trazo

| # | Ítem | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 7.1 | Tras un objeto: Seleccionar; Esc o clic en vacío vuelven a la herramienta anterior y el clic sigue con ella; clic en el objeto no vuelve; elegir a mano no recuerda | hecho | selftest («after an inserter…», «Esc returns», «click on the empty page returns… and that tool draws», «choosing Select on purpose clears the return») | — |
| 7.2 | Inicio del trazo sin punto de otro grosor (3 causas del README) | hecho | `penFeelChecks`: primer ancho = segundo, grosor mínimo 0,05, un toque deja punto al soltar | — |

## 8. Opciones de herramienta en el mismo icono

| # | Ítem | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 8.1 | Lápiz ▾ abre «Lápiz y tableta» | hecho (era «falta») | `penaudit`: el botón del lápiz tiene la flecha; un clic en el icono solo elige el lápiz; un clic en la flecha abre el diálogo | La flecha está en el borde derecho del botón (estilo de botón dividido de Qt), no en la esquina inferior derecha como en Electron |
| 8.2 | Relleno, plano cartesiano, espacio 3D y figuras ▾ abren sus paneles | cubierto por otras áreas | las flechas se ven en la captura de la barra; sus paneles las prueban sus áreas | — |

## 9. Barra principal

| # | Ítem | Estado | Cómo se verificó | Desviaciones |
|---|---|---|---|---|
| 9.1 | La barra no oculta lápiz, goma, colores ni tamaños con la ventana estrecha | hecho (corregido) | `penaudit` mide a 2600, 1920, 1400 y 1200 px que los 53 botones están dentro de la ventana; capturas `pen_toolbar_*.png` | **Corregido**: a 1400 px el selector de tamaños, el zoom y «página nueva» quedaban tras la flecha de desbordamiento. Ahora hay dos filas: `toolBar()` (archivo, deshacer, herramientas) y `styleBar()` (colores, tamaños, zoom, página nueva); la segunda baja solo si no caben en una línea (como el `flex-wrap` de Electron). Por debajo de ~1100 px la primera fila sola ya no cabe y vuelve la flecha de Qt |

## 10. Rendimiento (equivalentes de la tabla de GPU/CPU de Electron)

No hay contadores de GPU de Windows aquí: se mide CPU del proceso y tiempos por evento. «Electron» = lo que dice su README (GPU, otra magnitud).

| # | Carga | Estado | Cifras C++ (esta máquina, plataforma offscreen, ver nota) |
|---|---|---|---|
| 10.1 | Reposo | hecho | 0 ms de CPU en 2 s (`penaudit`), Electron: 0 % |
| 10.2 | Dibujar con lápiz | hecho | ver la sección 11 |
| 10.3 | Dibujar con brillo | hecho | p99 de 0,11 ms por evento con el halo en vivo (sección 11) |
| 10.4 | Desplazar / zoom | hecho | selftest: paso de scroll 60 px, media 1,7 ms (p50 0,25); paso de zoom ×1,08, p50 ~6 ms; teselas rugosas mientras se mueve y nítidas a los 150 ms |
| 10.5 | Goma barriendo y anillo | hecho | `eraserTable`: 300 eventos sobre 20000 trazos; el anillo repinta solo su rectángulo |
| 10.6 | Miniaturas (solo la página que cambió; sin brillo; panel cerrado = nada) | hecho (corregido) | **Corregido**: antes se dibujaban en el hilo de la interfaz (1 s con 20000 trazos); ahora en un hilo aparte desde una copia de las capas visibles. `penaudit`: la miniatura de 20000 trazos está lista en ~0,8 s y el giro más largo de la interfaz mientras tanto es de 14 ms (mediana de 3 repeticiones) |
| 10.7 | Preferencia «Efectos» | hecho | existe en Preferencias (área de ajustes) | — |
| 10.8 | Memoria acotada | parcial | ver sección 12 | — |

## 11. Números del camino del lápiz (confirmados al final)

1500 eventos de un trazo de lápiz con presión, 0 / 8000 / 20000 trazos de 60 puntos más en la página (`pointerTable`, mismas condiciones que la tabla inicial del selftest completo: zoom que ajusta al ancho, 6 teselas visibles; el equipo estaba además ocupado por otras compilaciones y selftests, así que son cifras pesimistas). Entre paréntesis, la medición de antes de empezar el audit.

| Trazos extra | evento (herramienta) | evento + repintado (media / p99) | soltar el lápiz (commit + parche de teselas) | deshacer |
|---|---|---|---|---|
| 0 | 0,005 ms | 0,017 / 0,050 ms (0,034 / 0,055) | 0,36 ms (0,48) | 11,1 ms, primera vez en frío (173) |
| 8000 | 0,005 ms | 0,014 / 0,036 ms (0,133 / 0,066; el máximo de 152 ms de entonces no se repitió) | 0,44 ms (1,04) | 1,7 ms (5,8) |
| 20000 | 0,009 ms | 0,043 / 0,088 ms (0,030 / 0,047) | 0,80 ms (1,31) | 5,3 ms (5,6) |

Sin regresión: nada del camino del puntero es O(elementos). Con `XL_SELFTEST_ONLY=pointer` (zoom 100 %, 4 teselas) salen 0,016 / 0,015 / 0,017 ms por evento, soltar 0,24 / 0,39 / 0,79 ms y deshacer 1,6 / 2,2 / 4,8 ms.

Con el brillo activado (`penaudit`, zoom 2): evento + repintado, media 0,028 ms y p99 0,110 ms (sin brillo: 0,013 y 0,035). La latencia real de pantalla y la presión de un lápiz físico no están medidas.

## 12. Los siete pendientes de informes anteriores

| Pendiente | Resultado |
|---|---|
| (a) `penLook` sin control en el diálogo | **Hecho**: selector en «Lápiz y tableta» (4.8) |
| (b) miniaturas en el hilo de la interfaz | **Hecho** (10.6) |
| (c) fallo raro de «stroke added while workers draw» | **No se pudo reproducir**: 300 repeticiones seguidas sin fallo (timings distintos, `XL_STALE_LOOP=300 XL_SELFTEST_ONLY=penstale`). Se revisó el código y se cerraron tres huecos que producen exactamente ese síntoma (imagen distinta en una tesela) con ciertas temporizaciones: (1) una tesela «rugosa» (sin antialiasing) que llegaba de un hilo cuando el movimiento ya había parado se quedaba rugosa hasta el siguiente repintado (`collectDone` ahora la vuelve a pedir nítida); (2) un parche pendiente de un trazo recién soltado se aplicaba **encima** de una tesela que ya lo contenía (bordes dobles; en un resaltador, el doble de oscuro): `processJobs` y `collectDone` ahora aplican antes los parches; (3) una cola de teselas esperando un hilo libre no se reanudaba si el trabajo cancelado no publica resultado (`cancelFar` ahora reanuda). Sigue sin estar «demostrado» que fuera eso: queda el comando de estrés para cazarlo |
| (d) brillo en vivo; resaltador en vivo = Multiply | **Hecho**: el resaltador usa Multiply en la capa viva; el brillo se dibuja con dos bandas suaves (capas a un cuarto de resolución estiradas, el desenfoque sale gratis) ajustadas al perfil del brillo definitivo (media 14 de 255 de diferencia en la zona del halo). Es una aproximación: al soltar, el brillo exacto sustituye al vivo |
| (e) tirón de 0,6 s al arrastrar 11000 trazos | **Reducido** de 583 ms a ~160 ms: el mapa de bits de la selección se dibuja en hilos (trozos de la selección en orden z, compuestos en orden; una selección con resaltador se dibuja de una vez porque Multiply no compone en capas aparte) y el resto es la extracción del documento (16 ms). Medido con 11033 trazos por `XL_SELFTEST_ONLY=selection` |
| (f) 1,4 GB de memoria en el selftest | Ver sección 13 |
| (g) barra a 1400 px | **Hecho** (9.1) |

## 13. Memoria (f)

Marcas por área en el selftest completo (`memMark`, en `selftest.cpp`): hasta el área de texto el pico es 188 MB; con chrome 246 MB; con ajustes y matemáticas, 246 MB. **El salto a 1407 MB ocurre dentro de la sección PDF** y el proceso vuelve a 233 MB al terminar. Marcas dentro de esa sección (`pdfselftest.cpp`, ejecutada sola): 97 MB tras abrir, 157 MB tras exportar PDF, **487 MB de pico en el paso «imprimir»** (`QPrinter::HighResolution`, 1200 ppp) y 181 MB después. O sea: no es algo que la aplicación retenga, es el transitorio de rasterizar el fondo PDF al imprimir (ya acotado a ~36 Mpx = 144 MB por página en `core/pageexport.cpp`) y, en el proceso del selftest completo, el montón fragmentado por todos los fixtures anteriores lo infla hasta 1,4 GB. Más los renders de 300 y 600 ppp que hace la propia prueba (una A4 a 600 ppp son 35 Mpx = 140 MB). Lo que sí retiene la aplicación está acotado: caché de teselas 320 MB, caché de fondos PDF 320 MB, caché de brillos 96 MB, 300 miniaturas (~25 MB), copias de la selección mientras se arrastra (máximo ~128 MB repartidos en los hilos), y el historial de 500 pasos (**acotado por número, no por bytes**: 500 borrados enormes pueden ocupar mucho; no se tocó). Con un documento de 7722 trazos en reposo el proceso ocupa ~125 MB. Veredicto: nada que acotar en la aplicación real salvo, como mejora futura, un presupuesto en bytes para el historial.

## 14. Para probar con hardware real (a-probar-por-el-usuario)

1. **Presión y puntas** (Deco Pro): Herramientas ▸ Lápiz y tableta ▸ dibuja en la hoja de prueba; mira «Niveles de presión distintos vistos» (si es 1 o 2 el controlador no usa Windows Ink) y que Inclinación se mueva. Prueba cada punta.
2. **Botones del lápiz**: botón lateral 1 sobre la página abre el cuadro de herramientas donde tocas; botón 2 + arrastrar mueve la hoja y al soltar vuelve la herramienta; extremo borrador borra trazos. Si no pasa nada, el controlador está reasignando el botón: déjalo en «clic derecho» / «borrador».
3. **Mando ACK05**: en el controlador de XP-Pen asigna cada tecla a `Ctrl+Alt+Mayús+1…8`, luego en el diálogo elige la acción de cada «Tecla n».
4. **Pantalla táctil**: con el lápiz lejos, un dedo arrastra la página y al soltar rápido sigue un momento; dos dedos hacen zoom; con «Los dedos dibujan» activado un dedo dibuja. **Palma**: apoya la mano mientras escribes con el lápiz; no debe moverse nada. Si se mueve, avísame con qué controlador (necesito saber si manda eventos de proximidad).
5. **Brillo en vivo**: activa el brillo y dibuja; compara con el trazo al soltar (debe parecerse, no ser idéntico).
6. **Segundo monitor / escala 150 %**: arrastra la ventana entre pantallas con distinta escala y dibuja; no probado aquí.
7. **Xournal++ oficial**: abre en él un `.xopp` con trazos de cada punta y de resaltador para ver los anchos variables (no probado aquí).

## 15. Archivos compartidos que toqué (ediciones pequeñas) y huecos que quedan

Compartidos: `app/documentview.{h,cpp}` (dedos, pellizco, inercia, palma, giro de página con la rueda, Shift+rueda, capas de halo, Multiply en vivo, tres arreglos de teselas), `app/mainwindow.{h,cpp}` (segunda fila de la barra, flecha del lápiz, zoom ×1,2), `app/tool.h` (`ToolInfo::optionsCommand`), `app/selftest.cpp` (modos `penaudit`, `penstale`, `pointer`, `math` y las marcas de memoria), `app/API.md`. Del área de ajustes: `tabletdialog.{h,cpp}` (selector de `penLook`), `controller.cpp` y `settingsselftest.cpp` (la barra de estilo, paso 1,2). Del área chrome: `panels.cpp`, `parts.h` (miniaturas en un hilo). De selección: `selectionimpl.cpp` (mapa de bits de arrastre en hilos; una línea de depuración detrás de `XL_SEL_PROF`) y `install.cpp` (un `#include <QToolButton>` que faltaba y rompía la compilación). De PDF: `pdfselftest.cpp` (marcas de memoria).

Huecos y decisiones:
- **Pantalla táctil, rechazo de palma, inercia y pellizco**: solo con eventos sintéticos. Lo más arriesgado de esta área: si Windows no entrega `QTouchEvent` al viewport tal como se espera, el dedo no hará nada; ver los pasos de la sección 14.
- **Historial por bytes**: sin presupuesto en bytes (solo 500 pasos). No lo toqué: es una decisión de diseño (qué se pierde primero).
- **Flecha de «Lápiz ▾»**: queda a la derecha del botón y no en la esquina como en Electron (el botón dividido de Qt); a 1000 px de ancho o menos la fila de herramientas por sí sola ya no cabe y vuelve la flecha de desbordamiento de Qt.
- **Brillo en vivo**: aproximado; no idéntico al brillo definitivo (se sustituye al soltar).
- **(c)** sin reproducir; ver sección 12.

## 16. Estado de la regresión al terminar

- `ctest --test-dir build-p3`: 23 de 23.
- `XL_SELFTEST_ONLY=penaudit`: 63 PASS, 0 FAIL. `XL_SETTINGS_ONLY=1`: 150 PASS, 0 FAIL. `XL_SELFTEST_ONLY=chrome`: 195 PASS, 0 FAIL. `XL_SELFTEST_ONLY=pdf`: todo PASS. `XL_SELFTEST_ONLY=selection`: todo PASS. Estrés de teselas: 300 + 70 repeticiones (las 70 con 4 procesos compitiendo por la CPU) sin un solo fallo.
- Selftest completo (`--selftest`): una pasada con todo el árbol llegó a 1000 PASS con el único fallo de un diálogo flotante que se corrigió después (en `chrome` suelto ya pasa), y **la última pasada se quedó colgada en la prueba de paridad de selección «glow: a click on the button opens the panel»** (otra área, en curso: pulsar el botón Brillo abre el menú de Qt con un bucle anidado y la prueba no lo cierra). Como esa parte no es de esta área no la toqué; hasta que se arregle, el selftest completo no termina por ahora y hay que correr las áreas por separado (`XL_SELFTEST_ONLY=...`).
