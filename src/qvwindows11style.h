#ifndef QVWINDOWS11STYLE_H
#define QVWINDOWS11STYLE_H

#include <QProxyStyle>

#ifdef Q_OS_WIN
class QvWindows11Style : public QProxyStyle
{
public:
    explicit QvWindows11Style(QStyle *style = nullptr);

    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const override;

    void polish(QApplication *application) override;

    void unpolish(QApplication *application) override;

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    const bool needsFullScreenRoundedCornerWorkaround;
};
#endif

#endif // QVWINDOWS11STYLE_H
