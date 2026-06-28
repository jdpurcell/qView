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

    enum class SessionSaveDecision
    {
        Yes,
        No,
        Cancel
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

    bool foundOnTopWindow() const;

    void openOptionsDialog(QWidget *parent = nullptr);

    void openWelcomeDialog(QWidget *parent = nullptr);

    void openAboutDialog(QWidget *parent = nullptr);

    void hideIncompatibleActions();

    void settingsUpdated();

    void defineFilterLists();

    QMenuBar *getMenuBar() const {  return menuBar; }

    const QSet<QString> &getDisabledFileExtensions() const { return disabledFileExtensions; }

    const QSet<QString> &getAllFileExtensionList() const { return allFileExtensionSet; }

    const QSet<QString> &getFileExtensionSet() const { return fileExtensionSet; }

    const QSet<QString> &getMimeTypeNameSet() const { return mimeTypeNameSet; }

    const QStringList &getNameFilterList() const { return nameFilterList; }

    const SettingsManager &getSettingsManager() const { return settingsManager; }
    SettingsManager &getSettingsManager() { return settingsManager; }

    ShortcutManager &getShortcutManager() { return shortcutManager; }

    ActionManager &getActionManager() { return actionManager; }

    UpdateChecker &getUpdateChecker() { return updateChecker; }

    bool getShowMainMenuIcons() const { return showMainMenuIcons; }

    bool getShowContextMenuIcons() const { return showContextMenuIcons; }

    bool getShowSubmenuIcons() const { return showSubmenuIcons; }

    bool getUseCustomMenuShadow() const { return useCustomMenuShadow; }

    static void ensureFontLoaded(const QString &path);

    static QIcon iconFromFont(const Qv::MaterialIcon iconName);

    static qreal keyboardAutoRepeatInterval();

    static bool isMouseEventSynthesized(const QMouseEvent *event);

    static bool supportsSessionPersistence();

    static bool tryRestoreLastSession();

    void onSystemInitiatedQuit();

    bool getIsApplicationQuitting() const { return isApplicationQuitting; }

    bool getIsSessionStateSaveRequested() const { return isSessionStateSaveRequested; }

    void addClosedWindowSessionState(const QJsonObject &state, const qint64 lastActivatedTimestamp);

signals:
    void windowOnTopChanged();

protected:
    SessionSaveDecision getSessionSaveDecision() const;

protected slots:
    void onCommitDataRequest(QSessionManager &manager);

    void onAboutToQuit();

private:
    std::atomic<bool> isApplicationQuitting {false};
    std::atomic<bool> isQuitSystemInitiated {false};

    QSet<MainWindow*> activeWindows;

    QMenu *dockMenu;

    QMenuBar *menuBar;

    QSet<QString> disabledFileExtensions;

    QSet<QString> allFileExtensionSet;
    QSet<QString> fileExtensionSet;
    QSet<QString> mimeTypeNameSet;
    QStringList nameFilterList;

    // This order is very important
    SettingsManager settingsManager;
    ActionManager actionManager;
    ShortcutManager shortcutManager;

    QPointer<QVOptionsDialog> optionsDialog;
    QPointer<QVWelcomeDialog> welcomeDialog;
    QPointer<QVAboutDialog> aboutDialog;

    bool showMainMenuIcons {true};
    bool showContextMenuIcons {true};
    bool showSubmenuIcons {true};
    bool useCustomMenuShadow {false};

    UpdateChecker updateChecker;

    bool isSessionStateSaveRequested {false};
    QList<ClosedWindowData> closedWindowData;
};

#endif // QVAPPLICATION_H
