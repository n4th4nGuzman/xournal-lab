// The second half of the preferences dialog: Ver, Escala, Área de dibujo, Valores predeterminados, Grabación de audio, LaTeX, Idioma, and what Aceptar does.
#include "chrome/svgicon.h"
#include "palettes.h"
#include "prefsdialog.h"
#include "settings.h"
#include "xppapply.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QGroupBox>

#define U(s) QString::fromUtf8(s)

static void fitCombo(QComboBox* c) {
  c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  c->setMinimumContentsLength(12);
  c->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

// ---- 6. Ver --------------------------------------------------------------------------------------------------------------------------------------
void PrefsDialog::buildView() {
  newPage(pageTitles()[5]);
  {
    auto* l = group(U("Ventana"));
    check(l, "view.menubarAtStart", U("Mostrar la barra de menús al iniciar"), &XppPrefs::menubarAtStart);
    note(l, U("Se muestra u oculta en cualquier momento con F10."));
    check(l, "view.titlePath", U("Mostrar la ruta del archivo en la barra de título"), &XppPrefs::titlePath);
    check(l, "view.titlePage", U("Mostrar el número de página en la barra de título"), &XppPrefs::titlePageNumber);
  }
  {
    auto* l = group(U("Apariencia"));
    theme = new QComboBox(this);
    theme->addItem(U("Clásico (oscuro)"), "classic");
    theme->addItem(U("Neon (synthwave, con brillo)"), "neon");
    fitCombo(theme);
    row(l, U("Tema"), {theme});
    reg("view.theme", theme);
    effects = new QComboBox(this);
    effects->addItem(U("Automático (reducidos si el sistema pide menos movimiento o transparencias)"), "auto");
    effects->addItem(U("Siempre completos"), "full");
    effects->addItem(U("Reducidos (menos uso de GPU)"), "reduced");
    fitCombo(effects);
    row(l, U("Efectos (brillos y sombras difuminadas)"), {effects});
    reg("view.effects", effects);
    auto* icon = new QComboBox;
    icon->addItem(U("Color (los iconos de la app)"));
    reg("view.iconTheme", icon);
    fitCombo(icon);
    row(l, U("Tema de iconos"), {icon});
    auto* variant = new QComboBox;
    variant->addItem(U("Según el tema elegido arriba"));
    reg("view.themeVariant", variant);
    fitCombo(variant);
    row(l, U("Variante del tema"), {variant});
    unsupported(l, {icon, variant}, U("la app trae un único juego de iconos y dos temas (el de arriba cumple la función de la variante clara/oscura); no usa temas del sistema."));
  }
  {
    auto* l = group(U("Colores"));
    colorRow(l, "view.colorBorder", U("Borde de la página actual"), &XppPrefs::colorBorder, false, true, QColor(90, 90, 90))->setUnsetText(U("sin borde"));
    colorRow(l, "view.colorBackground", U("Fondo entre páginas"), &XppPrefs::colorBackground, false, true, QColor(0x3b, 0x3b, 0x3b));
    colorRow(l, "view.colorSelection", U("Selección (texto, trazos…)"), &XppPrefs::colorSelection, false, true, QColor(0x2f, 0x6f, 0xd0));
    colorRow(l, "view.colorActive", U("Selección activa (resultados de búsqueda…)"), &XppPrefs::colorActiveSelection, false, true, QColor(255, 90, 0));
    check(l, "view.pageShadow", U("Mostrar sombra detrás de las páginas"), &XppPrefs::pageShadow);
    auto* recolor = addCheck(l, U("Recolorear el área de dibujo y las vistas previas de la barra lateral (colores oscuro y claro)"));
    reg("view.recolor", recolor);
    unsupported(l, {recolor}, U("la app no recolorea las páginas; el tema cambia los marcos, no el papel."));
  }
  {
    auto* l = group(U("Cursor"));
    combo(l, "view.cursorIcon", U("Icono del cursor para el lápiz"), &XppPrefs::cursorIcon, {U("Círculo del tamaño del trazo (el de la app)"), U("Sin icono"), U("Punto pequeño"), U("Lápiz grande"), U("Cursor del sistema"), U("Cruz")});
    auto* on = check(l, "view.highlight", U("Resaltar la posición del cursor"), &XppPrefs::highlightCursor);
    auto* r = dspin(l, "view.highlightRadius", U("Radio"), &XppPrefs::highlightRadius, 0, 30, 1, 0, U(" píxeles"));
    auto* c = colorRow(l, "view.highlightColor", U("Color del círculo"), &XppPrefs::highlightColor, true, false, QColor(255, 255, 0, 128));
    auto* bw = dspin(l, "view.highlightBorderWidth", U("Ancho del borde"), &XppPrefs::highlightBorderWidth, 0, 30, 1, 0, U(" píxeles"));
    auto* bc = colorRow(l, "view.highlightBorderColor", U("Color del borde"), &XppPrefs::highlightBorderColor, true, false, QColor(0, 0, 255, 128));
    auto sync = [=](bool b) { for (QWidget* w : {static_cast<QWidget*>(r), static_cast<QWidget*>(c), static_cast<QWidget*>(bw), static_cast<QWidget*>(bc)}) w->setEnabled(b); };
    connect(on, &QCheckBox::toggled, this, sync);
    sync(false);
  }
  {
    auto* l = group(U("Zurdos / diestros"));
    check(l, "view.sidebarRight", U("Mostrar la barra lateral en el lado derecho"), &XppPrefs::sidebarRight);
    check(l, "view.scrollbarLeft", U("Mostrar la barra de desplazamiento vertical en el lado izquierdo"), &XppPrefs::scrollbarLeft);
  }
  {
    auto* l = group(U("Barra lateral"));
    combo(l, "view.sidebarNumbering", U("Numeración de páginas"), &XppPrefs::sidebarNumbering, {U("Ninguna"), U("Debajo de la vista previa"), U("Fondo circular"), U("Fondo cuadrado")});
  }
  {
    auto* l = group(U("Barras de desplazamiento"));
    check(l, "view.hideH", U("Ocultar la barra de desplazamiento horizontal"), &XppPrefs::hideHScroll);
    check(l, "view.hideV", U("Ocultar la barra de desplazamiento vertical"), &XppPrefs::hideVScroll);
    auto* fade = addCheck(l, U("Desactivar que las barras se desvanezcan"));
    reg("view.noFade", fade);
    unsupported(l, {fade}, U("las barras de Qt en Windows no se desvanecen: siempre se ven mientras hacen falta."));
  }
  {
    auto* l = group(U("Desplazamiento al arrastrar una selección contra el borde"));
    auto* speed = new QDoubleSpinBox;
    speed->setValue(20);
    auto* mult = new QDoubleSpinBox;
    mult->setValue(5);
    row(l, U("Velocidad base"), {speed});
    row(l, U("Multiplicador máximo"), {mult});
    reg("view.edgePanSpeed", speed);
    reg("view.edgePanMax", mult);
    unsupported(l, {speed, mult}, U("la selección no desplaza la página sola al arrastrarla contra el borde; hay que usar la rueda, la mano o el dedo."));
  }
  {
    auto* l = group(U("Pantalla completa"));
    check(l, "view.fsMenubar", U("Mostrar la barra de menús"), &XppPrefs::fsMenubar);
    check(l, "view.fsToolbar", U("Mostrar las barras de herramientas"), &XppPrefs::fsToolbar);
    check(l, "view.fsSidebar", U("Mostrar la barra lateral"), &XppPrefs::fsSidebar);
  }
  {
    auto* l = group(U("Modo presentación"));
    check(l, "view.prMenubar", U("Mostrar la barra de menús"), &XppPrefs::prMenubar);
    check(l, "view.prToolbar", U("Mostrar las barras de herramientas"), &XppPrefs::prToolbar);
    check(l, "view.prSidebar", U("Mostrar la barra lateral"), &XppPrefs::prSidebar);
    check(l, "view.prFullscreen", U("Ir a pantalla completa"), &XppPrefs::prFullscreen);
  }
  {
    auto* l = group(U("Rendimiento"));
    auto* before = new QSpinBox;
    before->setValue(3);
    auto* after = new QSpinBox;
    after->setValue(5);
    auto* eager = addCheck(l, U("Limpiar las páginas en caché al desplazarse"));
    eager->setChecked(true);
    row(l, U("Precargar páginas antes"), {before});
    row(l, U("Precargar páginas después"), {after});
    reg("view.preloadBefore", before);
    reg("view.preloadAfter", after);
    reg("view.eager", eager);
    unsupported(l, {before, after, eager}, U("la app dibuja por teselas con su propia caché acotada (los trabajos se reparten entre varios hilos y se descartan los de teselas lejanas); no tiene un número de páginas por precargar."));
  }
  endPage();
}

// ---- 7. Escala ------------------------------------------------------------------------------------------------------------------------------------
void PrefsDialog::buildZoom() {
  newPage(pageTitles()[6]);
  {
    auto* l = group(U("Velocidad del zoom"));
    dspin(l, "zoom.step", U("Velocidad de un paso de zoom"), &XppPrefs::zoomStep, 0.1, 50, 1, 2, U(" %"));
    dspin(l, "zoom.scroll", U("Velocidad con Ctrl + rueda (por muesca)"), &XppPrefs::zoomStepScroll, 0.1, 50, 1, 2, U(" %"));
    note(l, U("Los valores por omisión de esta app (20 % por paso y 19,72 % por muesca) son los que ya tenía; el original usa 10 % y 2 %."));
  }
  {
    auto* l = group(U("Zoom predeterminado"));
    check(l, "zoom.fitOnLoad", U("Forzar el zoom al ancho al cargar un documento"), &XppPrefs::forceFitOnLoad);
  }
  {
    auto* l = group(U("Calibración de los DPI de la pantalla"));
    note(l, U("Para que al 100 % el tamaño de los elementos sea el natural."));
    auto* automatic = addCheck(l, U("Automático"));
    automatic->setChecked(true);
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(50, 400);
    slider->setValue(72);
    reg("zoom.dpiAuto", automatic);
    reg("zoom.dpi", slider);
    l->addWidget(slider);
    unsupported(l, {automatic, slider}, U("en esta app el 100 % es un píxel por punto (la página A4 mide 595 px); no hay calibración con regla. El zoom «100 %» y «Ajustar al ancho» siguen funcionando."));
  }
  endPage();
}

// ---- 8. Área de dibujo --------------------------------------------------------------------------------------------------------------------------
void PrefsDialog::buildDrawing() {
  newPage(pageTitles()[7]);
  {
    auto* l = group(U("Desplazamiento fuera de la página"));
    note(l, U("Si añades espacio junto a las páginas puedes elegir la zona de la pantalla en la que quieres trabajar. Las cantidades son píxeles."));
    auto* h = check(l, "draw.addH", U("Añadir espacio horizontal adicional"), &XppPrefs::addHSpace);
    auto* hl = ispin(l, "draw.hLeft", U("A la izquierda"), &XppPrefs::hSpaceLeft, 0, 2000, U(" px"));
    auto* hr = ispin(l, "draw.hRight", U("A la derecha"), &XppPrefs::hSpaceRight, 0, 2000, U(" px"));
    auto* v = check(l, "draw.addV", U("Añadir espacio vertical adicional"), &XppPrefs::addVSpace);
    auto* va = ispin(l, "draw.vAbove", U("Por encima"), &XppPrefs::vSpaceAbove, 0, 2000, U(" px"));
    auto* vb = ispin(l, "draw.vBelow", U("Por debajo"), &XppPrefs::vSpaceBelow, 0, 2000, U(" px"));
    auto* u = check(l, "draw.unlimited", U("Desplazamiento ilimitado (una pantalla entera de espacio por cada lado)"), &XppPrefs::unlimitedScroll);
    auto sync = [=] {
      const bool un = u->isChecked();
      h->setEnabled(!un); v->setEnabled(!un);
      hl->setEnabled(!un && h->isChecked()); hr->setEnabled(!un && h->isChecked());
      va->setEnabled(!un && v->isChecked()); vb->setEnabled(!un && v->isChecked());
    };
    for (QCheckBox* cb : {h, v, u}) connect(cb, &QCheckBox::toggled, this, sync);
    binders_.push_back({[sync](const XppPrefs&, const TabletSettings&) { sync(); }, [](XppPrefs&, TabletSettings&) {}});
  }
  {
    auto* l = group(U("Añadir automáticamente una última página vacía (excepto en PDF)"));
    auto* g = new QButtonGroup(this);
    QRadioButton* r[3];
    const char* labels[3] = {"desactivado", "al desplazarse al final de la última página", "al dibujar en la última página"};
    for (int i = 0; i < 3; i++) { r[i] = new QRadioButton(U(labels[i])); g->addButton(r[i], i); l->addWidget(r[i]); reg(QString("draw.lastPage%1").arg(i), r[i]); }
    binders_.push_back({[r](const XppPrefs& x, const TabletSettings&) { r[std::clamp(x.lastPageAppend, 0, 2)]->setChecked(true); },
                        [r](XppPrefs& x, TabletSettings&) { x.lastPageAppend = r[1]->isChecked() ? 1 : r[2]->isChecked() ? 2 : 0; }});
  }
  {
    auto* l = group(U("Acción al tocar con la herramienta"));
    note(l, U("Un trazo demasiado corto y rápido para ser dibujo (un toque) se descarta y se hace esta acción."));
    auto* on = check(l, "draw.tapEnabled", U("Activar la acción al toque"), &XppPrefs::tapEnabled);
    auto* t = ispin(l, "draw.tapTime", U("Límite de tiempo"), &XppPrefs::tapTime, 0, 1000, U(" ms"));
    auto* len = dspin(l, "draw.tapLength", U("Límite de longitud"), &XppPrefs::tapLength, 0.01, 100, 0.1, 2, U(" mm"));
    auto* freq = ispin(l, "draw.tapSuccessive", U("Límite de frecuencia"), &XppPrefs::tapSuccessive, 0, 1000, U(" ms"));
    auto* box = check(l, "draw.tapToolbox", U("Mostrar el cuadro de herramientas flotante"), &XppPrefs::tapToolbox);
    auto* sel = addCheck(l, U("Intentar seleccionar primero un objeto"));
    reg("draw.tapSelect", sel);
    unsupported(l, {sel}, U("el tablero de selección no ofrece buscar el objeto bajo un punto; la acción al toque abre el cuadro de herramientas."));
    auto sync = [=](bool b) { for (QWidget* w : {static_cast<QWidget*>(t), static_cast<QWidget*>(len), static_cast<QWidget*>(freq), static_cast<QWidget*>(box)}) w->setEnabled(b); };
    connect(on, &QCheckBox::toggled, this, sync);
    sync(false);
  }
  {
    auto* l = group(U("Páginas emparejadas"));
    auto* off = new QSpinBox;
    off->setValue(1);
    row(l, U("Desplazamiento de la primera página"), {off});
    reg("draw.pairsOffset", off);
    unsupported(l, {off}, U("la vista de dos páginas empareja siempre 1-2, 3-4…"));
  }
  {
    auto* l = group(U("Herramientas de dibujo: modificadores según la dirección (experimental)"));
    auto* dd = new QCheckBox(U("Activar con un radio de determinación de"));
    auto* radius = new QSpinBox;
    radius->setRange(2, 1000);
    radius->setValue(50);
    row(l, QString(), {dd, radius});
    reg("draw.dirMods", dd);
    reg("draw.dirRadius", radius);
    unsupported(l, {dd, radius}, U("no existe: los modificadores (Mayús, Ctrl) se mantienen con el teclado."));
  }
  {
    auto* l = group(U("Redimensionado"));
    auto* keep = addCheck(l, U("Conservar el grosor de línea"));
    reg("draw.keepWidth", keep);
    unsupported(l, {keep}, U("la selección escala siempre el grosor junto con el trazo."));
  }
  {
    auto* l = group(U("Ajuste (snapping)"));
    auto* rot = new QDoubleSpinBox; rot->setValue(0.3);
    auto* gridTol = new QDoubleSpinBox; gridTol->setValue(0.5);
    auto* grid = new QDoubleSpinBox; grid->setValue(14.17);
    auto* shapes = addCheck(l, U("Usar el ajuste para las formas reconocidas"));
    row(l, U("Tolerancia del ajuste de rotación"), {rot});
    row(l, U("Tolerancia del ajuste a la cuadrícula"), {gridTol});
    row(l, U("Tamaño de la cuadrícula"), {grid});
    reg("draw.snapRot", rot); reg("draw.snapGridTol", gridTol); reg("draw.snapGrid", grid); reg("draw.snapShapes", shapes);
    unsupported(l, {rot, gridTol, grid, shapes}, U("la app ajusta la rotación de la selección a pasos fijos con Mayús y no tiene cuadrícula de ajuste."));
  }
  {
    auto* l = group(U("Reconocedor de formas"));
    auto* min = new QSpinBox;
    min->setRange(1, 200);
    min->setValue(40);
    row(l, U("Tamaño mínimo del trazo"), {min});
    reg("draw.recognizerMin", min);
    unsupported(l, {min}, U("el reconocedor de formas de la app (interruptor «Formas automáticas») no tiene umbral de tamaño."));
  }
  {
    auto* l = group(U("Caché de PDF"));
    auto* th = new QSpinBox;
    th->setRange(0, 900);
    th->setValue(5);
    row(l, U("Volver a dibujar cuando el zoom de la página cambie más de"), {th});
    reg("draw.rerender", th);
    unsupported(l, {th}, U("el fondo PDF se rasteriza en un hilo aparte a la medida del zoom con su caché acotada; no hay umbral configurable."));
  }
  endPage();
}

// ---- 9. Valores predeterminados -------------------------------------------------------------------------------------------------------------
void PrefsDialog::buildDefaults() {
  newPage(pageTitles()[8]);
  {
    auto* l = group(U("Herramienta predeterminada"));
    note(l, U("Elige la herramienta y sus ajustes para el botón «Predeterminado» (Herramientas → Herramienta predeterminada)."));
    buttonRow(l, "def.tool", U("Herramienta"), &XppPrefs::defaultTool);
  }
  {
    auto* l = group(U("Puntero láser"));
    note(l, U("El puntero láser deja un trazo que se desvanece al poco tiempo."));
    ispin(l, "def.laserFade", U("Tiempo de desvanecimiento"), &XppPrefs::laserFadeMs, 0, 4000, U(" ms"));
  }
  {
    auto* l = group(U("Tabulador"));
    auto* on = check(l, "def.tabSpaces", U("Sangrar con espacios"), &XppPrefs::tabSpaces);
    auto* n = ispin(l, "def.tabCount", U("Número de espacios:"), &XppPrefs::tabSpaceCount, 1, 8);
    connect(on, &QCheckBox::toggled, n, &QWidget::setEnabled);
    n->setEnabled(false);
  }
  {
    auto* l = group(U("Paleta de colores"));
    palette = new QComboBox(this);
    for (const Palette& p : palettes()) palette->addItem(p.name, p.id);
    fitCombo(palette);
    row(l, U("Paleta"), {palette});
    reg("def.palette", palette);
    note(l, U("Cambia los botones de color de la barra y del cuadro de herramientas."));
  }
  {
    auto* l = group(U("Tamaños de las herramientas"));
    note(l, U("Los cinco tamaños de cada herramienta, separados por comas (decimales con punto). También se cambian con clic derecho o pulsación larga en un tamaño de la barra."));
    struct S { SizeKind k; const char* key; QString label; };
    const S ss[] = {{SizeKind::Pen, "def.sizes.pen", U("Lápiz")}, {SizeKind::Highlighter, "def.sizes.highlighter", U("Marcador")}, {SizeKind::Eraser, "def.sizes.eraser", U("Borrador")}, {SizeKind::Text, "def.sizes.text", U("Texto")}};
    for (const S& s : ss) {
      auto* e = new QLineEdit;
      e->setMinimumWidth(260);
      row(l, s.label, {e});
      reg(s.key, e);
      sizeEdits_.insert(sizeKindKey(s.k), e);
    }
  }
  {
    auto* l = group(U("Cuadro de herramientas"));
    toolbox = addCheck(l, U("Cuadro de herramientas activado (tecla Q o botón lateral del lápiz)"));
    reg("def.toolbox", toolbox);
  }
  endPage();
}

// ---- 10. Grabación de audio ------------------------------------------------------------------------------------------------------------------------
void PrefsDialog::buildAudio() {
  newPage(pageTitles()[9]);
  auto* l = group(U("Grabación de audio"));
  auto* banner = new QLabel(U("Esta versión no graba ni reproduce audio asociado a las notas: el formato de los documentos ya tiene sitio para ello, pero la grabación no está hecha. "
                              "Los ajustes de abajo se muestran para que se vea qué tiene el original y no hacen nada."));
  banner->setProperty("dlg", "warn");
  banner->setWordWrap(true);
  l->addWidget(banner);
  auto* off = addCheck(l, U("Desactivar el audio"));
  auto* folder = new QLineEdit;
  auto* in = new QComboBox; in->addItem(U("Predeterminado del sistema"));
  auto* out = new QComboBox; out->addItem(U("Predeterminado del sistema"));
  auto* rate = new QComboBox; for (const char* s : {"16000 Hz", "44100 Hz", "96000 Hz", "192000 Hz"}) rate->addItem(U(s));
  rate->setCurrentIndex(1);
  auto* gain = new QDoubleSpinBox; gain->setValue(1);
  auto* seek = new QSpinBox; seek->setValue(5); seek->setSuffix(U(" s"));
  
  row(l, U("Carpeta de almacenamiento"), {folder});
  row(l, U("Dispositivo de entrada"), {in});
  row(l, U("Dispositivo de salida"), {out});
  row(l, U("Frecuencia de muestreo"), {rate});
  row(l, U("Ganancia"), {gain});
  row(l, U("Tiempo de búsqueda predeterminado"), {seek});
  reg("audio.disable", off); reg("audio.folder", folder); reg("audio.in", in); reg("audio.out", out); reg("audio.rate", rate); reg("audio.gain", gain); reg("audio.seek", seek);
  for (QWidget* w : {static_cast<QWidget*>(off), static_cast<QWidget*>(folder), static_cast<QWidget*>(in), static_cast<QWidget*>(out), static_cast<QWidget*>(rate), static_cast<QWidget*>(gain), static_cast<QWidget*>(seek)}) { w->setEnabled(false); disabled_.append(w); }
  endPage();
}

// ---- 11. LaTeX --------------------------------------------------------------------------------------------------------------------------------------
void PrefsDialog::buildLatex() {
  newPage(pageTitles()[10]);
  auto* l = group(U("LaTeX"));
  auto* banner = new QLabel(U("Las fórmulas se componen con el motor integrado de la app (sin instalar LaTeX ni KaTeX) y se guardan como en la app Electron: el texto TeX queda en el archivo y cualquiera de las dos apps lo reabre y edita. "
                              "Por eso no hay orden de generación, plantilla ni prueba que configurar."));
  banner->setProperty("dlg", "warn");
  banner->setWordWrap(true);
  l->addWidget(banner);
  auto* on = addCheck(l, U("Activar la herramienta LaTeX"));
  on->setChecked(true);
  auto* cmd = new QLineEdit(U("pdflatex -interaction=nonstopmode"));
  auto* tpl = new QLineEdit;
  
  row(l, U("Orden de generación"), {cmd});
  row(l, U("Plantilla global"), {tpl});
  reg("latex.enable", on); reg("latex.command", cmd); reg("latex.template", tpl);
  for (QWidget* w : {static_cast<QWidget*>(on), static_cast<QWidget*>(cmd), static_cast<QWidget*>(tpl)}) { w->setEnabled(false); disabled_.append(w); }
  note(l, U("La herramienta LaTeX de la app está siempre disponible (tecla de la barra de herramientas)."));
  endPage();
}

// ---- 12. Idioma --------------------------------------------------------------------------------------------------------------------------------------
void PrefsDialog::buildLanguage() {
  newPage(pageTitles()[11]);
  auto* l = group(U("Ajustes de idioma"));
  note(l, U("Elige el idioma (requiere reiniciar)."));
  auto* lang = new QComboBox;
  lang->addItem(U("Español"));
  reg("lang.language", lang);
  row(l, U("Idioma"), {lang});
  unsupported(l, {lang}, U("la interfaz está solo en español, como la app original de este proyecto; no hay otras traducciones."));
  endPage();
}

// ---- values in and out -----------------------------------------------------------------------------------------------------------------------------
void PrefsDialog::load(const Prefs& p, const TabletSettings& t) {
  loading_ = true;
  opened_ = t;
  theme->setCurrentIndex(std::max(0, theme->findData(p.theme)));
  palette->setCurrentIndex(std::max(0, palette->findData(p.palette)));
  effects->setCurrentIndex(std::max(0, effects->findData(p.effects)));
  welcome->setChecked(p.welcome);
  finger->setChecked(p.fingerDraw);
  over->setChecked(p.saveOverOpened);
  toolbox->setChecked(c_.toolboxSettings().enabled);
  autosaveOn->setChecked(p.autosaveMinutes > 0);
  for (const Binder& b : binders_) b.load(p.xpp, t);
  if (p.autosaveMinutes > 0) autosave->setValue(p.autosaveMinutes);   // an interval chosen in an older version (or by the Electron app) is the one shown
  autosave->setEnabled(autosaveOn->isChecked());
  const SizePresets sz = prefsSizes(p);
  for (auto it = sizeEdits_.begin(); it != sizeEdits_.end(); ++it) {
    for (int k = 0; k < kSizeKinds; k++) {
      if (it.key() != sizeKindKey(SizeKind(k))) continue;
      QStringList parts;
      for (double v : sz.of(SizeKind(k))) parts << QString::number(v, 'g', 6);
      it.value()->setText(parts.join(", "));
    }
  }
  loading_ = false;
}

Prefs PrefsDialog::collect(TabletSettings* tablet) const {
  Prefs p = c_.prefs();
  p.theme = theme->currentData().toString() == "neon" ? "neon" : "classic";
  p.palette = palette->currentData().toString();
  const QString fx = effects->currentData().toString();
  p.effects = fx == "full" || fx == "reduced" ? fx : "auto";
  p.welcome = welcome->isChecked();
  p.fingerDraw = finger->isChecked();
  p.saveOverOpened = over->isChecked();
  XppPrefs x = p.xpp;
  TabletSettings t = c_.editor().tablet();
  for (const Binder& b : binders_) b.save(x, t);
  p.xpp = x;
  p.autosaveMinutes = autosaveOn->isChecked() ? std::clamp(autosave->value(), 1, 120) : 0;
  // sizes: a list that does not read as numbers within the limits keeps the saved one
  SizePresets sz = prefsSizes(p);
  for (int k = 0; k < kSizeKinds; k++) {
    QLineEdit* e = sizeEdits_.value(sizeKindKey(SizeKind(k)));
    if (!e) continue;
    std::vector<double> v;
    bool ok = true;
    for (const QString& part : e->text().split(',', Qt::SkipEmptyParts)) {
      bool good = false;
      const double d = part.trimmed().replace(',', '.').toDouble(&good);
      if (!good) { ok = false; break; }
      v.push_back(d);
    }
    if (ok && !v.empty()) {
      SizePresets cand = sz;
      cand.of(SizeKind(k)) = v;
      const SizePresets clean = sanitizeSizes(sizesToJson(cand));
      if (clean.of(SizeKind(k)) == v) sz = cand;
    }
  }
  setPrefsSizes(p, sz);
  if (tablet) *tablet = t;
  return p;
}

void PrefsDialog::openDialog() {
  load(c_.prefs(), c_.editor().tablet());
  showPage(0);
  c_.restoreGeometry(this, "prefs", QSize(860, 600));
  show();
  raise();
  activateWindow();
  finger->setFocus();
}

void PrefsDialog::hideEvent(QHideEvent* e) {
  c_.rememberGeometry(this, "prefs");
  QDialog::hideEvent(e);
}

void PrefsDialog::reject() {   // Cancelar and Esc: whatever was edited is thrown away (the next opening reads the preferences again)
  QDialog::reject();
}

void PrefsDialog::accept() {
  TabletSettings t;
  const Prefs p = collect(&t);
  const SizePresets sizes = prefsSizes(p);
  const bool boxOn = toolbox->isChecked();
  QDialog::accept();   // hides it first, as the Electron dialog closes before reporting
  c_.applyPrefs(p);
  TabletSettings cur = c_.editor().tablet();   // the tablet dialog may have changed other fields while this one was open: only what this dialog changed is applied
  if (t.barrel != opened_.barrel) cur.barrel = t.barrel;
  if (t.barrel2 != opened_.barrel2) cur.barrel2 = t.barrel2;
  if (t.eraser != opened_.eraser) cur.eraser = t.eraser;
  if (t.penLook != opened_.penLook) cur.penLook = t.penLook;
  if (!(cur == c_.editor().tablet())) c_.applyTablet(cur);
  if (!(c_.sizes().v == sizes.v)) c_.applySizes(sizes);
  if (boxOn != c_.toolboxSettings().enabled) c_.setToolboxEnabled(boxOn);
}
