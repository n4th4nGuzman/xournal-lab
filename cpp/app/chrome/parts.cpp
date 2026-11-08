// What the welcome page shows, read back for the self test. (This file is also the translation unit AUTOMOC pairs with parts.h, which declares the
// Q_OBJECT widgets of the area.)
#include "parts.h"
#include <QRegularExpression>

namespace chrome {

int WelcomePage::paperCardCount() const { return content_ ? content_->findChildren<QPushButton*>(QRegularExpression("^wPaper:")).size() : 0; }
QWidget* WelcomePage::card(const QString& value) const { return content_ ? content_->findChild<QPushButton*>("wPaper:" + value) : nullptr; }
QPushButton* WelcomePage::button(const QString& name) const { return content_ ? content_->findChild<QPushButton*>(name) : nullptr; }
QStringList WelcomePage::recentPaths() const {
  QStringList out;
  if (content_) for (QPushButton* b : content_->findChildren<QPushButton*>(QRegularExpression("^wRecent"))) out << b->property("path").toString();
  return out;
}

}  // namespace chrome
