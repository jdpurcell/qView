#include "qvapplication.h"
#include "qvoptionsdialog.h"
#include "qvcocoafunctions.h"
#include "simplefonticonengine.h"
#include "updatechecker.h"

#include <QFileOpenEvent>
#include <QSettings>
#include <QTimer>
#include <QFileDialog>
#include <QMessageBox>
#include <QFontDatabase>
#include <QStyleHints>

QVApplication::QVApplication(int &argc, char **argv) : QApplication(argc, argv)
{
#if defined Q_OS_UNIX && !defined Q_OS_MACOS
    setDesktopFileName("com.interversehq.qView");

    QIcon appIcon;
    appIcon.addFile(":/icons/qView-16.png");
    appIcon.addFile(":/icons/qView-32.png");
    appIcon.addFile(":/icons/qView-64.png");
    appIcon.addFile(":/icons/qView-128.png");
    appIcon.addFile(":/icons/qView-256.png");
    QApplication::setWindowIcon(appIcon);

    // Add fallback fromTheme icon search
    QIcon::setFallbackSearchPaths(QIcon::fallbackSearchPaths() << "/usr/share/pixmaps");
#endif

    // Connections
    connect(this, &QGuiApplication::commitDataRequest, this, &QVApplication::onCommitDataRequest, Qt::DirectConnection);
    connect(this, &QCoreApplication::aboutToQuit, this, &QVApplication::onAboutToQuit);
    connect(&settingsManager, &SettingsManager::settingsUpdated, this, &QVApplication::settingsUpdated);
    connect(&actionManager, &ActionManager::recentsMenuUpdated, this, &QVApplication::recentsMenuUpdated);
    connect(&updateChecker, &UpdateChecker::checkedUpdates, this, &QVApplication::checkedUpdates);

    settingsUpdated();

    // Check for updates
    // TODO: move this to after first window show event
    if (getSettingsManager().getBoolean("updatenotifications"))
        updateChecker.check();

    showMainMenuIcons = getSettingsManager().getBoolean("mainmenuicons");
    showContextMenuIcons = getSettingsManager().getBoolean("contextmenuicons");
    showSubmenuIcons = getSettingsManager().getBoolean("submenuicons");
#ifdef Q_OS_WIN
    // Workaround for ugly menu shadows in windows11 style (QTBUG-133116)
    useCustomMenuShadow =
        (QT_VERSION < QT_VERSION_CHECK(6, 11, 0) || QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows11) &&
        style()->objectName().compare("windows11", Qt::CaseInsensitive) == 0;
#endif

    // Ask Qt to show menu icons - the action clone logic decides whether to actually set icons
    setAttribute(Qt::AA_DontShowIconsInMenus, false);

    // Setup macOS dock menu
    dockMenu = new QMenu();
    connect(dockMenu, &QMenu::triggered, this, [](QAction *triggeredAction){
        ActionManager::actionTriggered(triggeredAction);
    });

    actionManager.loadRecentsList();

#ifdef Q_OS_MACOS
    actionManager.addCloneOfAction(dockMenu, "newwindow");
    actionManager.addCloneOfAction(dockMenu, "open");
    dockMenu->setAsDockMenu();
#endif

    // Build menu bar
    menuBar = actionManager.buildMenuBar();
    connect(menuBar, &QMenuBar::triggered, this, [](QAction *triggeredAction){
        ActionManager::actionTriggered(triggeredAction);
    });

    // Set mac-specific application settings
#ifdef COCOA_LOADED
    QVCocoaFunctions::setUserDefaults();
#endif

    hideIncompatibleActions();
}

QVApplication::~QVApplication()
{
    dockMenu->deleteLater();
    menuBar->deleteLater();
}

bool QVApplication::event(QEvent *event)
{
    if (event->type() == QEvent::FileOpen)
    {
        auto *openEvent = static_cast<QFileOpenEvent *>(event);
        bool reuseWindow = getSettingsManager().getBoolean("reusewindow");
        openFile(getMainWindow(!reuseWindow), openEvent->file());
    }
    else if (event->type() == QEvent::ApplicationStateChange)
    {
        auto *stateEvent = static_cast<QApplicationStateChangeEvent*>(event);
        if (stateEvent->applicationState() == Qt::ApplicationActive)
            settingsManager.loadSettings();
    }
    return QApplication::event(event);
}

void QVApplication::openFile(MainWindow *window, const QString &file, bool resize)
{
    window->setJustLaunchedWithImage(resize);
    window->openFile(file);
}

void QVApplication::openFile(const QString &file, bool resize)
{
    auto *window = qvApp->getMainWindow(true);

    QVApplication::openFile(window, file, resize);
}

void QVApplication::pickFile(MainWindow *parent)
{
    QSettings settings;
    settings.beginGroup("recents");

    auto *fileDialog = new QFileDialog(parent, tr("Open..."));
    fileDialog->setDirectory(settings.value("lastFileDialogDir", QDir::homePath()).toString());
    fileDialog->setFileMode(QFileDialog::ExistingFiles);
    fileDialog->setNameFilters(qvApp->getNameFilterList());
    if (parent)
        fileDialog->setWindowModality(Qt::WindowModal);

    connect(fileDialog, &QFileDialog::filesSelected, fileDialog, [parent](const QStringList &selected){
        bool isFirstLoop = true;
        for (const auto &file : selected)
        {
            if (isFirstLoop && parent)
                parent->openFile(file);
            else
                QVApplication::openFile(file);

            isFirstLoop = false;
        }

        // Set lastFileDialogDir
        QSettings settings;
        settings.beginGroup("recents");
        settings.setValue("lastFileDialogDir", QFileInfo(selected.constFirst()).path());
    });
    fileDialog->show();
}

MainWindow *QVApplication::newWindow(const QJsonObject &windowSessionState)
{
    auto *w = new MainWindow(nullptr, windowSessionState);
    w->show();
    w->raise();

    return w;
}

MainWindow *QVApplication::getMainWindow(bool shouldBeEmpty)
{
    MainWindow *foundWindow = nullptr;

    for (MainWindow *window : std::as_const(activeWindows))
    {
        // If an empty window is requested, check this flag because it gets set right
        // after a load is requested, so it will be set if an image is already loaded
        // or if a load is currently in progress
        if (shouldBeEmpty && window->getCurrentFileDetails().isLoadRequested)
            continue;

        if (foundWindow && foundWindow->getLastActivatedTimestamp() >= window->getLastActivatedTimestamp())
            continue;

        foundWindow = window;
    }

    return foundWindow ? foundWindow : newWindow();
}

void QVApplication::checkedUpdates()
{
    const UpdateChecker::CheckResult checkResult = updateChecker.getCheckResult();

    QWidget *dialogParent = aboutDialog ? aboutDialog : nullptr;

    if (checkResult.wasSuccessful && checkResult.isConsideredUpdate())
    {
        updateChecker.openDialog(dialogParent, !aboutDialog);
    }
    else if (aboutDialog)
    {
        if (!checkResult.wasSuccessful)
            QMessageBox::critical(dialogParent, tr("Error"), tr("Error checking for updates:\n%1").arg(checkResult.errorMessage));
        else
            QMessageBox::information(dialogParent, tr("No Updates"), tr("You already have the latest version."));
    }

    if (aboutDialog)
        aboutDialog->updateCheckForUpdatesButtonState();
}

void QVApplication::recentsMenuUpdated()
{
#ifdef COCOA_LOADED
    QStringList recentsPathList;
    for (const auto &recent : actionManager.getRecentsList())
    {
        recentsPathList << recent.filePath;
    }
    QVCocoaFunctions::setDockRecents(recentsPathList);
#endif
}

void QVApplication::addToActiveWindows(MainWindow *window)
{
    if (!window)
        return;

    activeWindows.insert(window);
}

void QVApplication::deleteFromActiveWindows(MainWindow *window)
{
    if (!window)
        return;

    activeWindows.remove(window);
}

bool QVApplication::foundLoadedImage() const
{
    for (const MainWindow *window : activeWindows)
    {
        if (window->getIsPixmapLoaded())
            return true;
    }
    return false;
}

bool QVApplication::foundOnTopWindow() const
{
    for (const MainWindow *window : activeWindows)
    {
        if (window->getWindowOnTop())
            return true;
    }
    return false;
}

void QVApplication::openOptionsDialog(QWidget *parent)
{
#ifdef Q_OS_MACOS
    // On macOS, the dialog should not be dependent on any window
    parent = nullptr;
#endif

    if (optionsDialog)
    {
        optionsDialog->raise();
        optionsDialog->activateWindow();
        return;
    }

    optionsDialog = new QVOptionsDialog(parent);
    optionsDialog->show();
}

void QVApplication::openWelcomeDialog(QWidget *parent)
{
#ifdef Q_OS_MACOS
    // On macOS, the dialog should not be dependent on any window
    parent = nullptr;
#endif

    if (welcomeDialog)
    {
        welcomeDialog->raise();
        welcomeDialog->activateWindow();
        return;
    }

    welcomeDialog = new QVWelcomeDialog(parent);
    welcomeDialog->show();
}

void QVApplication::openAboutDialog(QWidget *parent)
{
#ifdef Q_OS_MACOS
    // On macOS, the dialog should not be dependent on any window
    parent = nullptr;
#endif

    if (aboutDialog)
    {
        aboutDialog->raise();
        aboutDialog->activateWindow();
        return;
    }

    aboutDialog = new QVAboutDialog(parent);
    aboutDialog->show();
}

void QVApplication::hideIncompatibleActions()
{
}

void QVApplication::settingsUpdated()
{
    auto &settingsManager = getSettingsManager();

    QString disabledFileExtensionsStr = settingsManager.getString("disabledfileextensions");
    disabledFileExtensions = Qv::listToSet(!disabledFileExtensionsStr.isEmpty() ? disabledFileExtensionsStr.split(';') : QStringList());

#ifdef Q_OS_MACOS
    setQuitOnLastWindowClosed(settingsManager.getBoolean("quitonlastwindow"));
#endif

    defineFilterLists();
}

void QVApplication::defineFilterLists()
{
    allFileExtensionSet.clear();
    fileExtensionSet.clear();
    mimeTypeNameSet.clear();
    nameFilterList.clear();

    const auto addExtension = [&](const QString &extension) {
        if (allFileExtensionSet.contains(extension))
            return;
        allFileExtensionSet << extension;
        if (disabledFileExtensions.contains(extension))
            return;
        fileExtensionSet << extension;
    };

    // Build extension list
    const auto &byteArrayFormats = QImageReader::supportedImageFormats();
    for (const auto &byteArray : byteArrayFormats)
    {
        const auto fileExtension = "." + QString::fromUtf8(byteArray);

        // Qt 5.15 seems to have added pdf support for QImageReader but it is super broken in qView
        if (fileExtension == ".pdf")
            continue;

        addExtension(fileExtension);

        // Register additional file extensions that decoders support but don't advertise
        if (fileExtension == ".jpg")
        {
            addExtension(".jpe");
            addExtension(".jfi");
            addExtension(".jfif");
        }
        else if (fileExtension == ".heic")
        {
            addExtension(".heics");
        }
        else if (fileExtension == ".heif")
        {
            addExtension(".heifs");
            addExtension(".hif");
        }
        else if (fileExtension == ".j2k")
        {
            addExtension(".j2c");
        }
    }

    // Build mime type list
    const auto &byteArrayMimeTypes = QImageReader::supportedMimeTypes();
    for (const auto &byteArray : byteArrayMimeTypes)
    {
        const QString mimeType = QString::fromUtf8(byteArray);

        // Qt 5.15 seems to have added pdf support for QImageReader but it is super broken in qView
        if (mimeType == "application/pdf")
            continue;

        mimeTypeNameSet << mimeType;
    }

    // Build name filter list for file dialogs
    const auto extensions = Qv::setToSortedList(fileExtensionSet);
    auto filterString = tr("Supported Images") + " (";
    for (const auto &extension : extensions)
    {
        filterString += "*" + extension + " ";
    }
    filterString.chop(1);
    filterString += ")";
    nameFilterList << filterString;
    nameFilterList << tr("All Files") + " (*)";
}

void QVApplication::ensureFontLoaded(const QString &path)
{
    static QSet<QString> loadedFontPaths;

    if (loadedFontPaths.contains(path))
        return;

    QFontDatabase::addApplicationFont(path);
    loadedFontPaths.insert(path);
}

QIcon QVApplication::iconFromFont(const Qv::MaterialIcon iconName)
{
    static std::optional<QFont> materialIconFont;
    if (!materialIconFont.has_value())
    {
        ensureFontLoaded(":/fonts/MaterialIconsOutlined-Regular.otf");
        materialIconFont = QFont("Material Icons Outlined");
        materialIconFont->setStyleStrategy(QFont::NoFontMerging);
    }
    return QIcon(new SimpleFontIconEngine(QChar(static_cast<quint16>(iconName)), materialIconFont.value()));
}

qreal QVApplication::keyboardAutoRepeatInterval()
{
    return 1.0 / qGuiApp->styleHints()->keyboardAutoRepeatRateF();
}

bool QVApplication::isMouseEventSynthesized(const QMouseEvent *event)
{
    return event->deviceType() != QInputDevice::DeviceType::Mouse;
}

bool QVApplication::supportsSessionPersistence()
{
#if defined(Q_OS_MACOS)
    return true;
#else
    return false;
#endif
}

bool QVApplication::tryRestoreLastSession()
{
    if (!supportsSessionPersistence())
        return false;

    QSettings settings;

    if (!settings.value("options/persistsession").toBool())
        return false;

    const QJsonObject sessionState = settings.value("sessionstate").toJsonObject();

    if (sessionState.isEmpty() || sessionState["version"].toInt() != Qv::SessionStateVersion)
        return false;

    const QJsonArray windowArray = sessionState["windows"].toArray();
    for (const QJsonValue &item : windowArray)
    {
        QVApplication::newWindow(item.toObject());
    }

    settings.remove("sessionstate");

    return true;
}

bool QVApplication::getIsApplicationQuitting() const
{
    return isApplicationQuitting;
}

bool QVApplication::isSessionStateEnabled() const
{
    return supportsSessionPersistence() && getSettingsManager().getBoolean("persistsession");
}

void QVApplication::setUserDeclinedSessionStateSave(const bool value)
{
    userDeclinedSessionStateSave = value;
}

bool QVApplication::isSessionStateSaveRequested() const
{
    return getIsApplicationQuitting() && isSessionStateEnabled() && !userDeclinedSessionStateSave;
}

void QVApplication::addClosedWindowSessionState(const QJsonObject &state, const qint64 lastActivatedTimestamp)
{
    closedWindowData.append({state, lastActivatedTimestamp});
}

void QVApplication::onCommitDataRequest(QSessionManager &manager)
{
    Q_UNUSED(manager)

    isApplicationQuitting = true;
}

void QVApplication::onAboutToQuit()
{
    isApplicationQuitting = true;

    if (isSessionStateSaveRequested())
    {
        QSettings settings;
        if (!closedWindowData.isEmpty())
        {
            QJsonObject state;

            state["version"] = Qv::SessionStateVersion;

            std::sort(
                closedWindowData.begin(), closedWindowData.end(),
                [](const ClosedWindowData& a, const ClosedWindowData& b) {
                    return a.lastActivatedTimestamp < b.lastActivatedTimestamp;
                });
            QJsonArray windows;
            for (const ClosedWindowData& item : std::as_const(closedWindowData))
                windows.append(item.sessionState);
            state["windows"] = windows;

            settings.setValue("sessionstate", state);
        }
        else
        {
            settings.remove("sessionstate");
        }
    }

    // Delay destroying application until thread pool threads have finished. If preloader
    // threads are still running, they require an application instance to construct a
    // QPixmap (even a null one) without crashing.
    QThreadPool::globalInstance()->waitForDone();
}
