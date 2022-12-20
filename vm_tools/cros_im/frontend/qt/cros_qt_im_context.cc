// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "frontend/qt/cros_qt_im_context.h"

#include <mutex>
#include <string>
#include <vector>
#include <qpa/qplatformnativeinterface.h>
#include <QLocale>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QtDebug>
#include <QtGlobal>
#include <QtGui/private/qhighdpiscaling_p.h>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtGui/private/qxkbcommon_p.h>
#else
#include <QtXkbCommonSupport/private/qxkbcommon_p.h>
#endif
#include <QTextCharFormat>
#include <QThread>

#include "backend/wayland_manager.h"

namespace {
std::mutex init_lock;
}

namespace cros_im {
namespace qt {

bool CrosQtIMContext::isValid() const {
  // has to be true, even before init, as otherwise init() never functions
  // correctly.
  return true;
}

void CrosQtIMContext::setFocusObject(QObject* object) {
  is_in_focus_ = object != nullptr;
  if (!inited_)
    return;

  if (!inputMethodAccepted())
    return;

  if (object) {
    // focus in
    Activate();
  } else {
    // focus out
    is_activated_ = false;
    backend_->Deactivate();
  }
}

void CrosQtIMContext::Activate() {
  Q_ASSERT(inited_);
  qDebug() << "Activate()";
  is_activated_ = true;
  if (!qApp)
    return;
  QWindow* window = qApp->focusWindow();
  if (is_x11_) {
    backend_->ActivateX11(window->winId());
  } else {
    wl_surface* surface = static_cast<struct wl_surface*>(
        QGuiApplication::platformNativeInterface()->nativeResourceForWindow(
            "surface", window));
    if (!surface) {
      qWarning() << "wl_surface is nullptr";
      return;
    }
    backend_->Activate(surface);
  }
  // hint is set in update()
  // probably need to check latest hint to see if we want to show input panel
  backend_->ShowInputPanel();
}

void CrosQtIMContext::invokeAction(QInputMethod::Action action,
                                   int cursorPosition) {
  if (!inited_)
    return;

  if (action == QInputMethod::Click) {
    commit();
  }
}

void CrosQtIMContext::reset() {
  if (!inited_)
    return;

  backend_->Reset();
}

void CrosQtIMContext::commit() {
  // Qt commanding plugin to commit something
  // Currently just commit preedit, but for zh_CN, preedit isn't a legal input
  // (a possible input under other conditions) This probably should be locale
  // dependent, e.g. in Japanese, commit preedit should do fine, but in Chinese
  // we would want latin without space or simply reset.

  if (!inited_)
    return;

  qDebug() << "CrosQtIMContext::commit()";
  if (!qApp)
    return;
  QObject* input = qApp->focusObject();
  if (!input)
    return;
  QInputMethodEvent event;
  event.setCommitString(QString::fromStdString(preedit_));
  QCoreApplication::sendEvent(input, &event);
  preedit_.clear();
  preedit_attributes_.clear();
  backend_->Reset();
}

void CrosQtIMContext::update(Qt::InputMethodQueries) {
  // We might also need to send surrounding text here.
  if (!is_activated_ && inputMethodAccepted()) {
    Activate();
  }
}

bool CrosQtIMContext::filterEvent(const QEvent* event) {
  // We don't capture any event as our key comes directly from the compositor
  return false;
}

QLocale CrosQtIMContext::locale() const {
  // Should get locale from IME, but it's not implemented in our backend
  QLocale empty_locale;
  return empty_locale;
}

bool CrosQtIMContext::hasCapability(
    QPlatformInputContext::Capability capability) const {
  return false;
}

void CrosQtIMContext::cursorRectangleChanged() {
  if (!inited_)
    return;

  if (!qApp)
    return;
  QWindow* window = qApp->focusWindow();
  if (!window)
    return;
  QRect rect = qApp->inputMethod()->cursorRectangle().toRect();
  if (!rect.isValid())
    return;

  // In some HiDPI situations, crOS will let Qt handle integer scaling and (if
  // needed) do its fractional scaling based on already-scaled windows. We need
  // to handle cursor location scaling for the window scaling step done by Qt.

  // Qt Wayland has complicated logic around window title bar handling:
  //
  // Under wayland, we can have client / server side decoration for titlebar.
  // In total, we have 4 title bar situations: server side decoration,client
  // side decoration drawn by Qt, client side decoration drawn by application,
  // and no title bar. Backend want the cursor location relative to wl_surface
  // origin.
  //
  // I haven't seen any situation where a double title bar was drawn, so I
  // can safely assume all the collaboration between objects around which
  // is drawing the title bar is functioning correctly.
  //
  // When the title bar is drawn by server side decoration, QWindow's top left
  // corner is the top left corner of wl_surface, we don't need to have any
  // offset here.
  // When title bar is drawn by Qt at the client side, QWindow's origin starts
  // below the title bar, but wl_surface's origin will be at Qt's decoration's
  // top left corner, we need to add an offset to compensate.
  // When title bar is drawn by the app, entire wl_surface is exposed to the
  // app as QWindow, and wl_surface origin matches QWindow origin, so no
  // offset is needed.
  // When there's no title bar (app is borderless), wl_surface origin matches
  // QWindow origin, no offset is needed.
  if (!is_x11_ && !window->flags().testFlag(Qt::FramelessWindowHint)) {
    QMargins margin = window->frameMargins();
    rect.translate(margin.left(), margin.top());
  }
  rect = QHighDpi::toNativePixels(rect, window);
  backend_->SetCursorLocation(rect.x(), rect.y(), rect.width(), rect.height());
}

bool CrosQtIMContext::init() {
  qDebug() << "init()";
  if (failed_init_) {
    qWarning() << "Failed init!";
    return false;
  }

  // Init sequence is a critical path, and need to be guarded
  qDebug() << "Trying to hold init lock";
  if (!init_lock.try_lock())
    return false;

  std::lock_guard<std::mutex> lock(init_lock, std::adopt_lock);
  if (inited_) {
    qWarning() << "Duplicate init() call!";
    return true;
  }

  if (is_x11_) {
    // xcb backend is used
    qInfo() << "Init for x11";
    backend_observer_ = std::make_unique<BackendObserver>(this);
    backend_ = std::make_unique<IMContextBackend>(backend_observer_.get());
    inited_ = true;
    if (is_in_focus_)
      Activate();
    return true;
  } else if (QGuiApplication::platformName() == "wayland") {
    // wayland backend is used and seems initialized (otherwise won't return
    // correct name)
    qInfo() << "wayland platform detected, starting cros input plugin";
    wl_display* display = static_cast<wl_display*>(
        qGuiApp->platformNativeInterface()->nativeResourceForWindow("display",
                                                                    nullptr));
    if (!display) {
      qWarning()
          << "Detect wayland but failed to get display, continue to wait";
      return false;
    }
    cros_im::WaylandManager::CreateInstance(display);
    backend_observer_ = std::make_unique<BackendObserver>(this);
    backend_ = std::make_unique<IMContextBackend>(backend_observer_.get());
    inited_ = true;
    qInfo() << "Successfully initialized cros IME plugin in wayland mode";
    if (is_in_focus_)
      Activate();
    return true;
  } else if (QGuiApplication::platformName() == "") {
    qDebug()
        << "platformName() is empty, wayland backend is not yet initialised";
    return false;
  } else {
    qWarning() << "Unsupported QPA platform: "
               << QGuiApplication::platformName();
    failed_init_ = true;
    return false;
  }
}

void CrosQtIMContext::BackendObserver::SetPreedit(
    const std::string& preedit,
    int cursor,
    const std::vector<PreeditStyle>& styles) {
  QObject* input = qApp->focusObject();
  if (!input)
    return;
  context_->preedit_attributes_.clear();

  for (auto& backend_style : styles) {
    QTextCharFormat format;
    // Chrome does not currently send DEFAULT, NONE, ACTIVE, INACTIVE.
    switch (backend_style.style) {
      default:
      case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_HIGHLIGHT:
      case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_SELECTION:
        format.setUnderlineStyle(QTextCharFormat::DashUnderline);
        break;
      case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_UNDERLINE:
        format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
        break;
      case ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_INCORRECT:
        format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
        break;
    }
    context_->preedit_attributes_.append(QInputMethodEvent::Attribute(
        QInputMethodEvent::TextFormat, backend_style.index,
        backend_style.length, format));
  }

  context_->preedit_attributes_.append(QInputMethodEvent::Attribute(
      QInputMethodEvent::Cursor, QString::fromStdString(preedit).length(), 1));
  context_->preedit_ = preedit;
  qDebug() << "backend cursor: " << cursor
           << ", preedit size: " << preedit.size();
  QInputMethodEvent event(QString::fromStdString(preedit),
                          context_->preedit_attributes_);
  QCoreApplication::sendEvent(input, &event);
}

void CrosQtIMContext::BackendObserver::SetPreeditRegion(
    int start_offset, int length, const std::vector<PreeditStyle>& styles) {
  // not needed for CJ
  qWarning() << "BackendObserver::SetPreeditRegion() is not implemented";
}

void CrosQtIMContext::BackendObserver::Commit(const std::string& commit) {
  // IME want plugin to commit this text
  // but why both qt and IME can tell plugin to commit?
  qDebug() << "BackendObserver::Commit()";
  if (commit.empty()) {
    qWarning() << "IME backend request to commit empty string";
    return;
  }
  if (!qApp)
    return;
  QObject* input = qApp->focusObject();
  if (!input)
    return;
  QInputMethodEvent event;
  event.setCommitString(QString::fromStdString(commit));
  QCoreApplication::sendEvent(input, &event);
  context_->preedit_ = "";
}

void CrosQtIMContext::BackendObserver::DeleteSurroundingText(int start_offset,
                                                             int length) {
  // not needed for CJ without autocorrect
  // possibly: "if you turn on autocorrect then it gets used instead of
  // backspace for some reason"
  qWarning() << "BackendObserver::DeleteSurroundingText() is not implemented";
}

void CrosQtIMContext::BackendObserver::KeySym(uint32_t keysym, KeyState state) {
  // some key events needs to be directly simulated as compositor only talks
  // to IME when IME is active

  // Modifier is unsupported for now
  qDebug() << "BackendObserver::KeySym()";
  if (!qApp)
    return;

  QObject* input = qApp->focusObject();
  if (!input)
    return;

  QEvent::Type type;

  if (state == KeyState::kReleased) {
    type = QEvent::KeyRelease;
  } else if (state == KeyState::kPressed) {
    type = QEvent::KeyPress;
  } else {
    return;
  }

  char32_t text_ucs4 = xkb_keysym_to_utf32(keysym);

  QKeyEvent event(type, QXkbCommon::keysymToQtKey(keysym, Qt::NoModifier),
                  Qt::NoModifier, 0, keysym, 0,
                  QString::fromUcs4(&text_ucs4, 1));
  QCoreApplication::sendEvent(input, &event);
}
}  // namespace qt
}  // namespace cros_im
