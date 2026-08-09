#ifndef OPENWITH_H
#define OPENWITH_H

#include <QIcon>
#include <QDialog>
#include <QAbstractButton>
#include <QStandardItemModel>

class OpenWith : public QObject
{
    Q_OBJECT
public:
    struct OpenWithItem {
        QIcon icon;
        QString iconName;
        QString name;
        QString exec;
        QStringList args;
        QStringList categories; // this is only used on linux
        bool isDefault = false;
        void *winAssocHandler = nullptr;
    };

    static QList<OpenWithItem> getOpenWithItems(const QString &filePath);

    static void showOpenWithDialog(QWidget *parent);

    static void openWithExecutable(const QString &executablePath, const QString &filePath);

    static void openWithExecutable(const QString &executablePath, const QStringList &args, const QString &filePath);

    static void openWith(const QString &filePath, const OpenWithItem &openWithItem);

    static QList<OpenWithItem> getOpenWithItemsFromDesktopFiles(const QString &filePath);
};
Q_DECLARE_METATYPE(OpenWith::OpenWithItem);

namespace Ui {
class QVOpenWithDialog;
}

class QVOpenWithDialog : public QDialog
{
    Q_OBJECT

public:
    struct Category {
        QString name;
        QString readableName;
        QString iconName;
    };
    explicit QVOpenWithDialog(QWidget *parent = nullptr);

    void populateTreeView();

    void triggeredOpen();

    ~QVOpenWithDialog();

signals:
    void selected(const QString exec, const QStringList args);
private:
    Ui::QVOpenWithDialog *ui;

    QStandardItemModel *model;

    const QList<Category> categories = {
        {"Development", tr("Development"), "applications-development"},
        {"Education", tr("Education"), "applications-education"},
        {"Game", tr("Games"), "applications-games"},
        {"Graphics", tr("Graphics"), "applications-graphics"},
        {"Network", tr("Internet"), "applications-internet"},
        {"AudioVideo", tr("Multimedia"), "applications-multimedia"},
        {"Office", tr("Office"), "applications-office"},
        {"Science", tr("Science"), "applications-science"},
        {"Settings", tr("Settings"), "preferences-system"},
        {"System", tr("System"), "applications-system"},
        {"Utility", tr("Utilities"), "applications-utilities"},
        {"", tr("Other"), "applications-other"}
    };
};

#endif // OPENWITH_H
