# Xournal Lab

Aplicación para escribir a mano en Windows con lápiz y tableta. Abre y guarda archivos `.xopp` de [Xournal++](https://github.com/xournalpp/xournalpp).

Versión: 0.1.0-alpha.

*English summary:* Xournal Lab is a handwriting notebook application for Windows that reads and writes Xournal++ `.xopp` files. The repository has two implementations of the same program: a C++/Qt 6 application and the Electron + TypeScript application it was ported from. Version 0.1.0-alpha. The repository is a GitHub fork of Xournal++ for linking purposes; its code is a separate code base. License: GPL-2.0-or-later.

## Descripción

Xournal Lab comenzó como un experimento sobre Xournal++ v1.3.8: leer su código fuente frente a hacer ingeniería inversa de su binario. Del experimento salió una versión que abre, edita y guarda `.xopp`, y después un port a C++ con Qt 6.

El repositorio contiene dos aplicaciones:

| Carpeta | Contenido |
|---|---|
| [`cpp/`](cpp) | Aplicación en C++20 y Qt 6 (Widgets, Qt6Pdf). Interfaz en español, temas neón y clásico. |
| [`app/`](app) | Versión en Electron + TypeScript. Es la referencia de comportamiento del port C++. |

Los elementos propios (planos, figuras, fórmulas, papeles especiales) se guardan como trazos y textos con atributos adicionales, de modo que el archivo sigue siendo un `.xopp` válido.

## Funciones

- Lápiz, resaltador y gomas (trazo completo o solo la zona tocada), con presión, curva de presión y zona muerta configurables. Puntas: bolígrafo, tinta, pluma y pincel.
- Selección por rectángulo, lazo u objeto; mover, redimensionar, rotar y espejar con transportador; copiar, pegar, duplicar y cambiar el orden de apilado.
- Texto, imágenes, formas (con reconocimiento automático de formas) y fórmulas LaTeX con vista previa, sin instalar LaTeX.
- Objetos matemáticos: plano cartesiano, espacio 3D, figuras, recta numérica, diagramas de Venn y relleno de regiones.
- Papeles: liso, rayado, cuadriculado, punteado, isométricos y especiales (milimetrado, plano cartesiano, trigonométrico, polar, logarítmico, Cornell), con espaciado y opacidad de las líneas ajustables.
- PDF como fondo, anotación, exportación a PDF o PNG e impresión.
- Páginas y capas con miniaturas, vista continua o página a página, dos páginas enfrentadas y búsqueda en textos y fórmulas.
- Zoom de 25 % a 2000 %, y ajuste al ancho que sigue el tamaño de la ventana.
- Lápiz y tableta en Windows: botones del lápiz asignables, con modo de mantener pulsado; sin la onda de Windows al tocar; sin el panel de escritura a mano sobre los cuadros de texto; hoja de prueba que muestra los eventos que llegan del controlador.
- Guardado: no sobrescribe en silencio el archivo de otro documento; el autoguardado escribe una copia de recuperación aparte.

## Uso

Guía de uso: [docs/USO.md](docs/USO.md). Compilación y empaquetado: [docs/COMPILAR.md](docs/COMPILAR.md).

En Windows 10/11 de 64 bits:

1. Compilar la aplicación C++ (MSYS2 UCRT64, Qt 6, CMake, Ninja), o generar el paquete portátil o el instalador con `cpp\package.bat` y `cpp\installer.bat`.
2. Abrir `xl_app.exe`, o arrastrar un `.xopp` sobre él.

## Estado y pruebas

- Versión alpha.
- Pruebas: 25 pruebas unitarias (`ctest`) y comprobaciones de interfaz sin ventana (`xl_app --selftest`) con eventos sintéticos de Qt.
- Comportamiento con lápiz: ajustado y probado con una XP-Pen Deco Pro MW (Gen 2) y lápiz X3 Pro en Windows 10. No se ha probado con otros lápices ni controladores. Las notas por área están en [`cpp/PARIDAD/`](cpp/PARIDAD) (por ejemplo [`winpen.md`](cpp/PARIDAD/winpen.md)).
- No se ha comprobado que Xournal++ abra los archivos con fórmulas ni objetos matemáticos generados por esta aplicación.
- Algunas pruebas usan documentos que no están en el repositorio.
- El instalador de Windows no está firmado; SmartScreen puede mostrar un aviso la primera vez.
- Funciones de Xournal++ sin implementar: plugins en Lua, plantillas de página, exportación a SVG, regla y escuadra dibujadas en la página, entre otras. La tabla de paridad está en [`cpp/PARIDAD/xpp_features.md`](cpp/PARIDAD/xpp_features.md).

## Relación con Xournal++

Este repositorio es un fork de [xournalpp/xournalpp](https://github.com/xournalpp/xournalpp) en GitHub. El código de la rama principal es una base de código distinta y no comparte historial con Xournal++; la rama `master` conserva el historial original.

- Xournal++ es el programa de referencia. Esta aplicación replica qué hace, dónde está y cómo se comporta, a partir del manual, del formato `.xopp` y de la observación del programa; no copia su código, según consta en [`cpp/PARIDAD/xpp_features.md`](cpp/PARIDAD/xpp_features.md). Durante el desarrollo se leyó el código fuente público de Xournal++ v1.3.8, por lo que no se descarta que alguna parte sea derivada. Por eso el proyecto usa la misma licencia que Xournal++.
- No está afiliado al equipo de Xournal++.

## Licencia

GPL-2.0-or-later, igual que Xournal++. El texto está en [LICENSE](LICENSE) (GNU GPL versión 2); se puede usar bajo esa versión o cualquier posterior.

Las bibliotecas enlazadas o incluidas conservan su licencia: Qt 6 (LGPL v3, enlazada dinámicamente), pdfium dentro de Qt6Pdf, y KaTeX y pdf.js en la versión Electron. Quien redistribuya los binarios debe cumplir con ellas.
