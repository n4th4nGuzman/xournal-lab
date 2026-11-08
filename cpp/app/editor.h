// The owner of the document and of everything that edits it: history, current tool, drawing style, selection, page/layer state, the tool registry and
// the command registry. It has no widgets of its own (the DocumentView and the MainWindow listen to its signals), so it can be driven headless.
// Every change to the document goes through here, so the spatial indexes, the tile cache and the undo stack never disagree. See API.md.
#pragma once
#include "model.h"
#include "pageindex.h"
#include "selection.h"
#include "tabletcfg.h"
#include "tool.h"
#include "xpppref.h"
#include <QAction>
#include <QColor>
#include <QElapsedTimer>
#include <QEvent>
#include <QObject>
#include <QRectF>
#include <functional>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

/**
 * One undo step, already performed when it is pushed (like pushAction in the TS). undo/redo must change the document only through Editor's element
 * primitives (which keep index and tiles current) or call touch() themselves.
 */
struct Action {
  QString label;
  int page = -1;               // the page it concerns (the view goes there on undo/redo when it is not visible); -1 = none
  bool structural = false;     // pages were added, removed, moved or resized: the layout is rebuilt
  bool repaintAll = false;     // the look of many pages changed (paper): everything repaints
  std::function<void()> undo, redo;
};

/** What the next stroke/shape is drawn with. Tools read it when a gesture starts; panels change it and emit styleChanged via Editor::setStyle. */
struct Style {
  QString color = "#000000";   // "#rrggbb"
  double penWidth = 1.41;
  double highlighterWidth = 2.83;
  double eraserRadius = 8;     // page units
  double textSize = 12;        // font size of the next text, page units (the text tool's size presets)
  bool glowOn = false;
  double glowSize = 8, glowIntensity = 0.8;
  QString highlighterAlpha = "7f";   // two hex digits, as the samples have it
  /** Line style of the next pen stroke / shape, as Xournal++ writes it in the stroke's `style` attribute: "plain" (no attribute), "dash", "dashdot", "dot". */
  QString lineStyle = "plain";
  /** Fill of the next pen / highlighter stroke or shape: the `fill` attribute (alpha 0..255 of the polygon the stroke outlines); -1 = none (no attribute). */
  int fillAlpha = -1;
  bool operator==(const Style&) const = default;
};

/** Writes the line style and fill of the drawing style into a stroke's attributes the way Xournal++ does (nothing for "plain" / no fill, so a default file is unchanged). */
inline void applyLineStyle(Attrs& attrs, const Style& st, bool allowStyle = true) {
  if (allowStyle && st.lineStyle != QLatin1String("plain") && !st.lineStyle.isEmpty()) attrSet(attrs, "style", st.lineStyle);
  if (st.fillAlpha >= 0) attrSet(attrs, "fill", QString::number(std::min(st.fillAlpha, 255)));
}

class InputFilter;   // settings/inputfilter.h: stabilizer, pressure rules, tap filter (preferences "Sistema de entrada", "Lápiz", "Área de dibujo")

class Editor : public QObject {
  Q_OBJECT
 public:
  explicit Editor(QObject* parent = nullptr);
  ~Editor() override;

  // ---- document ------------------------------------------------------------------------------------------------------------------------
  Document& doc() { return doc_; }
  const Document& doc() const { return doc_; }
  /** Replace everything (new, open). Clears history, selection and caches. `path` is where Save writes (empty = ask). */
  void setDocument(Document d, const QString& path = QString());
  const QString& path() const { return path_; }
  void setPath(const QString& p) { path_ = p; }
  bool dirty() const { return version_ != savedVersion_; }
  void markSaved();
  /** Counts every change, undo and redo and never goes back: tells whether anything happened since a given moment (autosave). */
  int changeCount() const { return mutations_; }

  DocumentView* view() const { return view_; }
  void setView(DocumentView* v) { view_ = v; }

  // ---- pages and layers (state the file does not keep: current page, active layer, hidden layers) -----------------------------------------
  int pageCount() const { return int(doc_.pages.size()); }
  int currentPage() const { return current_; }
  void setCurrentPage(int p);
  int activeLayer(int page) const;
  void setActiveLayer(int page, int layer);
  bool layerVisible(int page, int layer) const;
  void setLayerVisible(int page, int layer, bool visible);
  std::vector<int> hiddenLayers(int page) const;
  /** Structural edits, each one undo step. They keep indices consistent for the per-page state. */
  int insertPage(int index, int likeIndex = -1);   // new empty page shaped like `likeIndex` (default: its neighbour); returns its index
  bool deletePage(int index);                      // the last page cannot be deleted (notice + false)
  bool movePage(int index, int delta);
  /** Paper look. `style` is a built-in style id ("plain"...). One undo step, every page when `all`. */
  void setPaperStyle(int page, const QString& style, bool all);
  void setPaperColor(int page, const QString& rgb, bool all);   // "#rrggbb"
  void setPageSize(int page, double w, double h, bool all);
  /** Generic undoable edit of the <background> attributes of some pages (special papers use it too): `fn` changes a copy, the whole attribute list is restored on undo. */
  void editBackgrounds(const std::vector<int>& pages, const QString& label, const std::function<void(Attrs&)>& fn);

  // ---- element primitives: change the document, keep index + tiles current, push NO history (the caller pushes one Action) --------------------
  int addElement(int page, int layer, Element e);                      // append on top of the layer; returns its position
  void insertElement(int page, int layer, int index, Element e);
  Element takeElement(int page, int layer, int index);
  /** Replace `count` elements from `index` by `with` (any number, also none). The eraser's cut and generated-object rebuilds use it. */
  void replaceElements(int page, int layer, int index, int count, std::vector<Element> with);
  /** Several disjoint replacements of one layer in a single pass (one O(layer) move instead of one per replacement): the eraser applies all the cuts of an event this way. */
  struct Repl { int index = 0; int count = 1; std::vector<Element> with; };   // positions are those before any of them is applied
  /** `repls` must be sorted by index and not overlap. Returns the inverse (positions as they are after), to undo by applying it. */
  std::vector<Repl> applyReplacements(int page, int layer, std::vector<Repl> repls);
  /** After editing the element at (layer, index) in place through doc(): refreshes index and repaints the old and the new area. `before` = area it used to cover. */
  void elementEdited(int page, int layer, int index, const QRectF& before = QRectF());
  /** Repaint a page area (page units; null = the whole page). Also drops the spatial index when `whole` content changed in ways the primitives did not see. */
  void touch(int page, const QRectF& rect = QRectF());
  void invalidateIndex(int page);                                      // use after replacing a layer or the whole page content directly
  /** Spatial index of the page, rebuilt when needed. Candidates only: run the exact test on them (core/geometry.h). */
  const PageIndex& index(int page);
  /** Convenience for a whole undo step that adds one element: add + pushAction + (the view patches its tiles). Returns the position. */
  int commitAdd(int page, int layer, Element e, const QString& label = QStringLiteral("Trazo"));

  /** Replace every piece of generated object `id` in the layer by `fresh`, at the first old piece's position (the swapPlane of the TS). Performed now; returns how to undo/redo it. */
  /** Positions of the pieces of generated object `id` in a layer (what Selection::select wants after a rebuild). O(layer); not for the pointer path. */
  std::vector<ElementRef> membersOf(int page, int layer, const QString& id) const;
  struct Swap { std::function<void()> undo, redo; };
  Swap swapGenerated(int page, int layer, const QString& id, std::vector<Element> fresh);

  // ---- history -------------------------------------------------------------------------------------------------------------------------
  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  int undoDepth() const { return int(undo_.size()); }
  QString undoLabel() const { return undo_.empty() ? QString() : undo_.back().label; }
  QString redoLabel() const { return redo_.empty() ? QString() : redo_.back().label; }
  void pushAction(Action a);
  /** Everything pushed between begin and end becomes one undo step (nestable). An empty group pushes nothing. */
  void beginGroup(const QString& label = QString());
  void endGroup();
  void undo();
  void redo();

  // ---- tools ---------------------------------------------------------------------------------------------------------------------------
  /** Registering an id again replaces the earlier registration (the selection area replaces the built-in placeholder "select"). */
  void registerTool(ToolInfo info);
  void registerTool(const QString& id, ToolFactory f) { ToolInfo i; i.id = id; i.factory = std::move(f); i.inToolbar = false; registerTool(std::move(i)); }
  bool hasTool(const QString& id) const { return tools_.count(id) != 0; }
  const ToolInfo* toolInfo(const QString& id) const;
  std::vector<const ToolInfo*> toolInfos() const;                      // by `order`
  const QString& tool() const { return tool_; }
  Tool* currentTool() { return toolInstance(tool_); }
  Tool* toolInstance(const QString& id);                                // creates it on first use
  void setTool(const QString& id);
  const QString& previousTool() const { return toolBefore_; }
  /** The tool to return to when what was just made is let go (Esc, or a click on the empty page); empty when Select was chosen on purpose. */
  const QString& returnTo() const { return returnTo_; }
  /** An inserting tool made something: select it, remember the tool used before the inserter (the pen when that was another inserter or unknown). */
  void afterCreate(const QString& fromTool);
  void escape();

  // ---- drawing style and tablet ---------------------------------------------------------------------------------------------------------
  const Style& style() const { return style_; }
  void setStyle(const Style& s);
  const TabletSettings& tablet() const { return tablet_; }
  void setTablet(const TabletSettings& t);
  bool fingerDraw() const { return fingerDraw_; }
  void setFingerDraw(bool on) { fingerDraw_ = on; }
  /** The settings the original Xournal++ preferences have (mouse buttons, stylus colours, pressure rules...); the settings area sets them, tools and the view read them. */
  const XppPrefs& xpp() const { return xpp_; }
  void setXpp(const XppPrefs& x) { if (x == xpp_) return; xpp_ = x; emit xppChanged(); }
  /** Size preset `index` (0..4) of a tool kind for the per-button sizes; installed by the settings area (0 = no such preset). */
  void setSizeLookup(std::function<double(const QString& toolId, int index)> f) { sizeLookup_ = std::move(f); }
  /** Pointer events pass through this before they reach the tools (null = straight through). The Editor does not own the rules, only the hook. */
  void setInputFilter(std::shared_ptr<InputFilter> f) { filter_ = std::move(f); }
  /** What a gesture that starts with this event does besides the tool: the colour / size its button asks for (empty = nothing). */
  const ButtonCfg* buttonConfigFor(const PointerEvent& e) const;
  /** Shape recognition (the "Formas automáticas" switch): the pen replaces a hand-drawn line, ellipse, rectangle or triangle by the clean shape. The pen tool applies it. */
  bool recognizeShapes() const { return recognize_; }
  void setRecognizeShapes(bool on) { if (on != recognize_) { recognize_ = on; emit recognizeChanged(on); } }

  // ---- selection (hooks; the implementation is in selection/) -----------------------------------------------------------------------------------
  Selection& selection() { return *selection_; }
  void setSelection(std::unique_ptr<Selection> s);
  /** The selection implementation calls this when its content changed, so the toolbar/status/overlay refresh. */
  void notifySelectionChanged() { emit selectionChanged(); }
  /** Work an area keeps outside the document until it is confirmed (a text being typed): registered by that area, run before save / export / print. */
  void addFlusher(std::function<void()> f) { flushers_.push_back(std::move(f)); }
  void flushPending() { for (auto& f : flushers_) f(); }

  // ---- commands: every menu entry and shortcut is a QAction looked up by id -------------------------------------------------------------
  /** The action with this id, created (disabled, no handler) on first request so menus can reference it before anyone implements it. */
  QAction* action(const QString& id);
  /** Implement (or add) a command: sets text/shortcut and the handler, and enables it. */
  QAction* addCommand(const QString& id, const QString& text, const QKeySequence& shortcut, std::function<void()> run);
  const std::map<QString, QAction*>& actions() const { return actions_; }

  // ---- input, called by the DocumentView ----------------------------------------------------------------------------------------------
  void pointerDown(const PointerEvent& e);
  void pointerMove(const PointerEvent& e);
  void pointerUp(const PointerEvent& e);
  void pointerCancel();
  void hover(const PointerEvent& e);
  void pointerLeft();
  bool keyPress(QKeyEvent* e);
  bool keyRelease(QKeyEvent* e);
  bool gestureActive() const { return gesture_ != nullptr; }
  Tool* gestureTool() const { return gesture_; }
  /** The tool id a gesture that starts with this event uses (the current tool, or the one a pen button / eraser end maps to). Empty = no gesture (button maps to undo...). */
  QString toolForGesture(const PointerEvent& e) const;
  /** The tool the page cursor and the overlay follow: the gesture's, else the one a pen button held right now (with the pen only hovering) borrows, else the current tool. */
  Tool* cursorTool() { return gesture_ ? gesture_ : hoverTool_ ? hoverTool_ : currentTool(); }
  /** The settings area tells which pen buttons a key binding owns ("Botón lateral 1/2" in Atajos): for those the per-button tool mapping of Preferencias stays out of the way,
   *  because the binding decides what the button does (and, for a tool, holds it only while pressed). */
  void setPenButtonBound(std::function<bool(PenButton)> f) { penBound_ = std::move(f); }

  /** The pen's presence, fed by the settings area from every raw tablet event of the application (hovering included). A driver may report a barrel button as a real
   *  mouse click on top of the tablet events: a mouse press that arrives while the pen is `penRecent()` is that button, not a mouse. */
  void notePen(QEvent::Type t);
  /** Milliseconds since the last tablet event (a very large number when none was seen), proximity leave included: the resting hand after a lifted pen counts as near. */
  double penAgeMs() const { return penSeen_ ? double(penClock_.nsecsElapsed()) / 1e6 : 1e12; }
  /** A tablet event was seen within kPenRecentMs and the pen has not left the tablet's range since. */
  bool penRecent() const { return !penGone_ && penAgeMs() < kPenRecentMs; }
  void forgetPen() { penSeen_ = false; penGone_ = false; }
  static constexpr int kPenRecentMs = 1000;

  /** Messages for the status bar ("La capa activa está oculta"). */
  void notify(const QString& msg) { emit notice(msg); }

 signals:
  void contentChanged(int page, const QRectF& rect);                   // rect null = whole page
  void elementAppended(int page, int layer, int index, const QRectF& rect);
  void structureChanged(int focusPage);
  void appearanceChanged();                                            // paper/colour of many pages
  void documentReplaced();
  void currentPageChanged(int page);
  void toolChanged(const QString& id);
  void toolRegistered(const QString& id);
  void styleChanged();
  void historyChanged();
  void dirtyChanged(bool dirty);
  void selectionChanged();
  void notice(const QString& msg);
  void toolboxRequested(const QPoint& globalPos);
  void layersChanged(int page);
  void focusRequested(int page);                                       // undo/redo touched a page: show it
  void actionCreated(QAction* a);
  void recognizeChanged(bool on);
  void xppChanged();
  void hoverToolChanged();                                             // a pen button held while hovering borrowed a tool (or gave it back): the cursor follows
  void gestureFinished();                                              // a gesture ended (up or cancel): the gesture slot is free again

 private:
  bool runPressAction(const PointerEvent& e);                          // undo / redo / tool box of a pen button: once per press, true when the mapping is one of them
  bool barrelFired_ = false;
  PenButton mappedButton(const PointerEvent& e) const;                // the pen button of this event whose Preferencias mapping applies (None when a key binding owns it)
  void setHoverTool(Tool* t);
  Tool* hoverTool_ = nullptr;                                          // activated on loan while a button maps to another tool and no gesture is on
  std::function<bool(PenButton)> penBound_;
  QElapsedTimer penClock_;
  bool penSeen_ = false, penGone_ = false;
  std::vector<std::function<void()>> flushers_;
  struct PageState { int activeLayer = 0; std::vector<int> hidden; };
  struct ToolEntry { ToolInfo info; std::unique_ptr<Tool> instance; };
  void syncPageState();
  PageIndex& indexRef(int page);
  void structural(int focus, const QString& label, std::function<void()> doIt, std::function<void()> undoIt);
  void afterHistory(const Action& a);
  void bumpVersion(int delta);
  static QRectF paddedRect(const Element& e);

  Document doc_;
  QString path_;
  DocumentView* view_ = nullptr;
  int current_ = 0;
  std::vector<PageState> pageState_;
  std::vector<PageIndex> indexes_;
  std::vector<Action> undo_, redo_;
  struct Group { QString label; std::vector<Action> items; };
  std::vector<Group> groups_;
  int version_ = 0, savedVersion_ = 0, mutations_ = 0;

  std::unordered_map<QString, ToolEntry> tools_;
  QString tool_ = "pen", toolBefore_, returnTo_;
  Tool* gesture_ = nullptr;
  QString gestureId_;
  bool gestureBorrowed_ = false;
  Style style_;
  TabletSettings tablet_;
  bool fingerDraw_ = false;
  bool recognize_ = false;
  XppPrefs xpp_;
  std::function<double(const QString&, int)> sizeLookup_;
  std::shared_ptr<InputFilter> filter_;
  Style gestureStyle_;                 // the style to put back when a gesture that borrowed a colour or size ends
  bool gestureStyleSaved_ = false;
  void dispatchDown(const PointerEvent& e);
  void dispatchMove(const PointerEvent& e);
  void dispatchUp(const PointerEvent& e);
  void restoreGestureStyle();
  std::unique_ptr<Selection> selection_;
  std::map<QString, QAction*> actions_;
};
