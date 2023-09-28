#ifndef QVAPPLICATION_H
#define QVAPPLICATION_H

#include "mainwindow.h"
#include "settingsmanager.h"
#include "shortcutmanager.h"
#include "actionmanager.h"
#include "updatechecker.h"
#include "qvoptionsdialog.h"
#include "qvaboutdialog.h"
#include "qvwelcomedialog.h"

#include <QApplication>
#include <QRegularExpression>

#if defined(qvApp)
#undef qvApp
#endif

#define qvApp (qobject_cast<QVApplication *>(QCoreApplication::instance()))	// global qvapplication object

class QVApplication : public QApplication
{
    Q_OBJECT

public:
    struct ClosedWindowData
    {
        QJsonObject sessionState;
        qint64 lastActivatedTimestamp;
    };

    explicit QVApplication(int &argc, char **argv);
    ~QVApplication() override;

    bool event(QEvent *event) override;

    static void openFile(MainWindow *window, const QString &file, bool resize = true);

    static void openFile(const QString &file, bool resize = true);

    static void pickFile(MainWindow *parent = nullptr);

    static void pickUrl(MainWindow *parnet = nullptr);

    static MainWindow *newWindow(const QJsonObject &windowSessionState = {});

    MainWindow *getMainWindow(bool shouldBeEmpty);

    void checkedUpdates();

    void recentsMenuUpdated();

    void addToActiveWindows(MainWindow *window);

    void deleteFromActiveWindows(MainWindow *window);

    bool foundLoadedImage() const;

    void openOptionsDialog(QWidget *parent = nullptr);

    void openWelcomeDialog(QWidget *parent = nullptr);

    void openAboutDialog(QWidget *parent = nullptr);

    void hideIncompatibleActions();

    void defineFilterLists();

    QMenuBar *getMenuBar() const {  return menuBar; }

    const QStringList &getFilterList() const { return filterList; }

    const QStringList &getNameFilterList() const { return nameFilterList; }

    const QStringList &getFileExtensionList() const { return fileExtensionList; }

    const QStringList &getMimeTypeNameList() const { return mimeTypeNameList; }

    const SettingsManager &getSettingsManager() const { return settingsManager; }
    SettingsManager &getSettingsManager() { return settingsManager; }

    ShortcutManager &getShortcutManager() { return shortcutManager; }

    ActionManager &getActionManager() { return actionManager; }

    UpdateChecker &getUpdateChecker() { return updateChecker; }

    bool getShowSubmenuIcons() const { return showSubmenuIcons; }

    static bool supportsSessionPersistence();

    static bool tryRestoreLastSession();

    bool getIsApplicationQuitting() const;

    bool isSessionStateEnabled() const;

    void setUserDeclinedSessionStateSave(const bool value);

    bool isSessionStateSaveRequested() const;

    void addClosedWindowSessionState(const QJsonObject &state, const qint64 lastActivatedTimestamp);

protected slots:
    void onCommitDataRequest(QSessionManager &manager);

    void onAboutToQuit();

private:
    QSet<MainWindow*> activeWindows;

    QMenu *dockMenu;

    QMenuBar *menuBar;

    QStringList filterList;
    QStringList nameFilterList;
    QStringList fileExtensionList;
    QStringList mimeTypeNameList;

    // This order is very important
    SettingsManager settingsManager; 
    ActionManager actionManager;
    ShortcutManager shortcutManager;

    QPointer<QVOptionsDialog> optionsDialog;
    QPointer<QVWelcomeDialog> welcomeDialog;
    QPointer<QVAboutDialog> aboutDialog;

    bool showSubmenuIcons;

    UpdateChecker updateChecker;

    bool isApplicationQuitting {false};
    bool userDeclinedSessionStateSave {false};
    QList<ClosedWindowData> closedWindowData;
};

#endif // QVAPPLICATION_H
