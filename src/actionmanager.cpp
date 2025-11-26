#include "actionmanager.h"
#include "qvapplication.h"
#include "qvcocoafunctions.h"
#include "openwith.h"
#include "qvmenu.h"

#include <QSettings>
#include <QActionGroup>
#include <QMessageBox>
#include <QPushButton>
#include <QMimeDatabase>
#include <QFileIconProvider>
#include <QKeyEvent>

ActionManager::ActionManager(QObject *parent) : QObject(parent)
{
    // Connect to settings signal
    connect(&qvApp->getSettingsManager(), &SettingsManager::settingsUpdated, this, &ActionManager::settingsUpdated);
    settingsUpdated();

    initializeActionLibrary();

    recentsSaveTimer = new QTimer(this);
    recentsSaveTimer->setSingleShot(true);
    recentsSaveTimer->setInterval(500);
    connect(recentsSaveTimer, &QTimer::timeout, this, &ActionManager::saveRecentsList);

    loadRecentsList();

#ifdef COCOA_LOADED
    windowMenu = new QMenu(tr("Window"));
    QVCocoaFunctions::setWindowMenu(windowMenu);
#endif
}

ActionManager::~ActionManager()
{
    qDeleteAll(actionLibrary);
}

void ActionManager::settingsUpdated()
{
    isSaveRecentsEnabled = qvApp->getSettingsManager().getBoolean("saverecents");

    auto const recentsMenus = menuCloneLibrary.values("recents");
    for (const auto &recentsMenu : recentsMenus)
    {
        recentsMenu->menuAction()->setVisible(isSaveRecentsEnabled);
    }
    if (!isSaveRecentsEnabled)
        clearRecentsList();
}

QAction *ActionManager::addCloneOfAction(QWidget *parent, const QString &key)
{
    if (auto action = getAction(key))
    {
        const bool isContextMenu = parent->property("isContextMenu").toBool();
        auto newAction = new QAction(parent);
        if (isContextMenu ? qvApp->getShowContextMenuIcons() : qvApp->getShowMainMenuIcons())
            newAction->setIcon(action->icon());
        newAction->setData(action->data());
        newAction->setText(action->text());
        if (!isContextMenu)
            newAction->setMenuRole(action->menuRole());
        newAction->setCheckable(action->isCheckable());
        newAction->setEnabled(action->isEnabled());
        newAction->setShortcuts(action->shortcuts());
        newAction->setVisible(action->isVisible());
        actionCloneLibrary.insert(key, newAction);
        parent->addAction(newAction);
        return newAction;
    }

    return nullptr;
}

QAction *ActionManager::getAction(const QString &key) const
{
    if (auto action = actionLibrary.value(key))
        return action;

    return nullptr;
}

bool ActionManager::wouldTriggerAction(const QKeyEvent *event, const QString &key) const
{
    return wouldTriggerAction(event, getAction(key)->shortcuts());
}

void ActionManager::setActionShortcuts(const QString &key, const QList<QKeySequence> &shortcuts) const
{
    const auto &actions = getAllInstancesOfAction(key);
    for (const auto &action : actions)
    {
        action->setShortcuts(shortcuts);
    }
}

QList<QAction*> ActionManager::getAllInstancesOfAction(const QString &key) const
{
    QList<QAction*> listOfActions = getAllClonesOfAction(key);

    if (auto mainAction = getAction(key))
            listOfActions.append(mainAction);

    return listOfActions;
}

QList<QAction*> ActionManager::getAllClonesOfAction(const QString &key) const
{
    return actionCloneLibrary.values(key);
}

QList<QAction*> ActionManager::getAllClonesOfAction(const QString &key, QWidget *parent) const
{
    QList<QAction*> listOfChildActions;
    const auto &actions = getAllClonesOfAction(key);
    for (const auto &action : actions)
    {
        if (hasAncestor(action, parent))
        {
            listOfChildActions.append(action);
        }
    }
    return listOfChildActions;
}

QList<QMenu*> ActionManager::getAllClonesOfMenu(const QString &key) const
{
    return menuCloneLibrary.values(key);
}

QList<QMenu*> ActionManager::getAllClonesOfMenu(const QString &key, QWidget *parent) const
{
    QList<QMenu*> listOfChildMenus;
    const auto &menus = getAllClonesOfMenu(key);
    for (const auto &menu : menus)
    {
        if (hasAncestor(menu->menuAction(), parent))
        {
            listOfChildMenus.append(menu);
        }
    }
    return listOfChildMenus;
}

void ActionManager::untrackClonedActions(const QList<QAction*> &actions)
{
    for (const auto &action : actions)
    {
        if (auto menu = action->menu())
        {
            QString key = action->data().toString();
            if (key.length() != 0)
                menuCloneLibrary.remove(key, menu);
        }
        else
        {
            QString key = action->data().toStringList().first();
            actionCloneLibrary.remove(key, action);
        }
    }
}

void ActionManager::untrackClonedActions(const QMenu *menu)
{
    untrackClonedActions(getAllNestedActions(menu->actions()));
}

void ActionManager::untrackClonedActions(const QMenuBar *menuBar)
{
    untrackClonedActions(getAllNestedActions(menuBar->actions()));
}

void ActionManager::hideAllInstancesOfAction(const QString &key)
{
    auto actions = getAllInstancesOfAction(key);
    for (auto &action : actions)
    {
        action->setVisible(false);
    }
}

QMenuBar *ActionManager::buildMenuBar(QWidget *parent)
{
    // Global menubar
    auto *menuBar = new QMenuBar(parent);

    // Beginning of file menu
    auto *fileMenu = new QVMenu(tr("&File"), menuBar);

#ifdef Q_OS_MACOS
    addCloneOfAction(fileMenu, "newwindow");
#endif
    addCloneOfAction(fileMenu, "open");
    addCloneOfAction(fileMenu, "openurl");
    fileMenu->addMenu(buildRecentsMenu(fileMenu));
    addCloneOfAction(fileMenu, "reloadfile");
    fileMenu->addSeparator();
#ifdef Q_OS_MACOS
    fileMenu->addSeparator();
    addCloneOfAction(fileMenu, "closewindow");
    addCloneOfAction(fileMenu, "closeallwindows");
#endif
#ifdef COCOA_LOADED
    QVCocoaFunctions::setAlternate(fileMenu, fileMenu->actions().length()-1);
#endif
    fileMenu->addSeparator();
    fileMenu->addMenu(buildOpenWithMenu(fileMenu));
    addCloneOfAction(fileMenu, "opencontainingfolder");
    addCloneOfAction(fileMenu, "showfileinfo");
    fileMenu->addSeparator();
    addCloneOfAction(fileMenu, "quit");

    menuBar->addMenu(fileMenu);
    // End of file menu

    // Beginning of edit menu
    auto *editMenu = new QVMenu(tr("&Edit"), menuBar);

    addCloneOfAction(editMenu, "undo");
    editMenu->addSeparator();
    addCloneOfAction(editMenu, "copy");
    addCloneOfAction(editMenu, "paste");
    addCloneOfAction(editMenu, "rename");
    editMenu->addSeparator();
    addCloneOfAction(editMenu, "delete");
    addCloneOfAction(editMenu, "deletepermanent");
#ifdef COCOA_LOADED
    QVCocoaFunctions::setAlternate(editMenu, editMenu->actions().length()-1);
#endif

    menuBar->addMenu(editMenu);
    // End of edit menu

    // Beginning of view menu
    menuBar->addMenu(buildViewMenu(menuBar));
    // End of view menu

    // Beginning of go menu
    auto *goMenu = new QVMenu(tr("&Go"), menuBar);

    addCloneOfAction(goMenu, "firstfile");
    addCloneOfAction(goMenu, "previousfile");
    addCloneOfAction(goMenu, "nextfile");
    addCloneOfAction(goMenu, "lastfile");
    addCloneOfAction(goMenu, "randomfile");
    addCloneOfAction(goMenu, "previousrandomfile");

    menuBar->addMenu(goMenu);
    // End of go menu

    // Beginning of tools menu
    menuBar->addMenu(buildToolsMenu(menuBar));
    // End of tools menu

    // Beginning of window menu
#ifdef COCOA_LOADED
    menuBar->addMenu(windowMenu);
#endif
    // End of window menu

    // Beginning of help menu
    menuBar->addMenu(buildHelpMenu(menuBar));
    // End of help menu

    return menuBar;
}

QMenu *ActionManager::buildViewMenu(QWidget *parent)
{
    const bool isContextMenu = parent->property("isContextMenu").toBool();
    auto *viewMenu = new QVMenu(tr("&View"), parent);
    viewMenu->menuAction()->setData("view");
    if (isContextMenu)
        viewMenu->setProperty("isContextMenu", true);
    if (isContextMenu && qvApp->getShowContextMenuIcons())
        viewMenu->setIcon(qvApp->iconFromFont(Qv::MaterialIcon::Visibility));

    addCloneOfAction(viewMenu, "zoomin");
    addCloneOfAction(viewMenu, "zoomout");
    addCloneOfAction(viewMenu, "zoomcustom");
    addCloneOfAction(viewMenu, "originalsize");
    addCloneOfAction(viewMenu, "zoomtofit");
    addCloneOfAction(viewMenu, "fillwindow");
    addCloneOfAction(viewMenu, "navresetszoom");
    viewMenu->addSeparator();
    addCloneOfAction(viewMenu, "rotateright");
    addCloneOfAction(viewMenu, "rotateleft");
    addCloneOfAction(viewMenu, "mirror");
    addCloneOfAction(viewMenu, "flip");
    addCloneOfAction(viewMenu, "resettransformation");
    viewMenu->addSeparator();
    addCloneOfAction(viewMenu, "matchimagesize");
    addCloneOfAction(viewMenu, "windowontop");
    addCloneOfAction(viewMenu, "toggletitlebar");
    addCloneOfAction(viewMenu, "fullscreen");

    menuCloneLibrary.insert(viewMenu->menuAction()->data().toString(), viewMenu);
    return viewMenu;
}

QMenu *ActionManager::buildToolsMenu(QWidget *parent)
{
    const bool isContextMenu = parent->property("isContextMenu").toBool();
    auto *toolsMenu = new QVMenu(tr("&Tools"), parent);
    toolsMenu->menuAction()->setData("tools");
    if (isContextMenu)
        toolsMenu->setProperty("isContextMenu", true);
    if (isContextMenu && qvApp->getShowContextMenuIcons())
        toolsMenu->setIcon(qvApp->iconFromFont(Qv::MaterialIcon::Build));

    addCloneOfAction(toolsMenu, "saveframeas");
    addCloneOfAction(toolsMenu, "pause");
    addCloneOfAction(toolsMenu, "nextframe");
    addCloneOfAction(toolsMenu, "previousframe");
    toolsMenu->addSeparator();
    addCloneOfAction(toolsMenu, "decreasespeed");
    addCloneOfAction(toolsMenu, "resetspeed");
    addCloneOfAction(toolsMenu, "increasespeed");
    toolsMenu->addSeparator();
    addCloneOfAction(toolsMenu, "slideshow");
    addCloneOfAction(toolsMenu, "options");

    menuCloneLibrary.insert(toolsMenu->menuAction()->data().toString(), toolsMenu);
    return toolsMenu;
}

QMenu *ActionManager::buildHelpMenu(QWidget *parent)
{
    const bool isContextMenu = parent->property("isContextMenu").toBool();
    auto *helpMenu = new QVMenu(tr("&Help"), parent);
    helpMenu->menuAction()->setData("help");
    if (isContextMenu)
        helpMenu->setProperty("isContextMenu", true);
    if (isContextMenu && qvApp->getShowContextMenuIcons())
        helpMenu->setIcon(qvApp->iconFromFont(Qv::MaterialIcon::HelpOutline));

    addCloneOfAction(helpMenu, "about");
    addCloneOfAction(helpMenu, "welcome");

    menuCloneLibrary.insert(helpMenu->menuAction()->data().toString(), helpMenu);
    return helpMenu;
}

QMenu *ActionManager::buildRecentsMenu(QWidget *parent)
{
    const bool isContextMenu = parent->property("isContextMenu").toBool();
    auto *recentsMenu = new QVMenu(tr("Open &Recent"), parent);
    recentsMenu->menuAction()->setData("recents");
    if (isContextMenu)
        recentsMenu->setProperty("isContextMenu", true);
    if (isContextMenu ? qvApp->getShowContextMenuIcons() : qvApp->getShowMainMenuIcons())
        recentsMenu->setIcon(qvApp->iconFromFont(Qv::MaterialIcon::WorkHistory));

    connect(recentsMenu, &QMenu::aboutToShow, this, [this]{
        this->loadRecentsList();
    });

    for (int i = 0; i < recentsListMaxLength; i++)
    {
        auto action = new QAction(tr("Empty"), recentsMenu);
        action->setVisible(false);
        action->setIconVisibleInMenu(true);
        action->setData("recent" + QString::number(i));

        recentsMenu->addAction(action);
        actionCloneLibrary.insert(action->data().toStringList().first(), action);
    }

    recentsMenu->addSeparator();
    addCloneOfAction(recentsMenu, "clearrecents");

    menuCloneLibrary.insert(recentsMenu->menuAction()->data().toString(), recentsMenu);
    updateRecentsMenu();
    // update settings whenever recent menu is created so it can possibly be hidden
    settingsUpdated();
    return recentsMenu;
}

void ActionManager::loadRecentsList()
{
    // Prevents weird bugs when opening the recent menu while the save timer is still running
    if (recentsSaveTimer->isActive())
        return;

    QSettings settings;
    settings.beginGroup("recents");

    QVariantList variantListRecents = settings.value("recentFiles").toList();
    recentsList = variantListToRecentsList(variantListRecents);

    auditRecentsList();
}

void ActionManager::saveRecentsList()
{
    QSettings settings;
    settings.beginGroup("recents");

    auto variantList = recentsListToVariantList(recentsList);

    settings.setValue("recentFiles", variantList);
}

void ActionManager::addFileToRecentsList(const QFileInfo &file)
{
    recentsList.prepend({file.fileName(), file.filePath()});
    auditRecentsList();
    recentsSaveTimer->start();
}

void ActionManager::auditRecentsList(const bool checkIfExists)
{
    // This function should be called whenever there is a change to recentsList,
    // and take care not to call any functions that call it.

    bool changedList = false;

    if (!isSaveRecentsEnabled && !recentsList.isEmpty())
    {
        recentsList.clear();
        changedList = true;
    }

    int i = 0;
    while (i < recentsList.length() && i < recentsListMaxLength)
    {
        const auto recent = recentsList.value(i);

        // Ensure file exists
        if (checkIfExists && !QFileInfo::exists(recent.filePath))
        {
            recentsList.removeAt(i);
            changedList = true;
            continue;
        }

        // Check for duplicates
        for (int j = i + 1; j < recentsList.length(); j++)
        {
            if (recent == recentsList.value(j))
            {
                recentsList.removeAt(j);
                changedList = true;
            }
        }

        i++;
    }

    while (recentsList.size() > recentsListMaxLength)
    {
        recentsList.removeLast();
        changedList = true;
    }

    updateRecentsMenu();
    if (changedList)
    {
        recentsSaveTimer->start();
    }
}

void ActionManager::clearRecentsList()
{
    recentsList.clear();
    saveRecentsList();
}

void ActionManager::updateRecentsMenu()
{
    for (int i = 0; i < recentsListMaxLength; i++)
    {
        const auto values = actionCloneLibrary.values("recent" + QString::number(i));
        for (const auto &action : values)
        {
            // If we are within the bounds of the recent list
            if (i < recentsList.length())
            {
                auto recent = recentsList.value(i);

                action->setVisible(true);
                action->setIconVisibleInMenu(false); // Hide icon temporarily to speed up updates in certain cases
                action->setText(recent.fileName);

                if (!qvApp->getShowSubmenuIcons())
                    continue;

#if defined Q_OS_UNIX && !defined Q_OS_MACOS
                // set icons for linux users
                QMimeDatabase mimedb;
                QMimeType type = mimedb.mimeTypeForFile(recent.filePath);
                action->setIcon(QIcon::fromTheme(type.iconName(), QIcon::fromTheme(type.genericIconName())));
#else
                // set icons for mac/windows users
                QFileInfo fileInfo(recent.filePath);
                QFileIconProvider provider;
                QIcon icon = provider.icon(fileInfo);
#ifdef Q_OS_MACOS
                // Workaround for native menu slowness
                if (!fileInfo.suffix().isEmpty())
                    icon = getCacheableIcon("filetype:" + fileInfo.suffix(), icon);
#endif
                action->setIcon(icon);
#endif
                action->setIconVisibleInMenu(true);
            }
            else
            {
                action->setVisible(false);
            }
        }
    }
    emit recentsMenuUpdated();
}

QMenu *ActionManager::buildOpenWithMenu(QWidget *parent)
{
    const bool isContextMenu = parent->property("isContextMenu").toBool();
    auto *openWithMenu = new QVMenu(tr("Open With"), parent);
    openWithMenu->menuAction()->setData("openwith");
    if (isContextMenu)
        openWithMenu->setProperty("isContextMenu", true);
    if (isContextMenu ? qvApp->getShowContextMenuIcons() : qvApp->getShowMainMenuIcons())
        openWithMenu->setIcon(qvApp->iconFromFont(Qv::MaterialIcon::Launch));
    openWithMenu->setDisabled(true);

    for (int i = 0; i < openWithMaxLength; i++)
    {
        auto action = new QAction(tr("Empty"), openWithMenu);
        action->setVisible(false);
        action->setIconVisibleInMenu(true);
        action->setData(QVariantList({"openwith" + QString::number(i), ""}));

        openWithMenu->addAction(action);
        actionCloneLibrary.insert(action->data().toStringList().first(), action);
        // Some madness that will show or hide separator if an item marked as default is in the first position
        if (i == 0)
        {
            connect(action, &QAction::changed, action, [action, openWithMenu]{
                // If this menu item is default
                if (action->data().toList().at(1).value<OpenWith::OpenWithItem>().isDefault)
                {
                    if (!openWithMenu->actions().at(1)->isSeparator())
                        openWithMenu->insertSeparator(openWithMenu->actions().at(1));
                }
                else
                {
                    if (openWithMenu->actions().at(1)->isSeparator())
                        openWithMenu->removeAction(openWithMenu->actions().at(1));
                }
            });
        }
    }

    openWithMenu->addSeparator();
    addCloneOfAction(openWithMenu, "openwithother");

    menuCloneLibrary.insert(openWithMenu->menuAction()->data().toString(), openWithMenu);
    return openWithMenu;
}

QMenu *ActionManager::buildSortMenu(QWidget *parent)
{
    const bool isContextMenu = parent->property("isContextMenu").toBool();
    auto *sortMenu = new QVMenu(tr("Sort Files By"), parent);
    sortMenu->menuAction()->setData("sortmenu");
    if (isContextMenu)
        sortMenu->setProperty("isContextMenu", true);
    if (isContextMenu ? qvApp->getShowContextMenuIcons() : qvApp->getShowMainMenuIcons())
        sortMenu->setIcon(qvApp->iconFromFont(Qv::MaterialIcon::Sort));

    auto *sortModeGroup = new QActionGroup(sortMenu);
    const auto addMode = [&](const QString &text, const Qv::SortMode mode) {
        auto *action = new QAction(text, sortMenu);
        action->setData(QStringList{"sortmode" + QString::number(static_cast<int>(mode))});
        action->setCheckable(true);
        sortModeGroup->addAction(action);
        sortMenu->addAction(action);
        actionCloneLibrary.insert(action->data().toStringList().first(), action);
    };
    addMode(tr("Name"), Qv::SortMode::Name);
    addMode(tr("Date Modified"), Qv::SortMode::DateModified);
    addMode(tr("Date Created"), Qv::SortMode::DateCreated);
    addMode(tr("Size"), Qv::SortMode::Size);
    addMode(tr("Type"), Qv::SortMode::Type);
    addMode(tr("Random"), Qv::SortMode::Random);

    sortMenu->addSeparator();

    auto *sortDirectionGroup = new QActionGroup(sortMenu);
    const auto addDirection = [&](const QString &text, const bool descending) {
        auto *action = new QAction(text, sortMenu);
        action->setData(QStringList{"sortdirection" + QString::number(static_cast<int>(descending))});
        action->setCheckable(true);
        sortDirectionGroup->addAction(action);
        sortMenu->addAction(action);
        actionCloneLibrary.insert(action->data().toStringList().first(), action);
    };
    addDirection(tr("Ascending"), false);
    addDirection(tr("Descending"), true);

    return sortMenu;
}

void ActionManager::actionTriggered(QAction *triggeredAction)
{
    auto key = triggeredAction->data().toStringList().first();

    // For some actions, do not look for a relevant window
    QStringList windowlessActions = {"newwindow", "quit", "clearrecents", "open"};
#ifdef Q_OS_MACOS
    windowlessActions << "about" << "welcome" << "options";
#endif
    for (const auto &actionName : std::as_const(windowlessActions))
    {
        if (key == actionName)
        {
            actionTriggered(triggeredAction, nullptr);
            return;
        }
    }

    // If some actions are triggered without an explicit window, we want
    // to give them a window without an image open
    bool shouldBeEmpty = false;
    if (key.startsWith("recent") || key == "openurl")
        shouldBeEmpty = true;

    if (auto *window = qvApp->getMainWindow(shouldBeEmpty))
        actionTriggered(triggeredAction, window);
}

void ActionManager::actionTriggered(QAction *triggeredAction, MainWindow *relevantWindow)
{
    // Conditions that will work with a nullptr window passed
    auto key = triggeredAction->data().toStringList().first();

    if (key == "quit") {
        if (qvApp->isSessionStateEnabled() && qvApp->foundLoadedImage()) {
            QMessageBox msgBox {relevantWindow};
            msgBox.setWindowModality(Qt::ApplicationModal);
            msgBox.setWindowTitle(tr("Remember Session?"));
            msgBox.setText(tr("Would you like to remember your opened images and re-open them at next launch?"));
            QPushButton *yesButton = msgBox.addButton(tr("&Remember"), QMessageBox::YesRole);
            QPushButton *noButton = msgBox.addButton(tr("&End Session"), QMessageBox::NoRole);
            msgBox.setDefaultButton(yesButton);
            msgBox.exec();
            qvApp->setUserDeclinedSessionStateSave(msgBox.clickedButton() == noButton);
        }
        QCoreApplication::quit();
    } else if (key == "newwindow") {
        qvApp->newWindow();
    } else if (key == "open") {
        qvApp->pickFile(relevantWindow);
    } else if (key == "open") {
        qvApp->pickFile(relevantWindow);
    } else if (key == "closeallwindows") {
        const auto topLevelWindows = QApplication::topLevelWindows();
        for (auto *window : topLevelWindows) {
            window->close();
        }
    } else if (key == "options") {
        qvApp->openOptionsDialog(relevantWindow);
    } else if (key == "about") {
        qvApp->openAboutDialog(relevantWindow);
    } else if (key == "welcome") {
        qvApp->openWelcomeDialog(relevantWindow);
    } else if (key == "clearrecents") {
        qvApp->getActionManager().clearRecentsList();
    }

    // The great filter
    if (!relevantWindow)
        return;

    // Conditions that require a valid window pointer
    if (key == "closewindow") {
        relevantWindow->close();
    } else if (key.startsWith("recent")) {
        QChar finalChar = key.at(key.length()-1);
        relevantWindow->openRecent(finalChar.digitValue());
    } else if (key == "openwithother") {
        OpenWith::showOpenWithDialog(relevantWindow);
    } else if (key.startsWith("openwith")) {
        const auto &openWithItem = triggeredAction->data().toList().at(1).value<OpenWith::OpenWithItem>();
        relevantWindow->openWith(openWithItem);
    } else if (key == "openurl") {
        relevantWindow->pickUrl();
    } else if (key == "reloadfile") {
        relevantWindow->reloadFile();
    } else if (key == "opencontainingfolder") {
        relevantWindow->openContainingFolder();
    } else if (key == "showfileinfo") {
        relevantWindow->showFileInfo();
    } else if (key == "delete") {
        relevantWindow->askDeleteFile();
    } else if (key == "deletepermanent") {
        relevantWindow->askDeleteFile(true);
    } else if (key == "undo") {
        relevantWindow->undoDelete();
    } else if (key == "copy") {
        relevantWindow->copy();
    } else if (key == "paste") {
        relevantWindow->paste();
    } else if (key == "rename") {
        relevantWindow->rename();
    } else if (key == "zoomin") {
        relevantWindow->zoomIn();
    } else if (key == "zoomout") {
        relevantWindow->zoomOut();
    } else if (key == "zoomcustom") {
        relevantWindow->zoomCustom();
    } else if (key == "originalsize") {
        relevantWindow->originalSize();
    } else if (key == "zoomtofit") {
        relevantWindow->setZoomToFit(triggeredAction->isChecked());
    } else if (key == "fillwindow") {
        relevantWindow->setFillWindow(triggeredAction->isChecked());
    } else if (key == "navresetszoom") {
        relevantWindow->setNavigationResetsZoom(triggeredAction->isChecked());
    } else if (key == "rotateright") {
        relevantWindow->rotateRight();
    } else if (key == "rotateleft") {
        relevantWindow->rotateLeft();
    } else if (key == "mirror") {
        relevantWindow->mirror();
    } else if (key == "flip") {
        relevantWindow->flip();
    } else if (key == "resettransformation") {
        relevantWindow->resetTransformation();
    } else if (key == "matchimagesize") {
        relevantWindow->setWindowSize(true, true);
    } else if (key == "windowontop") {
        relevantWindow->toggleWindowOnTop();
    } else if (key == "toggletitlebar") {
        relevantWindow->toggleTitlebarHidden();
    } else if (key == "fullscreen") {
        relevantWindow->toggleFullScreen();
    } else if (key == "firstfile") {
        relevantWindow->firstFile();
    } else if (key == "previousfile") {
        relevantWindow->previousFile();
    } else if (key == "nextfile") {
        relevantWindow->nextFile();
    } else if (key == "lastfile") {
        relevantWindow->lastFile();
    } else if (key == "randomfile") {
        relevantWindow->randomFile();
    } else if (key == "previousrandomfile") {
        relevantWindow->previousRandomFile();
    } else if (key == "saveframeas") {
        relevantWindow->saveFrameAs();
    } else if (key == "pause") {
        relevantWindow->pause();
    } else if (key == "nextframe") {
        relevantWindow->nextFrame();
    } else if (key == "previousframe") {
        relevantWindow->previousFrame();
    } else if (key == "decreasespeed") {
        relevantWindow->decreaseSpeed();
    } else if (key == "resetspeed") {
        relevantWindow->resetSpeed();
    } else if (key == "increasespeed") {
        relevantWindow->increaseSpeed();
    } else if (key == "slideshow") {
        relevantWindow->toggleSlideshow();
    } else if (key.startsWith("sortmode")) {
        relevantWindow->setSortMode(static_cast<Qv::SortMode>(key.mid(QString("sortmode").length()).toInt()));
    } else if (key.startsWith("sortdirection")) {
        relevantWindow->setSortDescending(key.endsWith("1"));
    }
}

bool ActionManager::wouldTriggerAction(const QKeyEvent *event, const QList<QKeySequence> &shortcuts)
{
    const QKeySequence targetSequence = (event->modifiers() | event->key()) & ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
    return shortcuts.contains(targetSequence);
}

void ActionManager::initializeActionLibrary()
{
    auto *quitAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Logout), tr("&Quit"));
    quitAction->setMenuRole(QAction::QuitRole);
    actionLibrary.insert("quit", quitAction);
#ifdef Q_OS_WIN
    //: The quit action is called "Exit" on windows
    quitAction->setText(tr("Exit"));
#endif

    auto *newWindowAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::WebAsset), tr("New Window"));
    actionLibrary.insert("newwindow", newWindowAction);

    auto *openAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::FileOpen), tr("&Open..."));
    actionLibrary.insert("open", openAction);

    auto *openUrlAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::CloudDownload), tr("Open &URL..."));
    actionLibrary.insert("openurl", openUrlAction);

    auto *reloadFileAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Refresh), tr("Re&load File"));
    reloadFileAction->setData({"disable"});
    actionLibrary.insert("reloadfile", reloadFileAction);

    auto *closeWindowAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Close), tr("Close Window"));
    actionLibrary.insert("closewindow", closeWindowAction);

    //: Close all windows, that is
    auto *closeAllWindowsAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::DisabledByDefault), tr("Close All"));
    actionLibrary.insert("closeallwindows", closeAllWindowsAction);

    auto *openContainingFolderAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::FolderOpen), tr("Open Containing &Folder"));
#ifdef Q_OS_WIN
    //: Open containing folder on windows
    openContainingFolderAction->setText(tr("Show in E&xplorer"));
#elif defined Q_OS_MACOS
    //: Open containing folder on macOS
    openContainingFolderAction->setText(tr("Show in &Finder"));
#endif
    openContainingFolderAction->setData({"disable"});
    actionLibrary.insert("opencontainingfolder", openContainingFolderAction);

    auto *showFileInfoAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Ballot), tr("Show File &Info"));
    showFileInfoAction->setData({"disable"});
    actionLibrary.insert("showfileinfo", showFileInfoAction);

    auto *deleteAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Delete), tr("&Move to Trash"));
#ifdef Q_OS_WIN
    deleteAction->setText(tr("&Delete"));
#endif
    deleteAction->setData({"disable"});
    actionLibrary.insert("delete", deleteAction);

    auto *deletePermanentAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::DeleteForever), tr("Delete Permanently"));
    deletePermanentAction->setData({"disable"});
    actionLibrary.insert("deletepermanent", deletePermanentAction);

    auto *undoAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::RestoreFromTrash), tr("&Restore from Trash"));
#ifdef Q_OS_WIN
    undoAction->setText(tr("&Undo Delete"));
#endif
    undoAction->setData({"undodisable"});
    actionLibrary.insert("undo", undoAction);

    auto *copyAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::ContentCopy), tr("&Copy"));
    copyAction->setData({"disable"});
    actionLibrary.insert("copy", copyAction);

    auto *pasteAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::ContentPaste), tr("&Paste"));
    actionLibrary.insert("paste", pasteAction);

    auto *renameAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::DriveFileRenameOutline), tr("R&ename..."));
    renameAction->setData({"disable"});
    actionLibrary.insert("rename", renameAction);

    auto *zoomInAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::ZoomIn), tr("Zoom &In"));
    zoomInAction->setData({"disable"});
    actionLibrary.insert("zoomin", zoomInAction);

    auto *zoomOutAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::ZoomOut), tr("Zoom &Out"));
    zoomOutAction->setData({"disable"});
    actionLibrary.insert("zoomout", zoomOutAction);

    auto *zoomCustomAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Search), tr("Set Zoom &Level..."));
    zoomCustomAction->setData({"disable"});
    actionLibrary.insert("zoomcustom", zoomCustomAction);

    auto *originalSizeAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::LooksOne), tr("Ori&ginal Size"));
    originalSizeAction->setData({"disable"});
    actionLibrary.insert("originalsize", originalSizeAction);

    auto *zoomToFitAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::FitScreen), tr("&Zoom to Fit"));
    zoomToFitAction->setData({"disable"});
    zoomToFitAction->setCheckable(true);
    actionLibrary.insert("zoomtofit", zoomToFitAction);

    auto *fillWindowAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::SettingsOverscan), tr("Fill &Window"));
    fillWindowAction->setData({"disable"});
    fillWindowAction->setCheckable(true);
    actionLibrary.insert("fillwindow", fillWindowAction);

    auto *navigationResetsZoomAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Deselect), tr("&Navigation Resets Zoom"));
    navigationResetsZoomAction->setData({"disable"});
    navigationResetsZoomAction->setCheckable(true);
    actionLibrary.insert("navresetszoom", navigationResetsZoomAction);

    auto *rotateRightAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::RotateRight), tr("Rotate &Right"));
    rotateRightAction->setData({"disable"});
    actionLibrary.insert("rotateright", rotateRightAction);

    auto *rotateLeftAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::RotateLeft), tr("Rotate &Left"));
    rotateLeftAction->setData({"disable"});
    actionLibrary.insert("rotateleft", rotateLeftAction);

    auto *mirrorAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::SwapHoriz), tr("&Mirror"));
    mirrorAction->setData({"disable"});
    actionLibrary.insert("mirror", mirrorAction);

    auto *flipAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::SwapVert), tr("&Flip"));
    flipAction->setData({"disable"});
    actionLibrary.insert("flip", flipAction);

    auto *resetTransformationAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Replay), tr("Reset &Transformation"));
    resetTransformationAction->setData({"disable"});
    actionLibrary.insert("resettransformation", resetTransformationAction);

    auto *matchImageSizeAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Wallpaper), tr("M&atch Image Size"));
    matchImageSizeAction->setData({"windowdisable"});
    actionLibrary.insert("matchimagesize", matchImageSizeAction);

    auto *windowOnTopAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::KeyboardDoubleArrowUp), tr("Window On To&p"));
    windowOnTopAction->setData({"windowdisable"});
    windowOnTopAction->setCheckable(true);
    actionLibrary.insert("windowontop", windowOnTopAction);

    auto *toggleTitlebarAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::VerticalAlignTop), tr("Hide Title&bar"));
    toggleTitlebarAction->setData({"windowdisable"});
    actionLibrary.insert("toggletitlebar", toggleTitlebarAction);

    auto *fullScreenAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Fullscreen), tr("Enter F&ull Screen"));
    fullScreenAction->setMenuRole(QAction::NoRole);
    fullScreenAction->setData({"windowdisable"});
    actionLibrary.insert("fullscreen", fullScreenAction);

    auto *firstFileAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::FirstPage), tr("&First File"));
    firstFileAction->setData({"folderdisable"});
    actionLibrary.insert("firstfile", firstFileAction);

    auto *previousFileAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::NavigateBefore), tr("Previous Fi&le"));
    previousFileAction->setData({"folderdisable"});
    actionLibrary.insert("previousfile", previousFileAction);

    auto *nextFileAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::NavigateNext), tr("&Next File"));
    nextFileAction->setData({"folderdisable"});
    actionLibrary.insert("nextfile", nextFileAction);

    auto *lastFileAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::LastPage), tr("Las&t File"));
    lastFileAction->setData({"folderdisable"});
    actionLibrary.insert("lastfile", lastFileAction);

    auto *randomFileAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Shuffle), tr("&Random File"));
    randomFileAction->setData({"folderdisable"});
    actionLibrary.insert("randomfile", randomFileAction);

    auto *previousRandomFileAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Shuffle), tr("&Previous Random File"));
    previousRandomFileAction->setData({"folderdisable"});
    actionLibrary.insert("previousrandomfile", previousRandomFileAction);

    auto *saveFrameAsAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Save), tr("Save Frame &As..."));
    saveFrameAsAction->setData({"gifdisable"});
    actionLibrary.insert("saveframeas", saveFrameAsAction);

    auto *pauseAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Pause), tr("Pa&use"));
    pauseAction->setData({"gifdisable"});
    actionLibrary.insert("pause", pauseAction);

    auto *nextFrameAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::SkipNext), tr("&Next Frame"));
    nextFrameAction->setData({"gifdisable"});
    actionLibrary.insert("nextframe", nextFrameAction);

    auto *previousFrameAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::SkipPrevious), tr("&Previous Frame"));
    previousFrameAction->setData({"gifdisable"});
    actionLibrary.insert("previousframe", previousFrameAction);

    auto *decreaseSpeedAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::FastRewind), tr("&Decrease Speed"));
    decreaseSpeedAction->setData({"gifdisable"});
    actionLibrary.insert("decreasespeed", decreaseSpeedAction);

    auto *resetSpeedAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Replay), tr("&Reset Speed"));
    resetSpeedAction->setData({"gifdisable"});
    actionLibrary.insert("resetspeed", resetSpeedAction);

    auto *increaseSpeedAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::FastForward), tr("&Increase Speed"));
    increaseSpeedAction->setData({"gifdisable"});
    actionLibrary.insert("increasespeed", increaseSpeedAction);

    auto *slideshowAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Slideshow), tr("Start S&lideshow"));
    slideshowAction->setData({"disable"});
    actionLibrary.insert("slideshow", slideshowAction);

    //: This is for the options dialog on windows
    auto *optionsAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Settings), tr("&Settings"));
#ifdef Q_OS_MACOS
    if (QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13)) {
        //: This is for the options dialog on older mac versions
        optionsAction->setText(tr("Preference&s..."));
    } else {
        optionsAction->setText(tr("Setting&s..."));
    }
#endif
    optionsAction->setMenuRole(QAction::PreferencesRole);
    actionLibrary.insert("options", optionsAction);

    auto *aboutAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Info), tr("&About"));
#ifdef Q_OS_MACOS
    //: This is for the about dialog on mac
    aboutAction->setText(tr("&About qView"));
#endif
    aboutAction->setMenuRole(QAction::AboutRole);
    actionLibrary.insert("about", aboutAction);

    auto *welcomeAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Start), tr("&Welcome"));
    actionLibrary.insert("welcome", welcomeAction);

    //: This is for clearing the recents menu
    auto *clearRecentsAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::PlaylistRemove), tr("Clear &Menu"));
    actionLibrary.insert("clearrecents", clearRecentsAction);

    //: Open with other program for unix non-mac
    auto *openWithOtherAction = new QAction(qvApp->iconFromFont(Qv::MaterialIcon::Launch), tr("Other Application..."));
#ifdef Q_OS_WIN
    //: Open with other program for windows
    openWithOtherAction->setText(tr("Choose another app"));
#elif defined Q_OS_MACOS
    //: Open with other program for macos
    openWithOtherAction->setText(tr("Other..."));
#endif
    actionLibrary.insert("openwithother", openWithOtherAction);

    // Set data values and disable actions
    const auto keys = actionLibrary.keys();
    for (const auto &key : keys)
    {
        auto *value = actionLibrary.value(key);
        auto data = value->data().toStringList();
        data.prepend(key);
        value->setData(data);

        if (data.last().contains("disable"))
            value->setEnabled(false);
    }
}

QIcon ActionManager::getCacheableIcon(const QString &cacheKey, const QIcon &icon)
{
    static QMutex mutex;
    static QCache<QString, QIcon> cache;
    QMutexLocker locker(&mutex);
    QIcon *cacheEntry = cache.take(cacheKey);
    if (cacheEntry == nullptr)
    {
        cacheEntry = new QIcon();
        // Depending on the source icon's implementation (e.g. if it's backed by a file engine), it may
        // not allow pixmap caching, so get a pixmap of each size once and copy it to a generic QIcon
        const auto iconSizes = icon.availableSizes();
        for (const auto &iconSize : iconSizes)
            cacheEntry->addPixmap(icon.pixmap(iconSize));
    }
    QIcon cacheableIcon = *cacheEntry;
    cache.insert(cacheKey, cacheEntry);
    return cacheableIcon;
}

bool ActionManager::hasAncestor(QObject *object, QObject *ancestor)
{
    while (object)
    {
        if (object == ancestor)
            return true;
        object = object->parent();
    }
    return false;
}
