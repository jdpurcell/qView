#include "qvoptionsdialog.h"
#include "ui_qvoptionsdialog.h"
#include "qvapplication.h"
#include "qvshortcutdialog.h"
#include <QColorDialog>
#include <QPalette>
#include <QScreen>
#include <QMessageBox>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QWindow>

#include <QDebug>

QVOptionsDialog::QVOptionsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::QVOptionsDialog)
{
    ui->setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint | Qt::CustomizeWindowHint));

    resize(650, 550);

    // Center fixed-height checkboxes beside taller form fields
    ui->windowLayout->setAlignment(ui->bgColorCheckbox, Qt::AlignVCenter);
    ui->imageLayout->setAlignment(ui->smoothScalingLimitCheckbox, Qt::AlignVCenter);
    ui->imageLayout->setAlignment(ui->fitZoomLimitCheckbox, Qt::AlignVCenter);
    ui->cursorLayout->setAlignment(ui->cursorAutoHideFullscreenCheckbox, Qt::AlignVCenter);

#ifdef Q_OS_MACOS
    // Workaround for QTBUG-150017
    if (QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 27))
    {
        if (QStyle *fusionStyle = QStyleFactory::create("fusion"))
        {
            fusionStyle->setParent(ui->formatsTable);
            ui->formatsTable->setStyle(fusionStyle);
            for (QWidget *child : ui->formatsTable->findChildren<QWidget *>())
                child->setStyle(fusionStyle);
        }
    }
#endif

    connect(ui->categoryList, &QListWidget::currentRowChanged, this, [this](int currentRow) { ui->stackedWidget->setCurrentIndex(currentRow); });
    connect(ui->buttonBox, &QDialogButtonBox::clicked, this, &QVOptionsDialog::buttonBoxClicked);
    connect(ui->shortcutsTable, &QTableWidget::cellDoubleClicked, this, &QVOptionsDialog::shortcutCellDoubleClicked);
    connect(ui->bgColorCheckbox, &QCheckBox::checkStateChanged, this, &QVOptionsDialog::bgColorCheckboxCheckStateChanged);
    connect(ui->mainMenuIconsCheckbox, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) { restartNotifyForCheckbox("mainmenuicons", state); });
    connect(ui->contextMenuIconsCheckbox, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) { restartNotifyForCheckbox("contextmenuicons", state); });
    connect(ui->submenuIconsCheckbox, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) { restartNotifyForCheckbox("submenuicons", state); });
    connect(ui->smoothScalingLimitCheckbox, &QCheckBox::checkStateChanged, this, &QVOptionsDialog::smoothScalingLimitCheckboxCheckStateChanged);
    connect(ui->fitZoomLimitCheckbox, &QCheckBox::checkStateChanged, this, &QVOptionsDialog::fitZoomLimitCheckboxCheckStateChanged);
    connect(ui->constrainImagePositionCheckbox, &QCheckBox::checkStateChanged, this, &QVOptionsDialog::constrainImagePositionCheckboxCheckStateChanged);
    connect(ui->cursorAutoHideFullscreenCheckbox, &QCheckBox::checkStateChanged, this, &QVOptionsDialog::cursorAutoHideFullscreenCheckboxCheckStateChanged);
    connect(ui->middleButtonModeClickRadioButton, &QRadioButton::clicked, this, &QVOptionsDialog::middleButtonModeChanged);
    connect(ui->middleButtonModeDragRadioButton, &QRadioButton::clicked, this, &QVOptionsDialog::middleButtonModeChanged);
    connect(ui->titlebarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QVOptionsDialog::titlebarComboBoxCurrentIndexChanged);
    connect(ui->smoothScalingComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QVOptionsDialog::smoothScalingComboBoxCurrentIndexChanged);
    connect(ui->langComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QVOptionsDialog::languageComboBoxCurrentIndexChanged);
    connect(ui->formatsTable, &QTableWidget::itemChanged, this, &QVOptionsDialog::formatsItemChanged);

    QSettings settings;

    customizePalette();
    populateCategories(settings.value("optionstab", 1).toInt());
    populateComboBoxes();
    populateLanguages();

    // Platform specific behaviors
#ifdef Q_OS_MACOS
    restoreGeometry(settings.value("optionsgeometry").toByteArray());

    if (QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13))
        setWindowTitle(tr("Preferences"));
#else
    setWindowModality(Qt::WindowModal);
#endif

    // Platform specific settings
#ifdef Q_OS_MACOS
    ui->menubarCheckbox->hide();
#else
    ui->reuseWindowCheckbox->hide();
    ui->darkTitlebarCheckbox->hide();
    ui->quitOnLastWindowCheckbox->hide();
#endif

    if (!QVApplication::supportsSessionPersistence())
        ui->persistSessionCheckbox->hide();

    QString ctrlKeyName = QKeySequence(Qt::CTRL).toString(QKeySequence::NativeText).replace(QRegularExpression("\\+$"), "");
    ui->altDoubleClickLabel->setText(tr("%1 + Double Click:").arg(ctrlKeyName));
    ui->altDragLabel->setText(tr("%1 + Drag:").arg(ctrlKeyName));
    ui->altMiddleClickLabel->setText(tr("%1 + Middle Click:").arg(ctrlKeyName));
    ui->altMiddleDragLabel->setText(tr("%1 + Middle Drag:").arg(ctrlKeyName));
    ui->altVerticalScrollLabel->setText(tr("%1 + Vertical Scroll:").arg(ctrlKeyName));
    ui->altHorizontalScrollLabel->setText(tr("%1 + Horizontal Scroll:").arg(ctrlKeyName));

    syncSettings(false, true);
    syncShortcuts();
    syncFormats();
    updateButtonBox();

    isInitialLoad = false;
}

QVOptionsDialog::~QVOptionsDialog()
{
    delete ui;
}

void QVOptionsDialog::done(int r)
{
    // Save window geometry
    QSettings settings;
    settings.setValue("optionsgeometry", saveGeometry());
    settings.setValue("optionstab", ui->categoryList->currentRow());

    QDialog::done(r);
}

void QVOptionsDialog::showEvent(QShowEvent *event)
{
#ifdef Q_OS_MACOS
    // On macOS, we don't make this dialog modal, so make sure it doesn't get covered by on top windows
    const auto updateWindowOnTop = [this]() {
        windowHandle()->setFlag(Qt::WindowStaysOnTopHint, qvApp->foundOnTopWindow());
    };
    updateWindowOnTop();
    connect(qvApp, &QVApplication::windowOnTopChanged, this, updateWindowOnTop);
#endif

    QDialog::showEvent(event);
}

void QVOptionsDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange)
    {
        customizePalette();
        populateCategories(ui->categoryList->currentRow());
    }
    QDialog::changeEvent(event);
}

void QVOptionsDialog::modifySetting(QString key, QVariant value)
{
    transientSettings.insert(key, value);
    updateButtonBox();
}

void QVOptionsDialog::saveSettings()
{
    QSettings settings;

    settings.beginGroup("options");

    const auto keys = transientSettings.keys();
    for (const auto &key : keys)
    {
        const auto &value = transientSettings[key];
        settings.setValue(key, value);
    }

    settings.setValue("disabledfileextensions", Qv::setToList(transientDisabledFileExtensions).join(';'));

    settings.endGroup();

    settings.beginGroup("shortcuts");

    const auto &shortcutsList = qvApp->getShortcutManager().getShortcutsList();
    for (int i = 0; i < transientShortcuts.length(); i++)
    {
        settings.setValue(shortcutsList.value(i).name, transientShortcuts.value(i));
    }

    qvApp->getShortcutManager().updateShortcuts();
    qvApp->getSettingsManager().loadSettings();
}

void QVOptionsDialog::syncSettings(bool defaults, bool makeConnections)
{
    auto &settingsManager = qvApp->getSettingsManager();
    settingsManager.loadSettings();

    // bgcolorenabled
    syncCheckbox(ui->bgColorCheckbox, "bgcolorenabled", defaults, makeConnections);
    bgColorCheckboxCheckStateChanged(ui->bgColorCheckbox->checkState());
    // bgcolor
    ui->bgColorButton->setText(settingsManager.getString("bgcolor", defaults));
    transientSettings.insert("bgcolor", ui->bgColorButton->text());
    updateBgColorButton();
    connect(ui->bgColorButton, &QPushButton::clicked, this, &QVOptionsDialog::bgColorButtonClicked);
    // checkerboardbackground
    syncCheckbox(ui->checkerboardBackgroundCheckbox, "checkerboardbackground", defaults, makeConnections);
    // titlebarmode
    syncComboBox(ui->titlebarComboBox, "titlebarmode", defaults, makeConnections);
    // customtitlebartext
    syncLineEdit(ui->customTitlebarLineEdit, "customtitlebartext", defaults, makeConnections);
    // windowresizemode
    syncComboBox(ui->windowResizeComboBox, "windowresizemode", defaults, makeConnections);
    // aftermatchingsize
    syncComboBox(ui->afterMatchingSizeComboBox, "aftermatchingsizemode", defaults, makeConnections);
    // minwindowresizedpercentage
    syncSpinBox(ui->minWindowResizeSpinBox, "minwindowresizedpercentage", defaults, makeConnections);
    // maxwindowresizedperecentage
    syncSpinBox(ui->maxWindowResizeSpinBox, "maxwindowresizedpercentage", defaults, makeConnections);
    // titlebaralwaysdark
    syncCheckbox(ui->darkTitlebarCheckbox, "titlebaralwaysdark", defaults, makeConnections);
    // quitonlastwindow
    syncCheckbox(ui->quitOnLastWindowCheckbox, "quitonlastwindow", defaults, makeConnections);
    // menubarenabled
    syncCheckbox(ui->menubarCheckbox, "menubarenabled", defaults, makeConnections);
    // fullscreendetails
    syncCheckbox(ui->detailsInFullscreen, "fullscreendetails", defaults, makeConnections);
    // mainmenuicons
    syncCheckbox(ui->mainMenuIconsCheckbox, "mainmenuicons", defaults, makeConnections);
    // contextmenuicons
    syncCheckbox(ui->contextMenuIconsCheckbox, "contextmenuicons", defaults, makeConnections);
    // submenuicons
    syncCheckbox(ui->submenuIconsCheckbox, "submenuicons", defaults, makeConnections);
    // slideshowkeepswindowontop
    syncCheckbox(ui->slideshowKeepsWindowOnTopCheckbox, "slideshowkeepswindowontop", defaults, makeConnections);
    // reusewindow
    syncCheckbox(ui->reuseWindowCheckbox, "reusewindow", defaults, makeConnections);
    // persistsession
    syncCheckbox(ui->persistSessionCheckbox, "persistsession", defaults, makeConnections);
    // smoothscalingmode
    syncComboBox(ui->smoothScalingComboBox, "smoothscalingmode", defaults, makeConnections);
    // scalingtwoenabled
    syncCheckbox(ui->scalingTwoCheckbox, "scalingtwoenabled", defaults, makeConnections);
    // smoothscalinglimitenabled
    syncCheckbox(ui->smoothScalingLimitCheckbox, "smoothscalinglimitenabled", defaults, makeConnections);
    // smoothscalinglimitpercent
    syncSpinBox(ui->smoothScalingLimitSpinBox, "smoothscalinglimitpercent", defaults, makeConnections);
    // scalefactor
    syncSpinBox(ui->scaleFactorSpinBox, "scalefactor", defaults, makeConnections);
    // cursorzoom
    syncCheckbox(ui->cursorZoomCheckbox, "cursorzoom", defaults, makeConnections);
    // onetoonepixelsizing
    syncCheckbox(ui->oneToOnePixelSizingCheckbox, "onetoonepixelsizing", defaults, makeConnections);
    // calculatedzoommode
    syncComboBox(ui->zoomDefaultComboBox, "calculatedzoommode", defaults, makeConnections);
    // fitzoomlimitenabled
    syncCheckbox(ui->fitZoomLimitCheckbox, "fitzoomlimitenabled", defaults, makeConnections);
    fitZoomLimitCheckboxCheckStateChanged(ui->fitZoomLimitCheckbox->checkState());
    // fitzoomlimitpercent
    syncSpinBox(ui->fitZoomLimitSpinBox, "fitzoomlimitpercent", defaults, makeConnections);
    // fitoverscan
    syncSpinBox(ui->fitOverscanSpinBox, "fitoverscan", defaults, makeConnections);
    // stickyzoom
    syncCheckbox(ui->stickyZoomCheckbox, "stickyzoom", defaults, makeConnections);
    // constrainimageposition
    syncCheckbox(ui->constrainImagePositionCheckbox, "constrainimageposition", defaults, makeConnections);
    constrainImagePositionCheckboxCheckStateChanged(ui->constrainImagePositionCheckbox->checkState());
    // constraincentersmallimage
    syncCheckbox(ui->constrainCentersSmallImageCheckbox, "constraincentersmallimage", defaults, makeConnections);
    // originalsizeastoggle
    syncCheckbox(ui->originalSizeAsToggleCheckbox, "originalsizeastoggle", defaults, makeConnections);
    // colorspaceconversion
    syncComboBox(ui->colorSpaceConversionComboBox, "colorspaceconversion", defaults, makeConnections);
    // language
    syncComboBox(ui->langComboBox, "language", defaults, makeConnections);
    // sortmode
    syncComboBox(ui->sortComboBox, "sortmode", defaults, makeConnections);
    // sortdescending
    syncRadioButtons({ui->descendingRadioButton0, ui->descendingRadioButton1}, "sortdescending", defaults, makeConnections);
    // preloadingmode
    syncComboBox(ui->preloadingComboBox, "preloadingmode", defaults, makeConnections);
    // navspeed
    syncSpinBox(ui->navSpeedSpinBox, "navspeed", defaults, makeConnections);
    // loopfolders
    syncCheckbox(ui->loopFoldersCheckbox, "loopfoldersenabled", defaults, makeConnections);
    // slideshowdirection
    syncComboBox(ui->slideshowDirectionComboBox, "slideshowdirection", defaults, makeConnections);
    // slideshowtimer
    syncDoubleSpinBox(ui->slideshowTimerSpinBox, "slideshowtimer", defaults, makeConnections);
    // afterdelete
    syncComboBox(ui->afterDeletionComboBox, "afterdelete", defaults, makeConnections);
    // askdelete
    syncCheckbox(ui->askDeleteCheckbox, "askdelete", defaults, makeConnections);
    // allowmimecontentdetection
    syncCheckbox(ui->mimeContentDetectionCheckbox, "allowmimecontentdetection", defaults, makeConnections);
    // skiphidden
    syncCheckbox(ui->skipHiddenCheckbox, "skiphidden", defaults, makeConnections);
    // saverecents
    syncCheckbox(ui->saveRecentsCheckbox, "saverecents", defaults, makeConnections);
    // updatenotifications
    syncCheckbox(ui->updateCheckbox, "updatenotifications", defaults, makeConnections);

    // mouse actions
    syncCheckbox(ui->navigationRegionsCheckbox, "navigationregionsenabled", defaults, makeConnections);
    syncComboBox(ui->doubleClickComboBox, "viewportdoubleclickaction", defaults, makeConnections);
    syncComboBox(ui->altDoubleClickComboBox, "viewportaltdoubleclickaction", defaults, makeConnections);
    syncComboBox(ui->dragComboBox, "viewportdragaction", defaults, makeConnections);
    syncComboBox(ui->altDragComboBox, "viewportaltdragaction", defaults, makeConnections);
    syncRadioButtons({ui->middleButtonModeClickRadioButton, ui->middleButtonModeDragRadioButton}, "viewportmiddlebuttonmode", defaults, makeConnections);
    middleButtonModeChanged();
    syncComboBox(ui->middleClickComboBox, "viewportmiddleclickaction", defaults, makeConnections);
    syncComboBox(ui->altMiddleClickComboBox, "viewportaltmiddleclickaction", defaults, makeConnections);
    syncComboBox(ui->middleDragComboBox, "viewportmiddledragaction", defaults, makeConnections);
    syncComboBox(ui->altMiddleDragComboBox, "viewportaltmiddledragaction", defaults, makeConnections);
    syncComboBox(ui->verticalScrollComboBox, "viewportverticalscrollaction", defaults, makeConnections);
    syncComboBox(ui->horizontalScrollComboBox, "viewporthorizontalscrollaction", defaults, makeConnections);
    syncComboBox(ui->altVerticalScrollComboBox, "viewportaltverticalscrollaction", defaults, makeConnections);
    syncComboBox(ui->altHorizontalScrollComboBox, "viewportalthorizontalscrollaction", defaults, makeConnections);
    syncCheckbox(ui->scrollActionCooldownCheckbox, "scrollactioncooldown", defaults, makeConnections);
    syncCheckbox(ui->cursorAutoHideFullscreenCheckbox, "cursorautohidefullscreenenabled", defaults, makeConnections);
    cursorAutoHideFullscreenCheckboxCheckStateChanged(ui->cursorAutoHideFullscreenCheckbox->checkState());
    syncDoubleSpinBox(ui->cursorAutoHideFullscreenDelaySpinBox, "cursorautohidefullscreendelay", defaults, makeConnections);
}

void QVOptionsDialog::syncCheckbox(QCheckBox *checkbox, const QString &key, bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getBoolean(key, defaults);
    checkbox->setChecked(val);
    transientSettings.insert(key, val);

    if (makeConnection)
    {
        connect(checkbox, &QCheckBox::checkStateChanged, this, [this, key](Qt::CheckState state) {
            modifySetting(key, static_cast<bool>(state));
        });
    }
}

void QVOptionsDialog::syncRadioButtons(QList<QRadioButton *> buttons, const QString &key, bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getInteger(key, defaults);
    if (auto widget = buttons.value(val))
        widget->setChecked(true);
    transientSettings.insert(key, val);

    if (makeConnection)
    {
        for (int i = 0; i < buttons.length(); i++)
        {
            connect(buttons.value(i), &QRadioButton::clicked, this, [this, key, i] {
                modifySetting(key, i);
            });
        }
    }
}

void QVOptionsDialog::syncComboBox(QComboBox *comboBox, const QString &key, bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getString(key, defaults);
    comboBox->setCurrentIndex(comboBox->findData(val));
    transientSettings.insert(key, val);

    if (makeConnection)
    {
        connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key, comboBox](int index) {
            Q_UNUSED(index)
            modifySetting(key, comboBox->currentData());
        });
    }
}

void QVOptionsDialog::syncSpinBox(QSpinBox *spinBox, const QString &key, bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getInteger(key, defaults);
    spinBox->setValue(val);
    transientSettings.insert(key, val);

    if (makeConnection)
    {
        connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, key](int i) {
            modifySetting(key, i);
        });
    }
}

void QVOptionsDialog::syncDoubleSpinBox(QDoubleSpinBox *doubleSpinBox, const QString &key, bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getDouble(key, defaults);
    doubleSpinBox->setValue(val);
    transientSettings.insert(key, val);

    if (makeConnection)
    {
        connect(doubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, key](double d) {
            modifySetting(key, d);
        });
    }
}

void QVOptionsDialog::syncLineEdit(QLineEdit *lineEdit, const QString &key, bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getString(key, defaults);
    lineEdit->setText(val);
    transientSettings.insert(key, val);

    if (makeConnection)
    {
        connect(lineEdit, &QLineEdit::textEdited, this, [this, key](const QString &text) {
            modifySetting(key, text);
        });
    }
}

void QVOptionsDialog::syncShortcuts(bool defaults)
{
    qvApp->getShortcutManager().updateShortcuts();

    transientShortcuts.clear();
    const auto &shortcutsList = qvApp->getShortcutManager().getShortcutsList();
    ui->shortcutsTable->setRowCount(shortcutsList.length());

    for (int i = 0; i < shortcutsList.length(); i++)
    {
        const ShortcutManager::SShortcut &shortcut = shortcutsList.value(i);

        // Add shortcut to transient shortcut list
        if (defaults)
            transientShortcuts.append(shortcut.defaultShortcuts);
        else
            transientShortcuts.append(shortcut.shortcuts);

        // Add shortcut to table widget
        auto *nameItem = new QTableWidgetItem();
        nameItem->setText(shortcut.readableName);
        ui->shortcutsTable->setItem(i, 0, nameItem);

        auto *shortcutsItem = new QTableWidgetItem();
        shortcutsItem->setText(ShortcutManager::stringListToReadableString(
                                   transientShortcuts.value(i)));
        ui->shortcutsTable->setItem(i, 1, shortcutsItem);
    }
    updateShortcutsTable();
}

void QVOptionsDialog::updateShortcutsTable()
{
    for (int i = 0; i < transientShortcuts.length(); i++)
    {
        const QStringList &shortcuts = transientShortcuts.value(i);
        ui->shortcutsTable->item(i, 1)->setText(ShortcutManager::stringListToReadableString(shortcuts));
    }
    updateButtonBox();
}

void QVOptionsDialog::shortcutCellDoubleClicked(int row, int column)
{
    Q_UNUSED(column)
    auto getTransientShortcutCallback = [this](int index) {
        return transientShortcuts.value(index);
    };
    auto *shortcutDialog = new QVShortcutDialog(row, getTransientShortcutCallback, this);
    connect(shortcutDialog, &QVShortcutDialog::shortcutsListChanged, this, [this](int index, const QStringList &stringListShortcuts) {
        transientShortcuts.replace(index, stringListShortcuts);
        updateShortcutsTable();
    });
    shortcutDialog->open();
}

void QVOptionsDialog::syncFormats(bool defaults)
{
    if (defaults)
        transientDisabledFileExtensions.clear();
    else
        transientDisabledFileExtensions = qvApp->getDisabledFileExtensions();

    const QStringList extensions = Qv::setToSortedList(qvApp->getAllFileExtensionList());

    isLoadingFormats = true;

    ui->formatsTable->setRowCount(extensions.count());

    int row = 0;
    for (const QString &extension : extensions)
    {
        const bool isDisabled = transientDisabledFileExtensions.contains(extension);

        auto *extensionItem = new QTableWidgetItem();
        extensionItem->setText(extension);
        ui->formatsTable->setItem(row, 0, extensionItem);

        auto *enabledItem = new QTableWidgetItem();
        enabledItem->setCheckState(isDisabled ? Qt::Unchecked : Qt::Checked);
        enabledItem->setData(Qt::UserRole, extension);
        ui->formatsTable->setItem(row, 1, enabledItem);

        row++;
    }

    isLoadingFormats = false;
}

void QVOptionsDialog::buttonBoxClicked(QAbstractButton *button)
{
    auto role = ui->buttonBox->buttonRole(button);
    if (role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::ApplyRole)
    {
        saveSettings();
        if (role == QDialogButtonBox::ApplyRole)
            button->setEnabled(false);
    }
    else if (role == QDialogButtonBox::ResetRole)
    {
        syncSettings(true);
        syncShortcuts(true);
        syncFormats(true);
    }
}

void QVOptionsDialog::updateButtonBox()
{
    QPushButton *defaultsButton = ui->buttonBox->button(QDialogButtonBox::RestoreDefaults);
    QPushButton *applyButton = ui->buttonBox->button(QDialogButtonBox::Apply);
    bool anyUnsaved = false;
    bool anyNonDefault = false;

    // settings
    const QList<QString> settingKeys = transientSettings.keys();
    for (const auto &key : settingKeys)
    {
        const auto &transientValue = transientSettings.value(key);
        const auto &savedValue = qvApp->getSettingsManager().getSetting(key);
        const auto &defaultValue = qvApp->getSettingsManager().getSetting(key, true);

        anyUnsaved |= transientValue != savedValue;
        anyNonDefault |= transientValue != defaultValue;
    }

    // shortcuts
    const QList<ShortcutManager::SShortcut> &shortcutsList = qvApp->getShortcutManager().getShortcutsList();
    for (int i = 0; i < transientShortcuts.length(); i++)
    {
        const auto &transientValue = transientShortcuts.value(i);
        QStringList savedValue = shortcutsList.value(i).shortcuts;
        QStringList defaultValue = shortcutsList.value(i).defaultShortcuts;

        anyUnsaved |= transientValue != savedValue;
        anyNonDefault |= transientValue != defaultValue;
    }

    // formats
    anyUnsaved |= transientDisabledFileExtensions != qvApp->getDisabledFileExtensions();
    anyNonDefault |= !transientDisabledFileExtensions.isEmpty();

    applyButton->setEnabled(anyUnsaved);
    defaultsButton->setEnabled(anyNonDefault);
}

void QVOptionsDialog::bgColorButtonClicked()
{
    auto *colorDialog = new QColorDialog(ui->bgColorButton->text(), this);
    colorDialog->setWindowModality(Qt::WindowModal);
    connect(colorDialog, &QDialog::accepted, colorDialog, [this, colorDialog] {
        auto selectedColor = colorDialog->currentColor();

        if (!selectedColor.isValid())
            return;

        modifySetting("bgcolor", selectedColor.name());
        ui->bgColorButton->setText(selectedColor.name());
        updateBgColorButton();
        colorDialog->deleteLater();
    });
    colorDialog->open();
}

void QVOptionsDialog::updateBgColorButton()
{
    QPixmap newPixmap = QPixmap(32, 32);
    newPixmap.fill(ui->bgColorButton->text());
    ui->bgColorButton->setIcon(QIcon(newPixmap));
}

void QVOptionsDialog::bgColorCheckboxCheckStateChanged(Qt::CheckState state)
{
    ui->bgColorButton->setEnabled(static_cast<bool>(state));
}

void QVOptionsDialog::restartNotifyForCheckbox(const QString &key, const Qt::CheckState state)
{
    const bool savedValue = qvApp->getSettingsManager().getBoolean(key);
    if (static_cast<bool>(state) != savedValue)
        QMessageBox::information(this, tr("Restart Required"), tr("You must restart qView for the setting change to take effect."));
}

void QVOptionsDialog::titlebarComboBoxCurrentIndexChanged(int index)
{
    const auto value = static_cast<Qv::TitleBarText>(ui->titlebarComboBox->itemData(index).toInt());
    ui->customTitlebarLineEdit->setEnabled(value == Qv::TitleBarText::Custom);
}

void QVOptionsDialog::smoothScalingComboBoxCurrentIndexChanged(int index)
{
    const auto value = static_cast<Qv::SmoothScalingMode>(ui->smoothScalingComboBox->itemData(index).toInt());
    ui->scalingTwoCheckbox->setEnabled(value == Qv::SmoothScalingMode::Expensive);
    ui->smoothScalingLimitCheckbox->setEnabled(value != Qv::SmoothScalingMode::Disabled);
    smoothScalingLimitCheckboxCheckStateChanged(ui->smoothScalingLimitCheckbox->checkState());
}

void QVOptionsDialog::smoothScalingLimitCheckboxCheckStateChanged(Qt::CheckState state)
{
    const bool selfEnabled = ui->smoothScalingLimitCheckbox->isEnabled();
    ui->smoothScalingLimitSpinBox->setEnabled(selfEnabled && static_cast<bool>(state));
}

void QVOptionsDialog::fitZoomLimitCheckboxCheckStateChanged(Qt::CheckState state)
{
    ui->fitZoomLimitSpinBox->setEnabled(static_cast<bool>(state));
}

void QVOptionsDialog::constrainImagePositionCheckboxCheckStateChanged(Qt::CheckState state)
{
    ui->constrainCentersSmallImageCheckbox->setEnabled(static_cast<bool>(state));
}

void QVOptionsDialog::cursorAutoHideFullscreenCheckboxCheckStateChanged(Qt::CheckState state)
{
    ui->cursorAutoHideFullscreenDelaySpinBox->setEnabled(static_cast<bool>(state));
}

void QVOptionsDialog::customizePalette()
{
    const QString currentStyle = qApp->style()->objectName();
    const QPalette appPalette = QApplication::palette();
    if (currentStyle.compare("fusion", Qt::CaseInsensitive) == 0 ||
        currentStyle.compare("macos", Qt::CaseInsensitive) == 0)
    {
        const QColor textColor = appPalette.color(QPalette::WindowText);
        QPalette palette = ui->categoryList->palette();
        palette.setColor(QPalette::HighlightedText, textColor);
        palette.setColor(QPalette::Highlight, Qv::getPerceivedBrightness(textColor) > 0.5 ? QColor(0, 65, 127) : QColor(75, 166, 255));
        ui->categoryList->setPalette(palette);
    }
}

void QVOptionsDialog::populateCategories(int selectedRow)
{
    const int iconSize = 24;
    const int listRightPadding = 3;
    const auto addItem = [&](const Qv::MaterialIcon iconName, const QString &text) {
        ui->categoryList->addItem(new QListWidgetItem(qvApp->iconFromFont(iconName), text));
    };
    ui->categoryList->setIconSize(QSize(iconSize, iconSize));
    ui->categoryList->setFont(QApplication::font());
    ui->categoryList->clear();
    addItem(Qv::MaterialIcon::WebAsset, tr("Window"));
    addItem(Qv::MaterialIcon::Image, tr("Image"));
    addItem(Qv::MaterialIcon::Tune, tr("Miscellaneous"));
    addItem(Qv::MaterialIcon::Keyboard, tr("Shortcuts"));
    addItem(Qv::MaterialIcon::Mouse, tr("Mouse"));
    addItem(Qv::MaterialIcon::Extension, tr("Formats"));
    ui->categoryList->setCurrentRow(selectedRow);
    ui->categoryList->setFixedWidth(ui->categoryList->sizeHintForColumn(0) + ui->categoryList->frameWidth() + listRightPadding);
}

void QVOptionsDialog::populateLanguages()
{
    ui->langComboBox->clear();

    ui->langComboBox->addItem(tr("System Language"), "system");

    // Put english at the top seperately because it has no file
    ui->langComboBox->addItem("English (en)", "en");

    const auto entries = QDir(":/i18n/").entryList();
    for (auto entry : entries)
    {
        entry.remove(0, 6);
        entry.remove(entry.length()-3, 3);
        QLocale locale(entry);

        const QString langString = locale.nativeLanguageName() + " (" + entry + ")";

        ui->langComboBox->addItem(langString, entry);
    }
}

void QVOptionsDialog::languageComboBoxCurrentIndexChanged(int index)
{
    Q_UNUSED(index)
    if (!isInitialLoad && !languageRestartMessageShown)
    {
        QMessageBox::information(this, tr("Restart Required"), tr("You must restart qView for the language change to take effect."));
        languageRestartMessageShown = true;
    }
}

void QVOptionsDialog::formatsItemChanged(QTableWidgetItem *item)
{
    if (isLoadingFormats)
        return;

    const QString extension = item->data(Qt::UserRole).toString();
    if (item->checkState() == Qt::Unchecked)
        transientDisabledFileExtensions.insert(extension);
    else
        transientDisabledFileExtensions.remove(extension);

    updateButtonBox();
}

void QVOptionsDialog::middleButtonModeChanged()
{
    const bool isClick = ui->middleButtonModeClickRadioButton->isChecked();
    const bool isDrag = ui->middleButtonModeDragRadioButton->isChecked();
    ui->middleClickLabel->setVisible(isClick);
    ui->middleClickComboBox->setVisible(isClick);
    ui->altMiddleClickLabel->setVisible(isClick);
    ui->altMiddleClickComboBox->setVisible(isClick);
    ui->middleDragLabel->setVisible(isDrag);
    ui->middleDragComboBox->setVisible(isDrag);
    ui->altMiddleDragLabel->setVisible(isDrag);
    ui->altMiddleDragComboBox->setVisible(isDrag);
}

const Ui::ComboBoxItems<Qv::AfterDelete> QVOptionsDialog::mapAfterDelete() {
    return {
        { Qv::AfterDelete::MoveBack, tr("Move Back") },
        { Qv::AfterDelete::DoNothing, tr("Do Nothing") },
        { Qv::AfterDelete::MoveForward, tr("Move Forward") }
    };
}

const Ui::ComboBoxItems<Qv::AfterMatchingSize> QVOptionsDialog::mapAfterMatchingSize() {
    return {
        { Qv::AfterMatchingSize::AvoidRepositioning, tr("Avoid repositioning") },
        { Qv::AfterMatchingSize::CenterOnPrevious, tr("Center relative to previous image") },
        { Qv::AfterMatchingSize::CenterOnScreen, tr("Center relative to screen") }
    };
}

const Ui::ComboBoxItems<Qv::CalculatedZoomMode> QVOptionsDialog::mapCalculatedZoomMode() {
    return {
        { Qv::CalculatedZoomMode::ZoomToFit, tr("Zoom to Fit") },
        { Qv::CalculatedZoomMode::FillWindow, tr("Fill Window") },
        { Qv::CalculatedZoomMode::OriginalSize, tr("Original Size") }
    };
}

const Ui::ComboBoxItems<Qv::ColorSpaceConversion> QVOptionsDialog::mapColorSpaceConversion() {
    return {
        { Qv::ColorSpaceConversion::Disabled, tr("Disabled") },
        { Qv::ColorSpaceConversion::AutoDetect, tr("Auto-detect") },
        { Qv::ColorSpaceConversion::SRgb, tr("sRGB") },
        { Qv::ColorSpaceConversion::DisplayP3, tr("Display P3") }
    };
}

const Ui::ComboBoxItems<Qv::PreloadMode> QVOptionsDialog::mapPreloadMode() {
    return {
        { Qv::PreloadMode::Disabled, tr("Disabled") },
        { Qv::PreloadMode::Adjacent, tr("Adjacent") },
        { Qv::PreloadMode::Extended, tr("Extended") }
    };
}

const Ui::ComboBoxItems<Qv::SlideshowDirection> QVOptionsDialog::mapSlideshowDirection() {
    return {
        { Qv::SlideshowDirection::Forward, tr("Forward") },
        { Qv::SlideshowDirection::Backward, tr("Backward") },
        { Qv::SlideshowDirection::Random, tr("Random") }
    };
}

const Ui::ComboBoxItems<Qv::SmoothScalingMode> QVOptionsDialog::mapSmoothScalingMode() {
    return {
        { Qv::SmoothScalingMode::Disabled, tr("Disabled") },
        { Qv::SmoothScalingMode::Bilinear, tr("Bilinear") },
        { Qv::SmoothScalingMode::Expensive, tr("Expensive") }
    };
}

const Ui::ComboBoxItems<Qv::SortMode> QVOptionsDialog::mapSortMode() {
    return {
        { Qv::SortMode::Name, tr("Name") },
        { Qv::SortMode::DateModified, tr("Date Modified") },
        { Qv::SortMode::DateCreated, tr("Date Created") },
        { Qv::SortMode::Size, tr("Size") },
        { Qv::SortMode::Type, tr("Type") },
        { Qv::SortMode::Random, tr("Random") }
    };
}

const Ui::ComboBoxItems<Qv::TitleBarText> QVOptionsDialog::mapTitleBarText() {
    return {
        { Qv::TitleBarText::Basic, tr("Basic") },
        { Qv::TitleBarText::Minimal, tr("Minimal") },
        { Qv::TitleBarText::Practical, tr("Practical") },
        { Qv::TitleBarText::Verbose, tr("Verbose") },
        { Qv::TitleBarText::Custom, tr("Custom") }
    };
}

const Ui::ComboBoxItems<Qv::WindowResizeMode> QVOptionsDialog::mapWindowResizeMode() {
    return {
        { Qv::WindowResizeMode::Never, tr("Never") },
        { Qv::WindowResizeMode::WhenLaunching, tr("When launching") },
        { Qv::WindowResizeMode::WhenOpeningImages, tr("When opening images") }
    };
}

const Ui::ComboBoxItems<Qv::ViewportClickAction> QVOptionsDialog::mapViewportClickAction() {
    return {
        { Qv::ViewportClickAction::None, tr("None") },
        { Qv::ViewportClickAction::ZoomToFit, tr("Zoom to Fit") },
        { Qv::ViewportClickAction::FillWindow, tr("Fill Window") },
        { Qv::ViewportClickAction::OriginalSize, tr("Original Size") },
        { Qv::ViewportClickAction::CenterImage, tr("Center Image") },
        { Qv::ViewportClickAction::ToggleFullScreen, tr("Toggle Full Screen") },
        { Qv::ViewportClickAction::ToggleTitlebarHidden, tr("Toggle Titlebar Hidden") }
    };
}

const Ui::ComboBoxItems<Qv::ViewportDragAction> QVOptionsDialog::mapViewportDragAction() {
    return {
        { Qv::ViewportDragAction::None, tr("None") },
        { Qv::ViewportDragAction::Pan, tr("Pan") },
        { Qv::ViewportDragAction::MoveWindow, tr("Move Window") }
    };
}

const Ui::ComboBoxItems<Qv::ViewportScrollAction> QVOptionsDialog::mapViewportScrollAction() {
    return {
        { Qv::ViewportScrollAction::None, tr("None") },
        { Qv::ViewportScrollAction::Zoom, tr("Zoom") },
        { Qv::ViewportScrollAction::Navigate, tr("Navigate") },
        { Qv::ViewportScrollAction::Pan, tr("Pan") }
    };
}

template <typename TEnum>
static void populateComboBox(QComboBox *comboBox, const Ui::ComboBoxItems<TEnum> &items)
{
    comboBox->clear();
    for (const auto &item : items)
    {
        comboBox->addItem(item.second, static_cast<int>(item.first));
    }
}

void QVOptionsDialog::populateComboBoxes()
{
    populateComboBox(ui->titlebarComboBox, mapTitleBarText());

    populateComboBox(ui->windowResizeComboBox, mapWindowResizeMode());

    populateComboBox(ui->afterMatchingSizeComboBox, mapAfterMatchingSize());

    populateComboBox(ui->smoothScalingComboBox, mapSmoothScalingMode());

    populateComboBox(ui->zoomDefaultComboBox, mapCalculatedZoomMode());

    populateComboBox(ui->colorSpaceConversionComboBox, mapColorSpaceConversion());

    populateComboBox(ui->sortComboBox, mapSortMode());

    populateComboBox(ui->preloadingComboBox, mapPreloadMode());

    populateComboBox(ui->slideshowDirectionComboBox, mapSlideshowDirection());

    populateComboBox(ui->afterDeletionComboBox, mapAfterDelete());

    populateComboBox(ui->doubleClickComboBox, mapViewportClickAction());
    populateComboBox(ui->altDoubleClickComboBox, mapViewportClickAction());
    populateComboBox(ui->middleClickComboBox, mapViewportClickAction());
    populateComboBox(ui->altMiddleClickComboBox, mapViewportClickAction());

    populateComboBox(ui->dragComboBox, mapViewportDragAction());
    populateComboBox(ui->altDragComboBox, mapViewportDragAction());
    populateComboBox(ui->middleDragComboBox, mapViewportDragAction());
    populateComboBox(ui->altMiddleDragComboBox, mapViewportDragAction());

    populateComboBox(ui->verticalScrollComboBox, mapViewportScrollAction());
    populateComboBox(ui->horizontalScrollComboBox, mapViewportScrollAction());
    populateComboBox(ui->altVerticalScrollComboBox, mapViewportScrollAction());
    populateComboBox(ui->altHorizontalScrollComboBox, mapViewportScrollAction());
}
