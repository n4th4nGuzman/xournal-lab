# Xournal Lab en C++ (Qt 6)

Port of the Electron app in `G:\xournal-lab\app` (read its `src\renderer\*.ts` for the exact behaviour; `G:\xournal-lab\README.md` describes every
feature). Goal: same features, but a fluid pen and a page that does not slow down with many elements. The Electron app stays untouched as the reference.

## Build and run (Windows, MSYS2 UCRT64)
```
set PATH=C:\msys64\ucrt64\bin;%PATH%
cd G:\xournal-lab\cpp
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
build\xl_app.exe            (needs C:\msys64\ucrt64\bin on the PATH for the Qt DLLs)
```
Re-run the first `cmake` line after adding files (the file lists are globs).
From Git Bash: `export PATH=/c/msys64/ucrt64/bin:$PATH`.  From PowerShell: `$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"`.
`proto/` is the stand-alone canvas prototype the user already tested and liked (its stroke rendering is the model for `app/`); it has its own CMake.

## Layout and ownership
- `core/` — no widgets (Qt Core + Gui only, for QPainter/QImage/fonts). Document model (`model.h`), `.xopp` read/write (`xopp.h`), generated-object helpers
  (`genobj.h`), plus one pair of files per ported module. Pure logic, deterministic, unit-tested.
- `tests/<name>.cpp` — one executable per file (`test_<name>`), registered in ctest. Exit code 0 = pass. Print what failed. Test the real behaviour
  (brute-force comparisons, round trips, the samples in `G:\xournal-lab\samples\*.xopp`), not mocks.
- `tools/<name>.cpp` — command-line helpers (`xl_<name>`).
- `app/` — the application (Qt Widgets). `app/resources.qrc` lists icons/resources.
- `proto/` — the prototype; do not modify.

**Each area owns its files. Changes to `core/model.*`, `core/xopp.*`, `core/genobj.*` and the CMake files are made on purpose and kept small and additive.**

## Rules
- Mirror the semantics of the TypeScript source (same defaults, same ranges, same attribute names and JSON, same roles), because documents must be
  interchangeable between both apps: `xlPlane`, `xlRole`, `xlSet`, `xlFig`, `xlMd`, `xlSpace3`, glow attrs, etc. Read the TS before porting; when you
  deviate, say why. Generated objects are ordinary strokes/texts with attributes, so the file stays valid Xournal++.
- UI text in Spanish, like the original. Code, comments and identifiers in English; comments explain why, not what; match the style of existing files.
- Performance is the reason for this port: nothing on the pointer path may be O(elements) per event; never repaint the whole page for one stroke; do
  not copy big vectors. Measure when it matters (see `proto/` and its `--bench`).
- State what is verified and how, what could not be (no tablet, no official Xournal++, no second monitor) and what is missing. A feature is not done until it has been run. Synthetic events are not a real pen.
- The C++ work does not modify `app/` (the Electron app) or the sample documents. Temp files go under the build dir or the system temp dir.
- Files are LF in `cpp/`.

## Application areas
The `core/` modules are covered by `ctest --test-dir build`. The application foundation is in `app/` (read `app/API.md` first: it explains tools, panels, commands, the overlay, undo, the live layer and the `swapGenerated` pattern). The headless checks run with
`QT_QPA_PLATFORM=offscreen build\xl_app.exe --selftest`; checks for an area go in `app/selftest.cpp` or in the area's own folder.
For ctest, QT_QPA_PLATFORM must not be set globally (text metrics differ on the offscreen platform and some tests assume the real one); the selftest and the UI checks do need `offscreen` or a real window.

Each area has a folder `app/<area>/` with an `install.cpp` (`installX(MainWindow&)`, called from `app/modules.cpp`) and its own .cpp/.h files (CMake globs `app/*.cpp` and `app/*/*.cpp`; re-run `cmake` after adding files). Shared files (`editor.*`, `mainwindow.*`, `documentview.*`, `selection.h`, `tool.h`, `selftest.cpp`, `generated.h`, `core/*`) take small additive edits only; nothing there is renamed or removed lightly.
`app/generated.h` is the contract between the selection and the generated-object kinds (planes, 3D, figures, lines, Venn): each kind implements and registers a `GeneratedHandler`; the selection consumes `GeneratedRegistry`.
A separate build dir per area works well (`cmake -S . -B build-<area> -G Ninja -DCMAKE_BUILD_TYPE=Release`; PATH needs C:\msys64\ucrt64\bin).

Behaviour reference = the Electron app: the TS in `app/src/renderer/` (app.ts has the UI glue and the e2e checks, editor.ts the gestures, index.html/styles.css the layout). UI text in Spanish, as in the Electron app (same labels, tooltips, shortcuts). The Electron UI can be captured with `cd app; node start.mjs --screenshot out.png`. Screenshots of the C++ UI (QWidget::grab, offscreen) are worth checking by eye: tidy, with targets big enough for a tablet screen.
Verification: drive the real Qt event path in `--selftest` (synthetic events) and say what cannot be verified (no pen, no official Xournal++, no second monitor).
Tests never write to `%APPDATA%` (use temp dirs): the real prefs.json is shared with the Electron app and is only written by the settings controller when a setting is changed in the app.

## PDF (pdf area)
Qt Gui cannot draw a PDF; the app uses **Qt6Pdf (pdfium)**, same Qt version, no extra build system. Install: `C:\msys64\usr\bin\bash.exe -lc "MSYSTEM=UCRT64 pacman -S --needed mingw-w64-ucrt-x86_64-qt6-pdf"`
(also pulls `qt6-declarative`, unused). The root CMake finds `Qt6::Pdf` and `Qt6::PrintSupport` as optional components: without them the build still works (PDF pages stay plain paper, formulas get the old
placeholder, `Imprimir…` says it is unavailable). Shipping next to `xl_app.exe`: `Qt6Pdf.dll` (pdfium is inside it; needs only `Qt6Network.dll` besides the Qt/MinGW DLLs already shipped) and
`Qt6PrintSupport.dll`; image plugin `imageformats/qpdf.dll` is optional (Qt can then also read a PDF as an image, but the app does not depend on it).
Layout: `core/pdfraster.*` (rasteriser: `rasterizePdfPage`, `PdfDocument`, `rasterizePdfInk` = formulas, exact true-colour alpha by white/black difference matting), `core/pdfbackground.*` (the
`<background type="pdf|pixmap">` attributes as Xournal++ writes them: first PDF page `domain="absolute" filename=... pageno=1`, later pages only `pageno=N`; lookup of a moved PDF by name; `normalizePdfReferences`),
`core/pageexport.*` (vector PDF with QPdfWriter, PNG, page ranges, shared page painting for print), `app/pdf/` (async background cache `PdfBackgrounds`, commands, dialogs). The canvas gets PDF pages through
`setBackgroundImageProvider` in `core/pagerender.h` (non-blocking, any thread, rasterised on a worker at the zoom's size, bounded LRU), formulas through `setPdfImageDecoder`.
Limits: PDF backgrounds are exported as images (150 dpi default), not as vector pages; very deep zoom of a big PDF page is capped at 24 Mpx per page image.

## Delivery, interchange and robustness (qa area)
- **Package**: `package.bat` (or `package.ps1 [-Build dir] [-NoBuild] [-Zip]`) rebuilds `xl_app`, assembles `dist\XournalLab\` (windeployqt6 + the transitive closure of the MSYS2 DLLs + only the plugins the app loads), checks that every import resolves inside the folder or to Windows, and runs the result once with a PATH that excludes MSYS2. `dist\README.txt` is the user note. `run_app.bat` still runs the build folder with the MSYS2 DLLs.
- **Smoke**: `smoke.ps1` = build (own folder `build-smoke`), ctest, offscreen selftest (reads its log), optional `-Interchange`; one screen of OK/FAIL.
- **Interchange with the Electron app**: `tools/interchange/run_interchange.ps1` (documents both ways through the Electron app's own `parseXopp`/`serializeXopp` running in a hidden Electron window, compared as canonical JSON by `compare.mjs`; `xl_dump`, `xl_qa_gen`) and `prefs_check.mjs` (`xl_prefsio`, prefs.json both ways on temp copies). Read-only on the Electron app and on the user's prefs.
- **Unknown XML is kept**: `Layer/Page/Document::extra` hold the elements this version does not interpret (`<timestamp>`, `<audio>`, anything a newer Xournal++ adds, with nested content, prefixes and namespace declarations) as `RawNode` trees anchored by "how many known siblings came before"; the writer puts them back at the same place. `LoadResult::skipped` is now always empty; `preserved` lists what was kept. Numbers must be finite (`nan`/`inf`/`1e999` are refused with a message); gzip output is capped (1.5 GB); `Page::width()/height()` are bounded to 1..`kMaxPageSize` points (the attribute in the file is untouched). All load errors are in Spanish.
- **Robustness tests**: `tests/robust_load.cpp` (corrupt/truncated gzip and XML, unknown elements, numbers, 100k strokes, 500 pages, 50 MB image) and `tests/robust_render.cpp` (hostile but loadable documents through repair, geometry, spatial index and renderer). `tools/robust/run_app_fuzz.ps1` opens 16 hostile files in the real app (offscreen). `xl_app --reopen N file...` prints working set per round (leak check). `app/crashlog.*` writes the fault address and a module-relative stack to stderr and `%TEMP%\xournal-lab-crash.log` on a fatal exception (resolve with `addr2line -e xl_app.exe 0x140000000+offset` in a `-g` build).
- **Memory bounds** (all LRU): tiles 320 MB (`TileCache`), PDF backgrounds 320 MB (`pdfbg`), glow bitmaps 96 MB, decoded images 64 MB (one oversized image is still kept), any temporary layer/PDF page image <= 24 Mpx, PDF/picture background for export or print <= 36 Mpx (~600 dpi on A4).
