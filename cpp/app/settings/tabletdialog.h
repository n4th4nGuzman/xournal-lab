// "Lápiz y tableta": pen tip and size, pressure curve with live readout and a test sheet, what the pen's buttons do, the express keys of the remote and the
// key / wheel / mouse-button bindings. Port of app/src/renderer/tabletui.ts. It is a separate top-level window (not modal) so the page stays visible and it
// can be dragged to another monitor; it remembers where it was.
#pragma once
#include "tabletcfg.h"
#include <QDialog>
#include <map>
#include <set>
#include <vector>

class SettingsController;
class CurveWidget;
class PadWidget;
class BrushCard;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QVBoxLayout;

class TabletDialog : public QDialog {
  Q_OBJECT
 public:
  TabletDialog(SettingsController& c, QWidget* parent);

  /** Show it with the current settings (the pen size is the style's). */
  void openDialog();
  const TabletSettings& current() const { return s_; }
  /** A pen or mouse sample anywhere in the application while the dialog is open (live readout). pressure is 0..1. */
  void sample(const QString& device, double pressure, double tiltX, double tiltY, int buttons, PenButton button);

  // for the self test
  QComboBox* penCombo(int i) const { return penSel_[i]; }
  QComboBox* expressCombo(int i) const { return express_[size_t(i)]; }
  CurveWidget* curveWidget() const { return curve_; }
  PadWidget* pad() const { return pad_; }
  QComboBox* presetCombo() const { return preset_; }
  QComboBox* lookCombo() const { return look_; }
  QComboBox* actionCombo() const { return action_; }
  QDoubleSpinBox* sizeSpin() const { return sizeNum_; }
  QPushButton* captureButton() const { return capture_; }
  QPushButton* suggestButton() const { return suggest_; }
  QString readoutText() const;
  /** One raw input event of the application, for the trace under the readout: a press / release / proximity always, a move only when the buttons changed since the last
   *  one of that device kind; `key` identifies the event so that the same one seen again on its way through the widgets is logged once. */
  void logInput(bool tablet, const QString& phase, int button, int buttons, const QString& source, const QString& device, qulonglong key);
  QString traceText() const;
  /** "izq+central", "der", "ninguno": the buttons of a mask in words. */
  static QString buttonWords(int mask);
  int keyRowCount() const { return keyRows_; }
  BrushCard* card(BrushKind k) const;
  void removeKey(const QString& combo);
  void setCurve(const PressureCurve& c);      // as dragging a handle would
  void setSize(double v);
  void setBrush(BrushKind k);
  void setPenAction(int which, PenAction a);  // 0 barrel, 1 barrel2, 2 eraser end

 protected:
  void hideEvent(QHideEvent*) override;
  void keyPressEvent(QKeyEvent* e) override;

 private:
  void build();
  void commit();
  void paintAll();
  void paintKeys();
  void stopCapture();
  void startCapture();

  SettingsController& c_;
  TabletSettings s_;
  double size_ = 1.41;
  QVBoxLayout* body_ = nullptr;
  std::map<BrushKind, BrushCard*> cards_;
  QSlider* sizeRange_ = nullptr;
  QDoubleSpinBox* sizeNum_ = nullptr;
  CurveWidget* curve_ = nullptr;
  PadWidget* pad_ = nullptr;
  QComboBox* preset_ = nullptr;
  QSlider* dead_ = nullptr;
  QComboBox* look_ = nullptr;   // TabletSettings::penLook: how the plain pen turns pressure into width
  QProgressBar *bar_ = nullptr, *barMapped_ = nullptr;
  QLabel* readout_ = nullptr;
  QPlainTextEdit* trace_ = nullptr;
  int lastMask_[2] = {0, 0};      // buttons of the last logged event, per device kind (mouse, tablet)
  qulonglong lastKey_ = 0;
  QComboBox* penSel_[3] = {nullptr, nullptr, nullptr};
  std::vector<QComboBox*> express_;
  QComboBox* action_ = nullptr;
  QPushButton *capture_ = nullptr, *suggest_ = nullptr;
  QWidget* keyList_ = nullptr;
  int keyRows_ = 0;
  QString picked_;
  std::vector<std::pair<double, double>> recent_;
  std::set<int> levels_;
  bool building_ = false;
};
