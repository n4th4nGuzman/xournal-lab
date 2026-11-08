# app/ extension API (for tools and panels)

Everything in `app/` is Qt Widgets, single GUI thread. The rule that keeps the page fast: **nothing on the pointer path may cost O(elements on the page)**
(see "Pointer path rules" at the end). Layering: `core/` (model, geometry, render) <- `Editor` (no widgets) <- `DocumentView` / `MainWindow` <- tools/panels.

```
Editor            owner of Document, undo journal, tool registry, command registry, style, selection hook, per-page state (current page, active layer, hidden layers)
DocumentView      the page area: layout, zoom, tiles, live layer, overlay pass. Listens to Editor signals; tools talk to it through a few calls (below)
MainWindow        menus, toolbar (built from the tool registry), status bar, side dock, file operations
Tool              interface of a tool, registered with Editor::registerTool(ToolInfo)
Selection         interface the Editor owns; NullSelection until the selection area installs the real one (Editor::setSelection)
PageIndex         per-page spatial index that survives inserts/erases (Editor::index(page))
```

Files: `tool.h` (Tool, PointerEvent, OverlayInfo, ToolInfo), `editor.h`, `selection.h`, `documentview.h`, `mainwindow.h`, `pageindex.h`, `liveink.h` (the stroke look of the prototype),
`icons.h`, `tools/*.cpp` (built-in tools), `tools/tools.h` (registration list), `selftest.cpp` (`--selftest`).

Build: `cmake -S . -B build-app -G Ninja -DCMAKE_BUILD_TYPE=Release` then `cmake --build build-app --target xl_app` (re-run the first line after adding files: the lists are globs).
Run headless checks: `QT_QPA_PLATFORM=offscreen build-app/xl_app.exe --selftest` (add a check to `behaviour()` in `selftest.cpp` for what you add; it drives the real Qt event path).

## Coordinates

* **Page units** = points (1/72 in), origin at the page's top-left corner. Elements, `PointerEvent::pos`, overlay painters and `liveDirty()` all use page units of *one page*.
* **Viewport pixels** (logical, floating): `PointerEvent::viewPos`, `DocumentView::toView/toPage/pageAt`.
* `zoom()` = CSS pixels per page unit (1.0 = one point per pixel; limits 0.25 .. 5). Convert a screen size to page units with `px / zoom`.
* Internally the view lays out and paints in *device* pixels so tiles land on whole pixels at any display scale (checked at 1.0 and 1.5); you never see that unless you read `documentview.cpp`.

## Adding a tool

1. Write `app/tools/<name>.cpp` (picked up by the glob). Subclass `Tool` and override what you need; everything has a default.
2. Declare `void register<Name>Tool(Editor&);` in `tools/tools.h`, define it in your file, call it from `registerBuiltinTools()` (one line). Registering an id that exists replaces it:
   the selection tool registers id `"select"` and thereby replaces the placeholder.

```cpp
class PlaneTool : public Tool {
 public:
  using Tool::Tool;                                         // Tool(Editor&); editor() and view() are available
  QCursor cursor() const override { return Qt::CrossCursor; }
  void pointerDown(const PointerEvent& e) override { page_ = e.page; a_ = b_ = e.pos; dragging_ = true; }
  void pointerMove(const PointerEvent& e) override {
    if (!dragging_) return;
    QRectF old = box();  b_ = e.pos;                         // old and new area: that is all that needs repainting
    view()->invalidateOverlay(page_, old.united(box()));
  }
  void pointerUp(const PointerEvent& e) override {
    dragging_ = false;
    b_ = e.pos;
    const double px = std::hypot(b_.x() - a_.x(), b_.y() - a_.y()) * e.zoom;
    if (px < 6) { /* a click without drag inserts nothing for plane/3D/line/venn */ view()->invalidateOverlay(page_); return; }
    commit();
  }
  void paintOverlay(QPainter& p, const OverlayInfo& info) override {   // page units; AA on; clipped to what is visible
    if (!dragging_ || info.page != page_) return;
    p.setPen(QPen(QColor("#39d353"), 1.0 / info.zoom, Qt::DashLine));
    p.drawRect(box());
  }
 private:
  QRectF box() const { return QRectF(a_, b_).normalized().adjusted(-3, -3, 3, 3); }
  void commit();
  int page_ = 0; QPointF a_, b_; bool dragging_ = false;
};

void registerPlaneTool(Editor& ed) {
  ToolInfo i;
  i.id = "plane"; i.label = "Plano cartesiano"; i.tooltip = "Plano cartesiano"; i.shortcut = QKeySequence("C");
  i.icon = Icons::make("plane", "C");                      // unknown names get a rounded square with the letter
  i.inserter = true;                                       // after it makes something: select it, remember the tool before (returnTo)
  i.order = 40;                                            // toolbar/menu order (built-ins use 0..12)
  i.factory = [](Editor& e) { return std::make_unique<PlaneTool>(e); };
  i.options = [](QWidget* parent, Editor& e) -> QWidget* { return new PlanePanel(parent, e); };   // optional: caret on the button opens it (built once)
  ed.registerTool(i);
}
```

What registering gives you for free: the QAction `tool.<id>` (checkable, shortcut, icon, tooltip), a toolbar button (with a caret if `options`), the entry in *Herramientas* (separators
between groups of ten `order` values), the status-bar name, and the pen-button / tablet-key mapping (a `PenAction` or app action `tool.<id>` selects it).

**Gesture rules**
* `pointerDown/Move/Up` arrive for one gesture; `e.page` is the page where it started and stays so while the pointer leaves it. Moves without contact arrive in `hover()`.
* One `PointerEvent` per Qt event; none is merged. Pen events have `hasPressure`, `tiltX/Y`, `pressedBarrel/pressedBarrel2`, `kind` (Mouse, Pen, Eraser end, Touch) and the device `timestampMs`.
* Scrolling and zooming are locked while a gesture runs (the live layer is tied to the scroll position). Tile rendering is paused too, so the pen always comes first.
* Pen buttons (`Editor::tablet()` = `TabletSettings`; barrel1 -> Toolbox, barrel2 -> Hand, eraser end -> Eraser by default) *borrow* a tool for one gesture: `activate()` and `deactivate()` are called around it, the toolbar selection does not change. Barrel1=Toolbox emits `Editor::toolboxRequested(globalPos)`; Undo/Redo actions fire on press.
* `activate()` is where a tool resets its state; `deactivate()` is where it drops previews (`view()->invalidateOverlay`) and finishes pending work (the figure tool finishes its polygon there).
* `view()` may be null in headless code; tools should still not crash (check it where it matters).
* Keys: `keyPress(QKeyEvent*)` gets keys before the window's shortcuts only if the view has focus; return true when used. Esc is handled by `Editor::keyPress` if you do not use it.

### Drawing ink while the pointer moves (the live layer)
```cpp
QPainter& p = view()->beginLive(page, /*opacity*/ 1.0);       // clears the layer; units = that page's points; AA on. Opacity composes the WHOLE layer (highlighter uses 0.5)
LiveInk ink(p, color, view()->zoom() * view()->devicePixelRatioF());
// widths: `add(pt, w)` takes the width AT that point and it is what you store as widths[i]; LiveInk then draws exactly what render_stroke.cpp draws for the saved stroke
// (same pieces through core/inkpiece.h), so nothing changes when the pen lifts. The default plain pen width is penWidth(PenLook::Proto, size, p) = size * (0.3 + 0.7 p^0.8);
// TabletSettings::penLook = PenLook::Electron brings back max(0.05, 1.5 p) * size.
view()->liveDirty(ink.add(QPointF(x, y), width));             // per event: draws one piece and repaints only its rectangle
...
ed.commitAdd(page, layer, Element(stroke));                    // the finished element: patched into the cached tiles it touches (no page re-render)
view()->endLive();                                             // after commitAdd, in the same call
```
Anything with a stroke-like preview that is too heavy for `paintOverlay` can draw on the live layer the same way. For shape tools a preview in `paintOverlay` is simpler and fine.

## Document edits and undo

**All changes to `doc()` go through `Editor`** so the spatial index, the tile cache and the history agree.

Primitives (change the document, push *no* history; the caller pushes one `Action`):
`addElement`, `insertElement`, `takeElement`, `replaceElements`, `applyReplacements` (many disjoint replacements of a layer in one pass; returns the inverse), `elementEdited(page, layer, index, rectBefore)` (after editing an element in place through `doc()`),
`touch(page, rect)` (repaint an area; a null rect also drops the index), `invalidateIndex(page)` (after you replaced whole layers by hand).

History:
```cpp
Action a;  a.label = "Mover";  a.page = page;                  // page: shown again on undo/redo if it scrolled out of view
a.undo = [&ed, ...] { /* use primitives; capture indices/elements by value, never pointers into the document */ };
a.redo = [&ed, ...] { ... };
ed.pushAction(std::move(a));                                    // the change is ALREADY done (like pushAction in the TS)
```
* `a.structural = true` for page add/remove/move/resize (the view re-lays out; use `Editor::insertPage/deletePage/movePage/setPageSize` instead of doing it yourself),
  `a.repaintAll = true` when the paper of many pages changed (`Editor::editBackgrounds(pages, label, fn)` does that for background attributes, special papers included).
* `ed.beginGroup("label") ... ed.endGroup()` makes everything pushed in between one undo step (nestable; empty groups push nothing).
* Shortcuts for the common step: `ed.commitAdd(page, layer, element, label)` (add + push). `ed.undo()/redo()` are no-ops during a gesture.
* Elements have no identity. A position `(page, layer, index)` is valid until something before it is inserted or erased: store positions only within one operation, and store *copies of elements* in undo closures.
* The active layer is `ed.activeLayer(page)`; hidden layers are `ed.layerVisible/setLayerVisible` (view state, not in the file). If the active layer is hidden, show it and `ed.notify("...")` (the pen does this).
* The layer panel exposes the model through `activeLayer/setActiveLayer/layerVisible/setLayerVisible` (they emit `layersChanged(page)`). Adding, removing, renaming or reordering layers: call the `core/docfactory.h` function on `ed.doc()` inside an `Action` of your own (undo = the inverse it reports), and after each do/undo/redo call `ed.invalidateIndex(page); ed.touch(page);` (element positions in the changed layers moved).

### Generated objects (planes, figures, number lines, venn, 3D): the `swapPlane` pattern
A generated object is an ordinary set of strokes/texts that share `xlPlane=<id>` (see `core/genobj.h`). To create or rebuild one, build the new members and swap them as ONE step:
```cpp
std::vector<Element> fresh = buildPlane(id, spec, frame, style);          // core/plane.h
Editor::Swap sw = ed.swapGenerated(page, layer, id, fresh);               // performed now; id "none" (or a new id) = nothing to replace, just insert at the top
Action a; a.label = "Plano"; a.page = page; a.undo = sw.undo; a.redo = sw.redo;
ed.pushAction(std::move(a));
// creating: ed.afterCreate("plane");   then select what was made:
ed.selection().select(page, ed.membersOf(page, layer, id));
```
`swapGenerated` removes every member of `id` in that layer and inserts `fresh` where the first old one was; undo removes the fresh ones and puts the old ones back at their old positions. Rebuilding after a resize of the selection frame is the same call with the
rebuilt members; wrap several swaps in `beginGroup/endGroup` when one gesture rebuilds more than one object.

### returnTo and afterCreate
`ToolInfo::inserter = true` marks tools that make an object. After making one call `ed.afterCreate("<tool id>")`: the editor switches to `select`, remembers the tool used *before* the inserter (the pen when that was another inserter, Select or unknown) and
`Esc` (`ed.escape()`), or a click on the empty page while `select` is current (`Selection::wouldHit` false), goes back to it, and that click's gesture is given to the returned tool. Choosing a tool from the toolbar clears it. All of this lives in `Editor`; tools only call `afterCreate`.

## Selection

`Editor::selection()` is the `Selection` interface (`selection.h`): `select(page, refs)`, `clear`, `contains`, `items`, `bbox`, `wouldHit`, `refresh`, `paintOverlay`. The selection area subclasses it and calls `ed.setSelection(std::make_unique<MySelection>(ed))`; `Editor::undo/redo` call `refresh()`;
call `ed.notifySelectionChanged()` when it changes. Its `paintOverlay` runs in the same overlay pass as the current tool's (frame, handles).

**Selection-aware operation** (move, delete, recolor, glow...):
```cpp
auto items = ed.selection().items();                       // vector<ElementRef>, same page
std::vector<Snap> before;  for (auto& r : items) before.push_back(snapshot(elementAt(r)));     // core/geometry.h
... apply (transformElement ...) then ed.elementEdited(page, r.layer, r.index, rectBefore) for each ...
Action a; a.page = page; a.undo = [...restore(...); ed.elementEdited(...)]; a.redo = [...]; ed.pushAction(a);
```
Candidates for hit testing / rectangle selection come from `ed.index(page).query(rect, pad, hits)` / `queryPoint(pos, pad, hits)` (z-ordered `(layer, index)` pairs; superset of the exact answer: finish with `hitElement`, `inRect`, `inPolygon` from `core/geometry.h`).
Deleting selected elements: `ed.applyReplacements(page, layer, repls)` with empty `with` is one pass per layer (sorted ascending by index).

## Panels, menus, toolbar

* **Side panel** (page thumbnails, layers): `win.addSidePanel("layers", "Capas", widget)`; the dock appears with the first panel and is toggled by *Ver > Barras laterales > Barra lateral*. Get the window with `static_cast<MainWindow*>(...)` from your own code, or have `main.cpp` hand it to your `install(MainWindow&)` function.
  Listen to `Editor` signals: `currentPageChanged`, `structureChanged`, `contentChanged(page, rect)`, `documentReplaced`, `layersChanged`, `selectionChanged`, `historyChanged`, `styleChanged`.
* **Tool options popup**: `ToolInfo::options` (above); it is built once, on first open, and kept; it reads/writes `ed.style()` through `ed.setStyle()` or its own settings.
* **Menu item / command**: every entry is a `QAction` found by id: `ed.action("edit.copy")`. Menus already contain disabled placeholders for what the feature areas own; implement one with
  `ed.addCommand("edit.copy", "Copiar", QKeySequence::Copy, [&]{ ... })` (sets text, shortcut, handler and enables it). Placeholder ids: `file.exportPdf edit.cut edit.copy edit.paste edit.delete edit.find edit.prefs view.presentation view.sidebarPages view.sidebarLayers nav.back nav.forward tool.toolbox pen.settings help.welcome help.pen`.
  To add a new entry: `win.menu("tools")->addAction(ed.addCommand("tool.x", "Texto", QKeySequence("X"), fn))` (keys: `file edit view nav journal tools help`; `win.addMenu(key, title, beforeKey)` for a new menu). Actions are added to the window, so their shortcuts work without a menu. Tablet key bindings use the same ids.
* **Toolbar**: tool buttons come from the registry. Other buttons: `win.toolBar()->addAction(ed.action(id))` (call it after construction; the toolbar is rebuilt when tools are registered, so add yours from a `toolRegistered` handler or rely on a registered tool).
* **Status bar**: `win.showMessage("...")` or `ed.notify("...")` from anywhere that has the editor.
* **Drawing style**: `ed.style()` (colour `#rrggbb`, pen/highlighter width, eraser radius, glow, highlighter alpha) and `ed.setStyle(s)`. Pen tip, pressure curve and pen buttons: `ed.tablet()` / `ed.setTablet()` (`core/tabletcfg.h`; load/save with `core/prefs.h`).

## Overlay

`Tool::paintOverlay(QPainter&, const OverlayInfo&)` (and `Selection::paintOverlay`) runs once per visible page that intersects the repaint, in that page's units, clipped to the visible part. Cost must be O(what you draw). Ask for repaints only for the areas that changed:
`view()->invalidateOverlay(page, pageRect)` (a null rect = the visible part of the page). Draw lines with `width / info.zoom` to keep a constant on-screen width. Remember to invalidate the *old* area when something moves.

## Pointer path rules (what the self test measures)

* Per event: O(points in the current stroke at most one new piece), never O(elements). Use `ed.index(page)` for "what is near the pointer". Do not call `elementBBox` over a layer, do not copy `Page`/`Layer`.
* Never repaint a page for one event: `liveDirty` / `invalidateOverlay` with the small rectangle only.
* A change to the document costs one `contentChanged(page, rect)`: affected tiles are marked dirty and re-rendered coalesced (8 ms) and only if visible. A new element on top of the top layer is drawn into the cached tiles directly (`elementAppended`).
* Tiles are drawn from `ed.index(page)` (only the elements that can touch the tile, `renderElementSeq` in z order, pieces of a stroke outside the tile skipped) and, when a tile has >= 200 elements (`setAsyncThreshold`), on worker threads from a COPY of those elements (a job never points into the document; `DocumentView` drops a result when a change touched its tile meanwhile, and cancels jobs for tiles that scrolled away). A change repaints only its rectangle inside the tile (`Tile::dirtyDev`); large ones go to a worker and the old picture stays up until it is ready. A stroke committed while the live layer is showing is drawn into the tiles on the next turn of the event loop (the live layer is kept until then). Anything else that wants to draw a whole page must not do it on the GUI thread for a dense page: copy what it needs, or use `renderElementSeq` with the index's candidates (the thumbnail panel does it on a worker from a copy of the page's visible layers: one job at a time, a result is dropped when the page changed meanwhile).

## Known stubs / TODO (and what they wait for)

* Tools: select/lasso (placeholder "select" registered), text, latex, shape, vspace, fill and the generated-object tools.
* Pen tip / pressure: done through `core/brush.h` (`BrushEngine`, `PressureMap`) driven by `Editor::tablet()`; the live layer of a glowing pen shows the glow the finished stroke will have: `beginLive(page, opacity, multiply, &LiveGlowParams)` runs the page renderer's own filter (`core/liveglow.h`, `LiveGlow`, sharing `glowCascade` with `renderStroke`) incrementally on the pixels the new ink can change, once per repaint; same colour, alpha, radius, falloff and composite as the saved stroke (the glow image contains the ink). A highlighter's live layer is composed with Multiply like the finished stroke (`beginLive(page, opacity, multiply)`).
* PDF/picture backgrounds: `renderTile` passes no `bgImage`; the PDF rasteriser (`pdfraster.ts`) is not ported.
* Preferences: `main.cpp` reads `%APPDATA%/xournal-electron/prefs.json` (read only) for the pen tip/buttons, glow, finger drawing and autosave; nothing is written back (the settings dialogs do that: `savePrefs`). Headless modes use defaults.
* "Save" on a freshly opened file always asks where to write (as the Electron app does, unless `Prefs::saveOverOpened`).
* Autosave writes `%APPDATA%/XournalLab/XournalLabCpp/autosave/<name>.autosave.xopp` every N minutes while dirty (off until `setAutosaveMinutes`).
* Fingers (`DocumentView::touchEvent`): one finger pans with a flick (inertia), or draws when `Editor::fingerDraw` is on; two fingers pinch-zoom around their centre; a finger that lands while the pen is near (`penNear()`: any pen event in the last 700 ms) is ignored (palm); the laser ignores fingers. Real `QTouchEvent`s are used (the viewport accepts touch), because unhandled touch arrives as emulated mouse events that this view discards.
* Toolbar: two rows, `MainWindow::toolBar()` (file, undo, tools) and `styleBar()` (colours, size presets, zoom, new page); the second goes under the first only when both do not fit on one line. Find a button of either with `MainWindow::toolbarWidget(action)`. `ToolInfo::optionsCommand` makes the caret of a tool run a command at once (the pen: `pen.settings`).

## Settings area (app/settings/)

`SettingsController::of(win)` (header `settings/settings.h`, a child of the window) owns the user's preferences (the prefs.json shared with the Electron app), the tool box, the
pen/tablet dialog, the preferences dialog, the size presets and the palettes. It reads the file once the event loop starts (only for a persisting window: `MainWindow::persistence()`)
and writes it about 400 ms after a change, merging with what is on disk and changing only the fields this area changed, so the theme the chrome area writes is never reverted.
Other areas that own a preference call `controller->mutatePrefs([](Prefs& p) { ... })` (re-reads, edits, writes; no-op when headless). `prefsApplied()` fires after any change.
Tests call `setStorage(tempPath)` first, which also allows writing to that path. It is also the only place that interprets bound keys, wheel turns and extra mouse buttons
(`Editor::tablet().keys`, through an application event filter) and that implements `pen.sizeUp/Down`, `brush.next/prev`, `color.next/prev`, `tool.previous`, `toolbox.show`.
Tool box items that stand for toolbar buttons map to command ids in `SettingsController::actionIdOf` (`bar:pgNext` -> `nav.next`, `bar:rotR` -> `selection.rotR`...): register the command
under that id and the button works (a missing command shows the button dimmed). `XL_SETTINGS_ONLY=1` with `--selftest` runs only the settings checks.

## UI parity (window chrome)

`MainWindow` is frameless: `titleBar()` holds the menu bar (`menuBar()` now returns it), title and window buttons. There is no status bar: `topBar()` (first toolbar row, built by `Chrome::buildNavBar`) ends with the document summary / messages (`showMessage`, `infoText()`, `infoWidget()`) and `zoomLabel()`. Row 2 is `toolBar()` + `styleBar()`; modules put the glow button before `styleLeadMarker()` and append selection actions. Icons: `chrome/svgicon.h`.


## Pointer filter and the settings of the original Preferencias (settings area)
`Editor::xpp()` holds `XppPrefs` (core/xpppref.h): mouse-button tools, stylus colours, pressure rules, view options... Tools and the view only READ it (they never write it: the settings area owns it, `Editor::setXpp`).
`Editor::pointerDown/Move/Up` send every event through the installed `InputFilter` (settings/inputfilter.h) before `dispatchDown/Move/Up`: pressure minimum / multiplier, pressure inference, ignoring the first N stylus events,
the tap filter and the input stabilizer live there. The filter may hold events back (the tap filter and "ignore the first events" do): `gestureActive()` is false until it lets the gesture start, so a tool never sees what was filtered.
A gesture started by a mouse button or a stylus button borrows that button's colour and size (`Editor::buttonConfigFor`) and gives them back when it ends.

## Pen buttons, hold-to-use and Windows pen niceties (app/winpen.*, settings/controller.cpp, Editor)
* **Hold-to-use.** A pen barrel button or extra mouse button bound in Preferencias > Lápiz y tableta > Atajos (`SettingsController::pressBound/releaseBound/endHold`) that runs a `tool.<id>` action selects that tool only while it is held; the release gives the tool before back (if the tool is still the one the hold set; a tool picked meanwhile stays). Non-tool actions (undo, zoom...) run once on press. A press while a stroke/gesture is on is ignored (never switches the tool under it); a release during a drag waits for `Editor::gestureFinished`. Stale state is cleared on `TabletLeaveProximity`, `WindowDeactivate` of the main window and application deactivation. Express keys (keyboard F-keys from the remote) keep their press-only behaviour.
* **Preferencias per-button tool** (`tablet().barrel/barrel2/eraser`, i.e. xpp stylus1/stylus2/eraser): `Editor::hover` borrows the mapped tool while the button is held with the pen only hovering (`Editor::cursorTool()`, signal `hoverToolChanged`), `DocumentView` refreshes hover on barrel press/release and pen leaving range, and the gesture that follows uses the same mapping. A barrel button owned by a key binding is hidden from that mapping (`Editor::setPenButtonBound`, `mappedButton`).
* **winpen** (`winpen::install()` from main, Windows only, no-op elsewhere/offscreen): app-wide filter turns off Windows feedback visuals per top-level window (`SetWindowFeedbackSetting`, run-time loaded) and answers `TabletQuerySystemGestureStatus` / sets the `MicrosoftTabletPenServiceProperty` window property; text inputs (QLineEdit, spin boxes, editable combos, QTextEdit/QPlainTextEdit) lose `WA_InputMethodEnabled` on Polish/FocusIn so the handwriting panel has no target. New text-input widgets need nothing; code that calls `setReadOnly(false)` is covered by the FocusIn pass. See `PARIDAD/winpen.md`.

## Real-pen input fixes (Editor::notePen, DocumentView, settings/controller.cpp, app/wheelguard.*)
* **Barrel button as a mouse click.** `Editor::notePen/penRecent/penAgeMs` (fed by the controller's application filter from every tablet event, hovering included; `TabletLeaveProximity` ends it) is the one record of the pen's presence; `DocumentView::penNear` (palm rejection) reads it too. A real (non-synthesized) mouse Middle/Right/Back/Forward press that arrives while `penRecent()` (1 s) is the pen's button: `DocumentView::penMouse_` merges it into the buttons of pen `PointerEvent`s (Right/Back = barrel 1, Middle/Forward = barrel 2) so `pressedBarrel/pressedBarrel2` and the Preferencias mapping apply; it never starts the mouse pan, mouse moves are ignored while it is down, and it is cleared on release, proximity leave, app deactivate and when the mouse's own buttons say it is up. The controller looks the click up as a barrel key (`barrelPress`: `penCombo`, "Botón lateral 1/2"), and when the same button also comes as a tablet event the bound action runs once (`barrelRan_`). With no pen near, mouse behaviour is unchanged. A binding on the mouse button of the same name ("Botón central") still applies.
* **Tool box and the pen.** A tablet or touch press reaches the application filter first as the event of the QWindow (not a QWidget). `ToolboxWidget::outsidePress(globalPos)` therefore decides "inside" from the global position (box frame, or an active popup that belongs to it), for mouse, tablet and touch alike.
* **Wheel over form controls.** `WheelGuard::install()` (main and the controller, once, before the controller's filter): value widgets (QComboBox, QAbstractSpinBox, QSlider, QDial) get `Qt::StrongFocus` on polish and a wheel over one without the focus is forwarded to the enclosing widgets (the form's scroll area) instead of changing it; the ACK05 wheel bindings still run first; an open combo list and the page are untouched.
* **Raw input trace.** `TabletDialog::logInput` (fed by `SettingsController::traceRaw` only while the dialog is visible): last 12 raw events (Tablet/Mouse, press/release/proximity always, moves only when the buttons changed, button and buttons in words, synthesized or not, device). Note the page ignores pointer events while a floating dialog is open (chrome filter), so events are traced where they are delivered: over the dialog's own test sheet.

* **Contact with a barrel button held (XP-Pen, Windows Ink; seen on the real pen).** Qt sends no press when the tip lands with Right/Middle already down, only TabletMove with pressure > 0 and buttons == Right/Middle (no Left). DocumentView::tabletEvent now defines contact once (Left, or raw pressure > 0 with a barrel bit) and starts the gesture on that rising edge (arrelContact_), ends it when pressure returns to 0, on release or on proximity leave; a barrel press during a Left stroke changes nothing. Selftests: 'contact:' checks (Right/Middle x mapped/bound).
