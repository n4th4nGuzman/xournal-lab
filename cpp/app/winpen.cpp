#include "winpen.h"
#include <QAbstractNativeEventFilter>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QInputMethod>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace winpen {
namespace {

Stats g_stats;

// tpcshrd.h values (MinGW may lack the header)
constexpr quint32 kDisablePressAndHold = 0x00000001, kDisablePenTapFeedback = 0x00000008, kDisablePenBarrelFeedback = 0x00000010, kDisableTouchUiForceOff = 0x00000200,
                  kDisableTouchSwitch = 0x00008000, kDisableFlicks = 0x00010000, kDisableSmoothScrolling = 0x00080000, kDisableFlickFallbackKeys = 0x00100000;

bool onWindowsPlatform() { return QGuiApplication::platformName() == QLatin1String("windows"); }

#ifdef Q_OS_WIN
UINT g_queryMessage = 0;   // RegisterWindowMessage("TabletQuerySystemGestureStatus")

/** The Windows 8 FEEDBACK_TYPE values, in the order of winuser.h. */
constexpr int kFeedback[] = {
    1,    // FEEDBACK_TOUCH_CONTACTVISUALIZATION: the ripple under a finger or pen on contact
    2,    // FEEDBACK_PEN_BARRELVISUALIZATION
    3,    // FEEDBACK_PEN_TAP
    4,    // FEEDBACK_PEN_DOUBLETAP
    5,    // FEEDBACK_PEN_PRESSANDHOLD
    6,    // FEEDBACK_PEN_RIGHTTAP
    7,    // FEEDBACK_TOUCH_TAP
    8,    // FEEDBACK_TOUCH_DOUBLETAP
    9,    // FEEDBACK_TOUCH_PRESSANDHOLD
    10,   // FEEDBACK_TOUCH_RIGHTTAP
    11,   // FEEDBACK_GESTURE_PRESSTAP
};

class NativeFilter : public QAbstractNativeEventFilter {
 public:
  bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override {
    if (g_queryMessage == 0 || type != "windows_generic_MSG") return false;
    const MSG* m = static_cast<const MSG*>(message);
    if (m->message != g_queryMessage) return false;
    *result = LRESULT(tabletGestureFlags());   // the tablet service asks every window what it allows: nothing
    return true;
  }
};
#endif

class Filter : public QObject {
 public:
  using QObject::QObject;
  bool eventFilter(QObject* o, QEvent* e) override {
    switch (e->type()) {
      case QEvent::Show:
      case QEvent::WinIdChange:
        if (auto* w = qobject_cast<QWidget*>(o); w && w->isWindow()) applyToWindow(w);
        break;
      case QEvent::Polish:
        if (auto* w = qobject_cast<QWidget*>(o); w && isTextInput(w)) quietTextInput(w);
        break;
      case QEvent::FocusIn:   // setReadOnly(false) and the like switch the context back on: take it away again before the field is used
        if (auto* w = qobject_cast<QWidget*>(o); w && isTextInput(w) && quietTextInput(w)) QGuiApplication::inputMethod()->update(Qt::ImEnabled);
        break;
      default: break;
    }
    return false;
  }
};

}  // namespace

quint32 tabletGestureFlags() {
  return kDisablePressAndHold | kDisablePenTapFeedback | kDisablePenBarrelFeedback | kDisableTouchUiForceOff | kDisableTouchSwitch | kDisableFlicks | kDisableSmoothScrolling |
         kDisableFlickFallbackKeys;
}

int applyToWindow(QWidget* window) {
  if (!window || !onWindowsPlatform()) return 0;
#ifdef Q_OS_WIN
  const WId id = window->effectiveWinId();
  if (!id) return 0;
  const HWND hwnd = reinterpret_cast<HWND>(id);
  using SetFeedback = BOOL(WINAPI*)(HWND, int, DWORD, UINT32, const VOID*);
  static const SetFeedback set = [] {
    HMODULE m = GetModuleHandleW(L"user32.dll");
    return m ? reinterpret_cast<SetFeedback>(reinterpret_cast<void*>(GetProcAddress(m, "SetWindowFeedbackSetting"))) : nullptr;
  }();
  int off = 0;
  if (set) {
    const BOOL no = FALSE;
    for (int kind : kFeedback) if (set(hwnd, kind, 0, sizeof no, &no)) off++;
  }
  // the tablet service also reads this window property (the documented way to turn press-and-hold and flicks off for one window)
  SetPropW(hwnd, L"MicrosoftTabletPenServiceProperty", reinterpret_cast<HANDLE>(uintptr_t(tabletGestureFlags())));
  g_stats.windows++;
  return off;
#else
  return 0;
#endif
}

bool isTextInput(const QWidget* w) {
  if (!w) return false;
  if (qobject_cast<const QLineEdit*>(w) || qobject_cast<const QAbstractSpinBox*>(w) || qobject_cast<const QTextEdit*>(w) || qobject_cast<const QPlainTextEdit*>(w)) return true;
  if (const auto* cb = qobject_cast<const QComboBox*>(w)) return cb->isEditable();
  if (const auto* area = qobject_cast<const QAbstractScrollArea*>(w->parentWidget()))
    return area->viewport() == w && (qobject_cast<const QTextEdit*>(area) || qobject_cast<const QPlainTextEdit*>(area));
  return false;
}

bool quietTextInput(QWidget* w) {
  if (!w) return false;
  const Qt::InputMethodHints quiet = Qt::ImhNoPredictiveText | Qt::ImhNoTextHandles;
  const bool wasOn = w->testAttribute(Qt::WA_InputMethodEnabled);
  if (!wasOn && (w->inputMethodHints() & quiet) == quiet) return false;
  w->setAttribute(Qt::WA_InputMethodEnabled, false);
  w->setInputMethodHints(w->inputMethodHints() | quiet);
  if (wasOn) g_stats.textInputs++;
  return wasOn;
}

void install() {
  static bool done = false;
  if (done || !QCoreApplication::instance()) return;
  done = true;
  qApp->installEventFilter(new Filter(qApp));
#ifdef Q_OS_WIN
  if (onWindowsPlatform()) {
    g_queryMessage = RegisterWindowMessageW(L"TabletQuerySystemGestureStatus");
    qApp->installNativeEventFilter(new NativeFilter);
  }
#endif
}

Stats stats() { return g_stats; }

}  // namespace winpen
