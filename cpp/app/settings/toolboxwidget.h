// The floating tool box: a frameless window of zones (rows) of big buttons and separators that opens at the pointer to change tool quickly, with a
// customise mode (drag buttons in from the catalogue or from the window's toolbar, reorder, move between zones, drag out to remove, add and remove
// zones). Port of the UI of app/src/renderer/toolbox.ts; the layout rules and the edits are core/toolboxmodel.h.
#pragma once
#include "toolboxmodel.h"
#include <QMimeData>
#include <QPointF>
#include <QWidget>
#include <optional>
#include <vector>

class SettingsController;
class WrapBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class QScrollArea;

class ToolboxWidget : public QWidget {
  Q_OBJECT
 public:
  ToolboxWidget(SettingsController& c, QWidget* parent);

  bool isOpen() const { return isVisible(); }
  bool isEditing() const { return editing_; }
  /** Show it with its centre on `globalPos` (a bit above: the pointer lands on the first row). False when it is switched off. */
  bool open(const QPoint& globalPos);
  void closeBox();
  void setEditing(bool on);
  /** A press (mouse, pen or finger) anywhere in the application: closes the box unless it is being customised or the press is inside it. Inside is decided from the global
   *  position, the one fact every kind of event has: a pen's tablet event reaches the application filter first as the event of the QWindow, which is no QWidget. */
  void outsidePress(const QPointF& globalPos);
  const ToolboxSettings& settings() const { return s_; }

  // the model edits, shared by the widgets and by the tests
  bool dropOn(int row, const ToolboxDrag& d, int index);
  void removeAt(int row, int index);
  void removeZone(int row);
  void addZone();
  void restoreDefault();

  /** Drag data: the item id and, when it comes from a zone of the box, where. */
  static QMimeData* makeMime(const ToolboxDrag& d);
  static std::optional<ToolboxDrag> parseMime(const QMimeData* m);

  // what is on screen, for the self test
  int zoneCount() const { return int(zones_.size()); }
  WrapBox* zoneBox(int i) const { return zones_[size_t(i)].box; }
  QStringList zoneIds(int i) const;            // item ids as drawn ("|" for separators)
  QWidget* itemWidget(const QString& id) const;
  QStringList poolIds() const;
  QPushButton* smallButton(const QString& text) const;
  QLineEdit* searchField() const { return search_; }
  void setPoolGroup(const QString& g);
  WrapBox* poolBox() const { return pool_; }
  QWidget* bodyWidget() const { return body_; }

 protected:
  void keyPressEvent(QKeyEvent* e) override;

 private:
  struct Zone { WrapBox* box = nullptr; int row = 0; std::vector<int> modelIndex; };
  void render();
  void renderPool();
  void commit();
  void place();
  QWidget* buttonFor(const ToolboxItem& item, int row, int index);
  void keepOnScreen();

  SettingsController& c_;
  ToolboxSettings s_;
  bool editing_ = false;
  QPoint at_{200, 200};
  QString group_, query_;
  QVBoxLayout* layout_ = nullptr;
  QLabel* title_ = nullptr;
  QPushButton* editBtn_ = nullptr;
  QWidget* body_ = nullptr;
  std::vector<Zone> zones_;
  WrapBox* pool_ = nullptr;
  WrapBox* chips_ = nullptr;
  QLineEdit* search_ = nullptr;
};
