#include "qvwindows11style.h"

#ifdef Q_OS_WIN
#include <QApplication>
#include <QCommonStyle>
#include <QEvent>
#include <QOperatingSystemVersion>
#include <QStyleOption>
#include <QWidget>
#include <QWindow>

#ifdef WIN32_LOADED
#include <qt_windows.h>
#include <dwmapi.h>
#endif

QvWindows11Style::QvWindows11Style(QStyle *style)
    : QProxyStyle(style),
#if defined(WIN32_LOADED) && QT_VERSION >= QT_VERSION_CHECK(6, 11, 0) && QT_VERSION < QT_VERSION_CHECK(6, 11, 3)
      needsFullScreenRoundedCornerWorkaround(QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11_21H2)
#else
      needsFullScreenRoundedCornerWorkaround(false)
#endif
{
}

QSize QvWindows11Style::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const
{
    QSize s = QProxyStyle::sizeFromContents(type, option, size, widget);
    const auto getCommonStyleSize = [&]() {
        return QCommonStyle::sizeFromContents(type, option, size, widget);
    };
    switch (type) {
    case CT_RadioButton:
    case CT_CheckBox:
        s.setHeight(getCommonStyleSize().height());
        break;
    case CT_ItemViewItem:
        s.setHeight(getCommonStyleSize().height() + 4);
        break;
    case CT_MenuBarItem:
        s.setWidth(size.width() + 24);
        break;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 1)
    case CT_MenuItem:
        if (const auto *menuItem = qstyleoption_cast<const QStyleOptionMenuItem *>(option)) {
            if (menuItem->text.contains(u'\t'))
                s.rwidth() += 20;
        }
        break;
#endif
    default:
        break;
    }
    return s;
}

void QvWindows11Style::polish(QApplication *application)
{
    if (needsFullScreenRoundedCornerWorkaround)
        application->installEventFilter(this);
    QProxyStyle::polish(application);
}

void QvWindows11Style::unpolish(QApplication *application)
{
    if (needsFullScreenRoundedCornerWorkaround)
        application->removeEventFilter(this);
    QProxyStyle::unpolish(application);
}

bool QvWindows11Style::eventFilter(QObject *object, QEvent *event)
{
#ifdef WIN32_LOADED
    if (needsFullScreenRoundedCornerWorkaround && (event->type() == QEvent::Show || event->type() == QEvent::WindowStateChange)) {
        const auto *widget = qobject_cast<QWidget *>(object);
        if (widget && widget->isWindow() && widget->testAttribute(Qt::WA_WState_Created) &&
            !widget->windowFlags().testFlag(Qt::FramelessWindowHint) &&
            (event->type() == QEvent::WindowStateChange || widget->isFullScreen()))
        {
            const auto window = widget->windowHandle();
            if (window && window->handle()) {
                const HWND wId = reinterpret_cast<HWND>(widget->winId());
                if (wId) {
                    const DWM_WINDOW_CORNER_PREFERENCE pref = widget->isFullScreen() ? DWMWCP_DEFAULT : DWMWCP_ROUND;
                    DwmSetWindowAttribute(wId, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
                }
            }
        }
    }
#endif
    return QProxyStyle::eventFilter(object, event);
}
#endif
