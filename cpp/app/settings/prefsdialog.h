// The preferences dialog, organised as the original Xournal++ one: a list of pages on the left (Cargar/guardar, Sistema de entrada, Ratón, Lápiz, Pantalla
// táctil, Ver, Escala, Área de dibujo, Valores predeterminados, Grabación de audio, LaTeX, Idioma), grouped boxes on the right, Cancelar / Aceptar at the bottom.
// Everything this app already had (theme, effects, palette, welcome, finger drawing, saving over the opened file, autosave, the tool box switch, the pen look,
// the size presets) sits where the original would put it. PARIDAD/prefs_xpp.md lists every setting and where it went.
//
// The dialog edits a copy: Aceptar applies it, Cancelar (or Esc) discards it. A control whose setting this app cannot honour is shown disabled with the
// reason next to it; none is a switch that silently does nothing. A separate window that is not modal and remembers its place; Intro accepts, Esc cancels.
#pragma once
#include "prefs.h"
#include "tabletcfg.h"
#include <QDialog>
#include <QHash>
#include <QMap>
#include <QPushButton>
#include <functional>
#include <vector>

class SettingsController;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QVBoxLayout;

/** A colour with an "unset = the theme's own" state: a swatch button (opens the colour dialog) and a button that clears it. Values are "#rrggbb" or "#aarrggbb". */
class ColorButton : public QWidget {
  Q_OBJECT
 public:
  ColorButton(bool alpha, bool allowDefault, QWidget* parent);
  QString value() const { return value_; }
  void setValue(const QString& v);
  /** What an unset colour looks like (shown on the swatch with the word "tema"). */
  void setShown(const QColor& c) { shown_ = c; paintSwatch(); }
  void setUnsetText(const QString& t) { unset_ = t; paintSwatch(); }
  void pick();   // opens the dialog
 signals:
  void changed();
 private:
  void paintSwatch();
  bool alpha_;
  QString value_;
  QColor shown_{128, 128, 128};
  QString unset_ = QStringLiteral("tema");
  QPushButton *swatch_, *reset_ = nullptr;
};

/** Tool, colour and size of a button (mouse button, stylus button, eraser end, the default button): the original's "ButtonConfig" line. */
class ButtonRow : public QWidget {
  Q_OBJECT
 public:
  ButtonRow(const QList<QPair<QString, QString>>& tools, QWidget* parent);   // (id, label) pairs for the tool list
  QComboBox* tool = nullptr;
  QComboBox* colorMode = nullptr;   // 0 unchanged, 1 custom
  ColorButton* color = nullptr;
  QComboBox* size = nullptr;        // 0 unchanged, 1..5
  void load(const ButtonCfg& c);
  ButtonCfg value() const;
};

class PrefsDialog : public QDialog {
  Q_OBJECT
 public:
  PrefsDialog(SettingsController& c, QWidget* parent);
  /** Fill the fields from the current preferences and show it. */
  void openDialog();
  /** Fill the fields from `p` / `t` without showing (the self test and openDialog use it). */
  void load(const Prefs& p, const TabletSettings& t);

  // for the self test and the screenshots
  static QStringList pageTitles();
  int pageCount() const;
  void showPage(int i);
  int currentPage() const;
  QWidget* page(int i) const;
  /** A control by its key ("mouse.middle", "pen.minPressure"...); null when there is none. */
  QWidget* control(const QString& key) const { return controls_.value(key); }
  template <class T> T* get(const QString& key) const { return qobject_cast<T*>(controls_.value(key)); }
  QStringList controlKeys() const { return controls_.keys(); }
  QStringList disabledKeys() const;   // controls shown disabled because the app cannot honour them
  QTableWidget* deviceTable() const { return devices_; }
  void refreshDevices();
  /** The preferences the fields stand for right now (what Aceptar would apply). */
  Prefs collect(TabletSettings* tablet = nullptr) const;

  QComboBox *theme = nullptr, *palette = nullptr, *effects = nullptr;
  QCheckBox *welcome = nullptr, *finger = nullptr, *over = nullptr, *toolbox = nullptr, *autosaveOn = nullptr;
  QSpinBox* autosave = nullptr;   // the interval in minutes (used while autosaveOn is checked)

 public slots:
  void accept() override;
  void reject() override;

 protected:
  void hideEvent(QHideEvent*) override;
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  struct Binder { std::function<void(const XppPrefs&, const TabletSettings&)> load; std::function<void(XppPrefs&, TabletSettings&)> save; };
  QWidget* newPage(const QString& title);
  QWidget* row(QVBoxLayout* l, const QString& label, std::initializer_list<QWidget*> ws);
  QVBoxLayout* group(const QString& title);   // a grouped box on the page being built; returns its layout
  QLabel* note(QVBoxLayout* l, const QString& text);
  void endPage();
  void reg(const QString& key, QWidget* w) { controls_.insert(key, w); }
  QCheckBox* addCheck(QVBoxLayout* l, const QString& text);
  QCheckBox* check(QVBoxLayout* l, const QString& key, const QString& label, bool XppPrefs::*m);
  QSpinBox* ispin(QVBoxLayout* l, const QString& key, const QString& label, int XppPrefs::*m, int lo, int hi, const QString& suffix = QString());
  QDoubleSpinBox* dspin(QVBoxLayout* l, const QString& key, const QString& label, double XppPrefs::*m, double lo, double hi, double step, int decimals, const QString& suffix = QString());
  QComboBox* combo(QVBoxLayout* l, const QString& key, const QString& label, int XppPrefs::*m, const QStringList& items);
  QLineEdit* line(QVBoxLayout* l, const QString& key, const QString& label, QString XppPrefs::*m);
  ColorButton* colorRow(QVBoxLayout* l, const QString& key, const QString& label, QString XppPrefs::*m, bool alpha, bool allowDefault, const QColor& shown);
  void addButtonRow(QVBoxLayout* l, const QString& label, ButtonRow* br);
  ButtonRow* buttonRow(QVBoxLayout* l, const QString& key, const QString& label, ButtonCfg XppPrefs::*m);
  /** Show `w` disabled and say why in a note below `l`'s last row: the app cannot honour this setting. */
  void unsupported(QVBoxLayout* l, std::initializer_list<QWidget*> ws, const QString& why);

  void buildSaveLoad();
  void buildInput();
  void buildMouse();
  void buildStylus();
  void buildTouch();
  void buildView();
  void buildZoom();
  void buildDrawing();
  void buildDefaults();
  void buildAudio();
  void buildLatex();
  void buildLanguage();

  SettingsController& c_;
  QListWidget* nav_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  QHash<QString, QWidget*> controls_;
  std::vector<Binder> binders_;
  QList<QPair<QString, QString>> toolList_;
  QTableWidget* devices_ = nullptr;
  QHash<QString, QLineEdit*> sizeEdits_;
  QMap<QString, int> devMap_;   // device key -> DeviceClass, as edited
  QList<QWidget*> disabled_;
  QVBoxLayout* cur_ = nullptr;   // the page being built
  TabletSettings opened_;        // the pen settings as the dialog opened: only what the dialog changed is applied (the tablet dialog may have changed others meanwhile)
  bool loading_ = false;
};
