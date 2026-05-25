#include "qvwindows11style.h"

#ifdef Q_OS_WIN
#include <QCommonStyle>
#include <QStyleOption>

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
#endif
