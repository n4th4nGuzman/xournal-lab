# Guía de uso de Xournal Lab

Guía de la aplicación C++ (`cpp/`). La versión Electron (`app/`) se comporta igual en lo básico; la tabla de herramientas de abajo vale para las dos. Versión 0.1.0-alpha. Las diferencias entre esta guía y la aplicación se pueden reportar en un *issue*.

## 1. Abrir la aplicación

- **Instalada o portátil:** doble clic en `xl_app.exe`, o arrastra un archivo `.xopp` (o un PDF) sobre el ejecutable.
- **Desde la línea de comandos:**
  - `xl_app.exe [archivo.xopp]` abre un documento.
  - `xl_app.exe --screenshot salida.png [--page N] [--zoom Z] [archivo.xopp]` guarda una captura de la ventana y sale.
- Sin documento, aparece la **página de bienvenida** con tarjetas para empezar un documento nuevo sobre el papel que elijas y la lista de recientes.

Dónde guarda sus datos:

| Qué | Dónde |
|---|---|
| Preferencias | `%APPDATA%\xournal-electron\prefs.json`, el mismo archivo que usa la versión Electron: las dos comparten preferencias |
| Archivos recientes y estado de la ventana | Registro de Windows, `HKCU\Software\XournalLab\XournalLabCpp` |
| Copias de recuperación (autoguardado) | Subcarpeta `autosave` de la carpeta de datos local de Qt, bajo `%APPDATA%` |

## 2. La pantalla

- **Fila de herramientas** arriba: lápiz, resaltador, gomas, selección, texto, formas, fórmulas, objetos matemáticos, mano, láser.
- **Fila de estilo** debajo: color (paleta y color personalizado), grosor o tamaño de la herramienta activa y estilo de línea.
- **Barra de colores** a un lado (se puede ocultar o pasar al lado derecho en el menú *Ver*).
- **Barra del fondo** (Diario): tipo de papel con sus opciones, color, formato de página.
- **Barras laterales**: miniaturas de páginas y capas.
- **Menús**: Archivo, Editar, Ver, Navegación, Diario, Herramientas, Ayuda. Los atajos aparecen en cada entrada.

### Grosores

La fila de estilo muestra los grosores de la herramienta activa. Con las herramientas que no tienen tamaño propio (selección, mano, relleno, fórmulas, láser), muestra los **grosores del lápiz**: así puedes elegir uno sin volver al lápiz, y vale también para plano, 3D, figuras, recta numérica y Venn. La goma tiene su propio radio y el texto su propio tamaño.

Para cambiar un valor guardado de la lista, mantén pulsado (o clic derecho, o Alt+clic) sobre su botón: guarda ahí el tamaño actual.

## 3. Herramientas

| Herramienta | Tecla | Notas |
|---|---|---|
| Lápiz | P | Sensible a la presión. |
| Resaltador | H | |
| Goma (trazo completo) | E | Borra el trazo entero. |
| Goma parcial | B | Corta el trazo por donde pasa. |
| Seleccionar objeto / rectángulo | S | Mover, redimensionar con las asas, rotar con el asa circular (con transportador; Mayús ajusta a 15°). |
| Lazo | L | Selecciona lo que queda dentro de una región libre. |
| Espacio vertical | V | Clic en una línea y arrastra: lo de abajo se mueve. |
| Texto | T | Clic en la página. Ctrl+Intro o clic fuera confirma, Esc cancela. |
| Formas | G | Rectángulo, elipse, línea, flechas, ejes. Mayús: cuadrados, círculos y ángulos de 15°. |
| Fórmula LaTeX | X | Clic en la página: cuadro con vista previa en vivo y paleta de símbolos. Ctrl+Intro inserta, Esc cancela. |
| Plano cartesiano | C | Objetos matemáticos generados: se guardan como trazos y textos con atributos extra. |
| Espacio 3D | D | |
| Figura | F | |
| Recta numérica | N | |
| Diagrama de Venn | J | |
| Relleno | U | |
| Mano | M | Arrastra para mover la página. |
| Láser | K | Una estela que se desvanece; no cambia el documento. |

Edición con la selección: Ctrl+A selecciona todo en la página, Ctrl+C / X / V copian, cortan y pegan (Ctrl+V pega también imágenes del sistema), Ctrl+D duplica, Ctrl+↑ / Ctrl+↓ traen al frente o envían al fondo. Deshacer es Ctrl+Z y rehacer Ctrl+Y.

## 4. Papeles y fondos

En la barra **Fondo** eliges el tipo de papel:

- **Estilos básicos:** liso, rayado, reglado, cuadriculado, punteado y los dos isométricos.
- **Papeles especiales:** milimetrado, plano cartesiano, trigonométrico, polar, logarítmico, isométrico y Cornell. Cada uno tiene sus opciones (unidad, subdivisión, ciclos, etc.) al lado del selector.

Además:

- **Espaciado** (en mm): la separación entre líneas, cuadros o puntos. Aparece solo con los estilos básicos; los papeles especiales ya traen su escala en sus propias opciones. Al cambiar el tipo de papel, el espaciado vuelve al normal.
- **Opacidad** (5 a 100 %): qué tan marcadas se ven las líneas. Vale para cualquier papel con líneas y se mantiene al cambiar de tipo.
- **Color del papel**, **formato** (A3, A4, A5, Carta, Legal o tamaño en mm) y el botón de girar la hoja.
- **Todas las páginas:** marca la casilla para aplicar el fondo y el formato a todo el documento.

Cada cambio se puede deshacer. El espaciado y la opacidad se guardan en el fondo de cada página; otros programas los ignoran y dibujan el papel con su aspecto normal.

## 5. Zoom y vista

- Rueda con Ctrl, `Ctrl++` / `Ctrl+-`, o los botones de la barra. El zoom va de **25 % a 2000 %**. `Ctrl+0` vuelve al 100 %.
- **Ajustar al ancho** (botón de los cuatro ángulos junto a la lupa): la hoja llena el ancho de la ventana y **se mantiene así** cuando cambias el tamaño de la ventana, abres o cierras un panel lateral o pasas a dos páginas. Cualquier otro zoom que elijas lo desactiva.
- **Ajustar a la página**, **vista continua o página a página**, **dos páginas** enfrentadas, **pantalla completa** (F11) y **modo presentación** (F5).

## 6. Páginas, capas y búsqueda

- **Páginas:** miniaturas en la barra lateral; añadir, borrar y mover desde ahí o desde el menú *Diario*. Navegación con primera/anterior/siguiente/última y *Ir a la página* (Ctrl+G).
- **Capas:** la capa activa es donde van los trazos nuevos; el ojo muestra u oculta, doble clic renombra. Ocultar y la capa activa son solo de pantalla y no se guardan en el archivo.
- **Buscar** (Ctrl+F): busca en textos y en el código LaTeX de las fórmulas, en todas las páginas.

## 7. PDF

- Abre un PDF para usarlo como fondo de las páginas y anota encima.
- Exporta a **PDF** (con rango de páginas) o a **PNG**. Los fondos PDF se exportan como imagen (150 ppp por defecto), no como páginas vectoriales.
- **Imprimir** desde el menú Archivo.

## 8. Guardar y recuperar

- Al guardar un documento que abriste desde un archivo, la aplicación pregunta dónde guardarlo (nombre sugerido `<archivo>-lab.xopp`) y no reemplaza ese archivo. Solo guarda directo sobre un archivo que ella misma guardó en esta sesión.
- *Guardar copia* no cambia la ruta ni el estado de «sin guardar».
- Con cambios pendientes, al cerrar pide confirmación.
- **Autoguardado** (Preferencias, desactivado por defecto): escribe una copia de recuperación aparte, no sobre el archivo original, y solo si hay cambios sin guardar. Se abre con *Archivo → Abrir copia de recuperación*.

## 9. Lápiz y tableta (Windows)

Los ajustes están en *Herramientas → Lápiz y tableta…* y en *Preferencias* (Ctrl+,).

- **Hoja de prueba:** dibuja con la punta elegida y muestra en vivo el dispositivo, la presión (cruda y con la curva), la inclinación, los botones y una traza de los eventos sin procesar. Si un botón no responde, copia esa traza al abrir un *issue*.
- **Curva de presión y zona muerta:** ajustan cuánto grosor da cada nivel de presión. La presión que llega es la de Windows Ink (de 0 a 1024 niveles), aunque el controlador de la tableta vea más niveles internamente.
- **Botones del lápiz:** en *Atajos de teclado*, «Asignar tecla…» captura el botón que pulses. Un botón asignado a una herramienta funciona como en Xournal++: **la herramienta está activa mientras mantienes el botón, y al soltarlo vuelves a la anterior**.
- **Efectos de Windows:** la aplicación pide a Windows que no dibuje la onda al tocar y que no abra el panel de escritura a mano sobre los cuadros de texto. Afecta solo a esta aplicación y no cambia ninguna configuración del sistema.
- **Rueda del ratón en formularios:** no cambia un selector hasta que lo has seleccionado con un clic.

### Consejos para tabletas XP-Pen

Probado con una XP-Pen Deco Pro MW (Gen 2) y lápiz X3 Pro en Windows 10, sin pantalla táctil. No se ha probado con otros modelos.

- Deja **Windows Ink activado** en el controlador: da presión e inclinación y es lo que usa Qt 6. El modo de ratón pierde presión y botones en el aire.
- El controlador envía los botones laterales como botones del ratón (derecho y central) además de como botones de lápiz; la aplicación lo tiene en cuenta.
- No combines una macro del controlador de «mantener para cambiar de herramienta» con la función de mantener de la aplicación.
- Si el panel de escritura a mano aparece igual, desactívalo en *Configuración de Windows → Dispositivos → Lápiz y Windows Ink*.

## 10. Preferencias

*Editar → Preferencias* (Ctrl+,): paletas de colores, tamaños de herramientas, sistema de entrada (clases de dispositivo: ratón, lápiz, borrador, pantalla táctil), asignación de botones, zoom, autoguardado, apariencia y más. Se guardan en `prefs.json` (ver arriba).

## 11. Problemas frecuentes

| Síntoma | Qué probar |
|---|---|
| El lápiz no dibuja o dibuja sin presión | Abre la hoja de prueba. Si el dispositivo aparece como ratón, activa Windows Ink en el controlador. |
| Un botón del lápiz no hace nada | Mira la traza de eventos sin procesar en la hoja de prueba y revisa que no esté asignado en dos sitios (Preferencias y atajos). |
| La herramienta mano o un botón se comporta de forma distinta con otro modelo de lápiz | Se ajustó con un solo modelo. Adjunta la traza de eventos de la hoja de prueba en un *issue*. |
| «El programa no inicia en otra PC» | Usa el paquete portátil o el instalador, que incluyen las bibliotecas de Qt; el ejecutable suelto de `build\` necesita MSYS2 en el PATH. |
| SmartScreen avisa al instalar | El instalador no está firmado. «Más información → Ejecutar de todas formas». |
