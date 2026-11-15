#ifndef QVWINDOWS11STYLE_H
#define QVWINDOWS11STYLE_H

#include <QProxyStyle>

#ifdef Q_OS_WIN
class QvWindows11Style : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const override;
};
#endif

#endif // QVWINDOWS11STYLE_H
