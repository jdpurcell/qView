#include "mainwindow.h"
#include "qvapplication.h"
#include "qvwin32functions.h"

#include <QCommandLineParser>
#include <QFontDatabase>

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setOrganizationName("qView");
    QCoreApplication::setApplicationName("qView");
    QCoreApplication::setApplicationVersion(QString::number(VERSION));

    QString defaultStyleName;
#if defined Q_OS_WIN && QT_VERSION >= QT_VERSION_CHECK(6, 8, 1)
    // windows11 style works on Windows 10 too if the right font is available
    if (QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows11)
        defaultStyleName = "windows11";
#endif
    // Convenient way to set a default style but still allow the user to customize it
    if (!defaultStyleName.isEmpty() && qEnvironmentVariableIsEmpty("QT_STYLE_OVERRIDE"))
        qputenv("QT_STYLE_OVERRIDE", defaultStyleName.toLocal8Bit());

    QVApplication app(argc, argv);

#if defined Q_OS_WIN && QT_VERSION >= QT_VERSION_CHECK(6, 8, 1)
    // For windows11 style on Windows 10, make sure we have the font it needs, otherwise change style
    if (QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows11 &&
        QApplication::style()->name() == "windows11" &&
        !QFontDatabase::families().contains("Segoe Fluent Icons"))
    {
        const QString fontPath = QDir(QApplication::applicationDirPath()).filePath("fonts/Segoe Fluent Icons.ttf");
        if (QFile::exists(fontPath))
            QFontDatabase::addApplicationFont(fontPath);
        else
            QApplication::setStyle("windowsvista");
    }
#endif

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QObject::tr("file"), QObject::tr("The file to open."));
#if defined Q_OS_WIN && WIN32_LOADED && QT_VERSION < QT_VERSION_CHECK(6, 7, 2)
    // Workaround for unicode characters getting mangled in certain cases. To support unicode arguments on
    // Windows, QCoreApplication normally ignores argv and gets them from the Windows API instead. But this
    // only happens if it thinks argv hasn't been modified prior to being passed into QCoreApplication's
    // constructor. Certain characters like U+2033 (double prime) get converted differently in argv versus
    // the value Qt is comparing with (__argv). This makes Qt incorrectly think the data was changed, and
    // it skips fetching unicode arguments from the API.
    // https://bugreports.qt.io/browse/QTBUG-125380
    parser.process(QVWin32Functions::getCommandLineArgs());
#else
    parser.process(app);
#endif

    auto *window = QVApplication::newWindow();
    if (!parser.positionalArguments().isEmpty())
        QVApplication::openFile(window, parser.positionalArguments().constFirst(), true);

    return QApplication::exec();
}
