# Xournal Lab

**Un cuaderno digital para escribir a mano en Windows, hecho para trabajar con lápiz y tableta, que lee y guarda archivos `.xopp` de [Xournal++](https://github.com/xournalpp/xournalpp).**

> **Estado: alpha (0.1.0-alpha).** Funciona y se usa a diario, pero le falta trabajo y puede tener errores. Haz copia de tus documentos importantes antes de editarlos con esta versión.

*English, in short:* Xournal Lab is an independent handwriting-notebook app for Windows that reads and writes Xournal++ `.xopp` files. It has two implementations of the same program: a C++/Qt 6 application (the main one, built for a fluid pen and tablet input) and the Electron + TypeScript application it was ported from. It is an alpha. This repository is a GitHub fork of Xournal++ only in the sense of a family link: the code here is a separate code base, written from observed behaviour and the file format, not copied from Xournal++. Licensed under GPL-2.0-or-later, like Xournal++.

## Qué es

Xournal Lab nació como un experimento sobre Xournal++ v1.3.8: ¿cuánto se entiende de un programa leyendo su código y cuánto haciendo ingeniería inversa de su binario? De ahí salió una versión propia que abre, edita y guarda `.xopp`, y después un **port a C++ con Qt 6** pensado para que el lápiz sea fluido y la página no se ponga lenta aunque tenga miles de trazos.

Hay dos aplicaciones en este repositorio:

| Carpeta | Qué contiene | Estado |
|---|---|---|
| [`cpp/`](cpp) | **Aplicación principal** en C++20 y Qt 6 (Widgets, Qt6Pdf). Interfaz en español, tema neón y clásico. | La que se usa y se desarrolla. |
| [`app/`](app) | Versión original en Electron + TypeScript. | Es la referencia de comportamiento del port; ya no es el foco. |

Los documentos son intercambiables con Xournal++: las cosas propias (planos, figuras, fórmulas, papeles especiales) se guardan como trazos y textos normales con atributos extra, así que el archivo sigue siendo un `.xopp` válido.

## Qué hace

- **Escribir a mano** con lápiz, resaltador y gomas (trazo completo o solo la zona tocada), sensible a la presión, con curva de presión y zona muerta configurables.
- **Seleccionar** por rectángulo, lazo u objeto; mover, redimensionar, rotar y espejar con transportador; copiar, pegar, duplicar y cambiar el orden de apilado.
- **Texto, imágenes, formas** (con reconocimiento automático de formas) y **fórmulas LaTeX** con vista previa en vivo, sin instalar LaTeX.
- **Objetos matemáticos**: plano cartesiano, espacio 3D, figuras, recta numérica, diagramas de Venn y relleno de regiones.
- **Papeles**: liso, rayado, cuadriculado, punteado, isométricos y especiales (milimetrado, plano cartesiano, trigonométrico, polar, logarítmico, Cornell). Se puede ajustar el **espaciado** y la **opacidad** de las líneas.
- **PDF**: usar un PDF como fondo, anotarlo, exportar a PDF o PNG e imprimir.
- **Páginas y capas** con miniaturas, vista continua o página a página, dos páginas enfrentadas y búsqueda en textos y fórmulas.
- **Zoom** de 25 % a 2000 %, con «ajustar al ancho» que sigue el tamaño de la ventana.
- **Lápiz y tableta en Windows**: botones del lápiz asignables (con «mantener para usar», como en Xournal++), sin onda de Windows al tocar, sin panel de escritura a mano sobre los cuadros de texto, y una hoja de prueba que muestra lo que llega del controlador.
- **Seguridad al guardar**: nunca sobrescribe el archivo de otro en silencio, el autoguardado escribe una copia de recuperación aparte.

## Cómo usarlo

La guía completa está en **[docs/USO.md](docs/USO.md)**. Para compilarlo: **[docs/COMPILAR.md](docs/COMPILAR.md)**.

En resumen, en Windows 10/11 de 64 bits:

1. Compila la aplicación C++ (MSYS2 UCRT64, Qt 6, CMake, Ninja) o genera el paquete portátil o el instalador con `cpp\package.bat` y `cpp\installer.bat`.
2. Abre `xl_app.exe`, o arrastra un `.xopp` encima.

## Qué está verificado y qué no

Esto es una alpha y lo decimos sin adornos:

- La aplicación C++ tiene 25 pruebas unitarias (`ctest`) y una prueba de interfaz en pantalla virtual (`xl_app --selftest`, cientos de comprobaciones con eventos sintéticos de Qt). Pasan en el equipo de desarrollo.
- **Nada de lo que depende de un lápiz físico se puede probar con eventos sintéticos.** El comportamiento del lápiz XP-Pen Deco Pro / X3 Pro en Windows 10 se ajustó con registros reales, pero se ha probado con un solo modelo y un solo equipo. Otros lápices o controladores pueden comportarse distinto. Las notas por área están en [`cpp/PARIDAD/`](cpp/PARIDAD) (por ejemplo [`winpen.md`](cpp/PARIDAD/winpen.md)), con la lista de lo que falta por probar.
- Que **Xournal++ oficial abra** todo lo que genera esta aplicación (fórmulas, objetos matemáticos) no se ha comprobado con la aplicación oficial.
- Algunas pruebas usan documentos personales que **no** se publican, así que esas no se pueden repetir desde este repositorio.
- El instalador de Windows no está firmado: SmartScreen puede mostrar un aviso la primera vez.
- Faltan funciones de Xournal++ (plugins en Lua, plantillas de página, exportar a SVG, regla y escuadra dibujadas en la página, entre otras). La tabla de paridad completa está en [`cpp/PARIDAD/xpp_features.md`](cpp/PARIDAD/xpp_features.md).

## Relación con Xournal++

Este proyecto es un **fork en GitHub de [xournalpp/xournalpp](https://github.com/xournalpp/xournalpp)** a efectos de enlace y de reconocimiento, pero **el código que contiene es otra base de código**: la rama principal de este repositorio no comparte historial con Xournal++.

- Xournal++ es el programa de referencia. Aquí se replica **qué hace, dónde está y cómo se comporta**, a partir del manual, del formato `.xopp` y de la observación del programa. La documentación de paridad lo dice así en [`cpp/PARIDAD/xpp_features.md`](cpp/PARIDAD/xpp_features.md): *se replica qué hace, no se copia su código.* Aun así, durante el desarrollo se leyó el código fuente público de Xournal++ v1.3.8, y no podemos garantizar que ninguna parte sea derivada. Por eso este proyecto usa la **misma licencia que Xournal++**.
- No está afiliado ni respaldado por el equipo de Xournal++. Si buscas el programa maduro y completo, usa [Xournal++](https://xournalpp.github.io).
- El historial original de Xournal++ sigue disponible en la rama `master` de este fork, sin cambios.

## Licencia

**GPL-2.0-or-later**, igual que Xournal++. El texto está en [LICENSE](LICENSE) (GNU GPL versión 2); se permite usarlo bajo esa versión o cualquier posterior.

Las bibliotecas que se enlazan o incluyen conservan su propia licencia: Qt 6 (LGPL v3, enlazada dinámicamente), pdfium dentro de Qt6Pdf, y en la versión Electron KaTeX y pdf.js. Quien redistribuya los binarios debe cumplir con ellas.
