#include "qvoptionsdialog.h"
#include "ui_qvoptionsdialog.h"
#include "qvapplication.h"
#include <QColorDialog>
#include <QPalette>
#include <QScreen>
#include <QMessageBox>
#include <QSettings>

#include <QDebug>

QVOptionsDialog::QVOptionsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::QVOptionsDialog)
{
    ui->setupUi(this);

    languageRestartMessageShown = false;

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint | Qt::CustomizeWindowHint));

    populateComboBoxes();

    connect(ui->buttonBox, &QDialogButtonBox::clicked, this, &QVOptionsDialog::buttonBoxClicked);
    connect(ui->shortcutsTable, &QTableWidget::cellDoubleClicked, this, &QVOptionsDialog::shortcutCellDoubleClicked);
    connect(ui->bgColorCheckbox, &QCheckBox::stateChanged, this, &QVOptionsDialog::bgColorCheckboxStateChanged);
    connect(ui->nonNativeThemeCheckbox, &QCheckBox::stateChanged, this, [this](int state) { restartNotifyForCheckbox("nonnativetheme", state); });
    connect(ui->submenuIconsCheckbox, &QCheckBox::stateChanged, this, [this](int state) { restartNotifyForCheckbox("submenuicons", state); });
    connect(ui->scalingCheckbox, &QCheckBox::stateChanged, this, &QVOptionsDialog::scalingCheckboxStateChanged);
    connect(ui->constrainImagePositionCheckbox, &QCheckBox::stateChanged, this, &QVOptionsDialog::constrainImagePositionCheckboxStateChanged);

    populateLanguages();

    QSettings settings;
    ui->tabWidget->setCurrentIndex(settings.value("optionstab", 1).toInt());

    // On macOS, the dialog should not be dependent on any window
#ifndef Q_OS_MACOS
    setWindowModality(Qt::WindowModal);
#else
    // Load window geometry
    restoreGeometry(settings.value("optionsgeometry").toByteArray());
#endif


    if (QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13)) {
        setWindowTitle("Preferences");
    }

// Platform specific settings
#if !defined(Q_OS_WIN) || QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
    ui->nonNativeThemeCheckbox->hide();
#endif
#ifdef Q_OS_MACOS
    ui->menubarCheckbox->hide();
#else
    ui->darkTitlebarCheckbox->hide();
    ui->quitOnLastWindowCheckbox->hide();
#endif

    if (!QVApplication::supportsSessionPersistence())
        ui->persistSessionCheckbox->hide();

// Hide language selection below 5.12, as 5.12 does not support embedding the translations :(
#if (QT_VERSION < QT_VERSION_CHECK(5, 12, 0))
    ui->langComboBox->hide();
    ui->langComboLabel->hide();
#endif

// Hide color space conversion below 5.14, which is when color space support was introduced
#if (QT_VERSION < QT_VERSION_CHECK(5, 14, 0))
    ui->colorSpaceConversionComboBox->hide();
    ui->colorSpaceConversionLabel->hide();
#endif

    syncSettings(false, true);
    connect(ui->titlebarComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QVOptionsDialog::titlebarComboBoxCurrentIndexChanged);
    connect(ui->windowResizeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QVOptionsDialog::windowResizeComboBoxCurrentIndexChanged);
    connect(ui->langComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QVOptionsDialog::languageComboBoxCurrentIndexChanged);
    syncShortcuts();
    updateButtonBox();
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
    settings.setValue("optionstab", ui->tabWidget->currentIndex());

    QDialog::done(r);
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
    if (ui->bgColorCheckbox->isChecked())
        ui->bgColorButton->setEnabled(true);
    else
        ui->bgColorButton->setEnabled(false);
    // bgcolor
    ui->bgColorButton->setText(settingsManager.getString("bgcolor", defaults));
    transientSettings.insert("bgcolor", ui->bgColorButton->text());
    updateBgColorButton();
    connect(ui->bgColorButton, &QPushButton::clicked, this, &QVOptionsDialog::bgColorButtonClicked);
    // checkerboardbackground
    syncCheckbox(ui->checkerboardBackgroundCheckbox, "checkerboardbackground", defaults, makeConnections);
    // titlebarmode
    syncComboBox(ui->titlebarComboBox, "titlebarmode", defaults, makeConnections);
    titlebarComboBoxCurrentIndexChanged(ui->titlebarComboBox->currentIndex());
    // customtitlebartext
    syncLineEdit(ui->customTitlebarLineEdit, "customtitlebartext", defaults, makeConnections);
    // windowresizemode
    syncComboBox(ui->windowResizeComboBox, "windowresizemode", defaults, makeConnections);
    windowResizeComboBoxCurrentIndexChanged(ui->windowResizeComboBox->currentIndex());
    // aftermatchingsize
    syncComboBox(ui->afterMatchingSizeComboBox, "aftermatchingsizemode", defaults, makeConnections);
    // minwindowresizedpercentage
    syncSpinBox(ui->minWindowResizeSpinBox, "minwindowresizedpercentage", defaults, makeConnections);
    // maxwindowresizedperecentage
    syncSpinBox(ui->maxWindowResizeSpinBox, "maxwindowresizedpercentage", defaults, makeConnections);
    // nonnativetheme
    syncCheckbox(ui->nonNativeThemeCheckbox, "nonnativetheme", defaults, makeConnections);
    // titlebaralwaysdark
    syncCheckbox(ui->darkTitlebarCheckbox, "titlebaralwaysdark", defaults, makeConnections);
    // quitonlastwindow
    syncCheckbox(ui->quitOnLastWindowCheckbox, "quitonlastwindow", defaults, makeConnections);
    // menubarenabled
    syncCheckbox(ui->menubarCheckbox, "menubarenabled", defaults, makeConnections);
    // fullscreendetails
    syncCheckbox(ui->detailsInFullscreen, "fullscreendetails", defaults, makeConnections);
    // submenuicons
    syncCheckbox(ui->submenuIconsCheckbox, "submenuicons", defaults, makeConnections);
    // persistsession
    syncCheckbox(ui->persistSessionCheckbox, "persistsession", defaults, makeConnections);
    // filteringenabled
    syncCheckbox(ui->filteringCheckbox, "filteringenabled", defaults, makeConnections);
    // scalingenabled
    syncCheckbox(ui->scalingCheckbox, "scalingenabled", defaults, makeConnections);
    if (ui->scalingCheckbox->isChecked())
        ui->scalingTwoCheckbox->setEnabled(true);
    else
        ui->scalingTwoCheckbox->setEnabled(false);
    // scalingtwoenabled
    syncCheckbox(ui->scalingTwoCheckbox, "scalingtwoenabled", defaults, makeConnections);
    // scalefactor
    syncSpinBox(ui->scaleFactorSpinBox, "scalefactor", defaults, makeConnections);
    // scrollzoomsenabled
    syncCheckbox(ui->scrollZoomsCheckbox, "scrollzoomsenabled", defaults, makeConnections);
    // cursorzoom
    syncCheckbox(ui->cursorZoomCheckbox, "cursorzoom", defaults, makeConnections);
    // onetoonepixelsizing
    syncCheckbox(ui->oneToOnePixelSizingCheckbox, "onetoonepixelsizing", defaults, makeConnections);
    // cropmode
    syncComboBox(ui->cropModeComboBox, "cropmode", defaults, makeConnections);
    // pastactualsizeenabled
    syncCheckbox(ui->pastActualSizeCheckbox, "pastactualsizeenabled", defaults, makeConnections);
    // fitoverscan
    syncSpinBox(ui->fitOverscanSpinBox, "fitoverscan", defaults, makeConnections);
    // constrainimageposition
    syncCheckbox(ui->constrainImagePositionCheckbox, "constrainimageposition", defaults, makeConnections);
    if (ui->constrainImagePositionCheckbox->isChecked())
        ui->constrainCentersSmallImageCheckbox->setEnabled(true);
    else
        ui->constrainCentersSmallImageCheckbox->setEnabled(false);
    // constraincentersmallimage
    syncCheckbox(ui->constrainCentersSmallImageCheckbox, "constraincentersmallimage", defaults, makeConnections);
    // sidewaysscrollnavigates
    syncCheckbox(ui->sidewaysScrollNavigatesCheckbox, "sidewaysscrollnavigates", defaults, makeConnections);
    // colorspaceconversion
    syncComboBox(ui->colorSpaceConversionComboBox, "colorspaceconversion", defaults, makeConnections);
    // language
    syncComboBoxData(ui->langComboBox, "language", defaults, makeConnections);
    // sortmode
    syncComboBox(ui->sortComboBox, "sortmode", defaults, makeConnections);
    // sortdescending
    syncRadioButtons({ui->descendingRadioButton0, ui->descendingRadioButton1}, "sortdescending", defaults, makeConnections);
    // preloadingmode
    syncComboBox(ui->preloadingComboBox, "preloadingmode", defaults, makeConnections);
    // loopfolders
    syncCheckbox(ui->loopFoldersCheckbox, "loopfoldersenabled", defaults, makeConnections);
    // slideshowreversed
    syncComboBox(ui->slideshowDirectionComboBox, "slideshowreversed", defaults, makeConnections);
    // slideshowtimer
    syncDoubleSpinBox(ui->slideshowTimerSpinBox, "slideshowtimer", defaults, makeConnections);
    // afterdelete
    syncComboBox(ui->afterDeletionComboBox, "afterdelete", defaults, makeConnections);
    // askdelete
    syncCheckbox(ui->askDeleteCheckbox, "askdelete", defaults, makeConnections);
    // allowmimecontentdetection
    syncCheckbox(ui->mimeContentDetectionCheckbox, "allowmimecontentdetection", defaults, makeConnections);
    // saverecents
    syncCheckbox(ui->saveRecentsCheckbox, "saverecents", defaults, makeConnections);
    // updatenotifications
    syncCheckbox(ui->updateCheckbox, "updatenotifications", defaults, makeConnections);
}

void QVOptionsDialog::syncCheckbox(QCheckBox *checkbox, const QString &key, bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getBoolean(key, defaults);
    checkbox->setChecked(val);
    transientSettings.insert(key, val);

    if (makeConnection)
    {
        connect(checkbox, &QCheckBox::stateChanged, this, [this, key](int state) {
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
    auto val = qvApp->getSettingsManager().getInteger(key, defaults);
    comboBox->setCurrentIndex(val);
    transientSettings.insert(key, val);

    if (makeConnection)
    {
        connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key](int index) {
            modifySetting(key, index);
        });
    }
}

void QVOptionsDialog::syncComboBoxData(QComboBox *comboBox, const QString &key, bool defaults, bool makeConnection)
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
    auto *shortcutDialog = new QVShortcutDialog(row, this);
    connect(shortcutDialog, &QVShortcutDialog::shortcutsListChanged, this, [this](int index, const QStringList &stringListShortcuts) {
        transientShortcuts.replace(index, stringListShortcuts);
        updateShortcutsTable();
    });
    shortcutDialog->open();
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
    }
}

void QVOptionsDialog::updateButtonBox()
{
    QPushButton *defaultsButton = ui->buttonBox->button(QDialogButtonBox::RestoreDefaults);
    QPushButton *applyButton = ui->buttonBox->button(QDialogButtonBox::Apply);
    defaultsButton->setEnabled(false);
    applyButton->setEnabled(false);

    // settings
    const QList<QString> settingKeys = transientSettings.keys();
    for (const auto &key : settingKeys)
    {
        const auto &transientValue = transientSettings.value(key);
        const auto &savedValue = qvApp->getSettingsManager().getSetting(key);
        const auto &defaultValue = qvApp->getSettingsManager().getSetting(key, true);

        if (transientValue != savedValue)
            applyButton->setEnabled(true);
        if (transientValue != defaultValue)
            defaultsButton->setEnabled(true);
    }

    // shortcuts
    const QList<ShortcutManager::SShortcut> &shortcutsList = qvApp->getShortcutManager().getShortcutsList();
    for (int i = 0; i < transientShortcuts.length(); i++)
    {
        const auto &transientValue = transientShortcuts.value(i);
        QStringList savedValue = shortcutsList.value(i).shortcuts;
        QStringList defaultValue = shortcutsList.value(i).defaultShortcuts;

        if (transientValue != savedValue)
            applyButton->setEnabled(true);
        if (transientValue != defaultValue)
            defaultsButton->setEnabled(true);
    }
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

void QVOptionsDialog::bgColorCheckboxStateChanged(int state)
{
    ui->bgColorButton->setEnabled(static_cast<bool>(state));
    updateBgColorButton();
}

void QVOptionsDialog::restartNotifyForCheckbox(const QString &key, const int state)
{
    const bool savedValue = qvApp->getSettingsManager().getBoolean(key);
    if (static_cast<bool>(state) != savedValue)
        QMessageBox::information(this, tr("Restart Required"), tr("You must restart qView to change this setting."));
}

void QVOptionsDialog::scalingCheckboxStateChanged(int state)
{
    ui->scalingTwoCheckbox->setEnabled(static_cast<bool>(state));
}

void QVOptionsDialog::titlebarComboBoxCurrentIndexChanged(int index)
{
    ui->customTitlebarLineEdit->setEnabled(index == 4);
}

void QVOptionsDialog::windowResizeComboBoxCurrentIndexChanged(int index)
{
    bool enableRelatedControls = index != 0;
    ui->afterMatchingSizeLabel->setEnabled(enableRelatedControls);
    ui->afterMatchingSizeComboBox->setEnabled(enableRelatedControls);
    ui->minWindowResizeLabel->setEnabled(enableRelatedControls);
    ui->minWindowResizeSpinBox->setEnabled(enableRelatedControls);
    ui->maxWindowResizeLabel->setEnabled(enableRelatedControls);
    ui->maxWindowResizeSpinBox->setEnabled(enableRelatedControls);
}

void QVOptionsDialog::constrainImagePositionCheckboxStateChanged(int state)
{
    ui->constrainCentersSmallImageCheckbox->setEnabled(static_cast<bool>(state));
}

void QVOptionsDialog::populateLanguages()
{
    ui->langComboBox->clear();

    ui->langComboBox->addItem(tr("System Language"), "system");

    // Put english at the top seperately because it has no file
    QLocale eng("en");
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
    if (!languageRestartMessageShown)
    {
        QMessageBox::information(this, tr("Restart Required"), tr("You must restart qView to change the language."));
        languageRestartMessageShown = true;
    }
}

const QMap<Qv::AfterDelete, QString> QVOptionsDialog::mapAfterDelete =
{
    { Qv::AfterDelete::MoveBack, tr("Move Back") },
    { Qv::AfterDelete::DoNothing, tr("Do Nothing") },
    { Qv::AfterDelete::MoveForward, tr("Move Forward") }
};

const QMap<Qv::AfterMatchingSize, QString> QVOptionsDialog::mapAfterMatchingSize =
{
    { Qv::AfterMatchingSize::AvoidRepositioning, tr("Avoid repositioning") },
    { Qv::AfterMatchingSize::CenterOnPrevious, tr("Center relative to previous image") },
    { Qv::AfterMatchingSize::CenterOnScreen, tr("Center relative to screen") }
};

const QMap<Qv::ColorSpaceConversion, QString> QVOptionsDialog::mapColorSpaceConversion =
{
    { Qv::ColorSpaceConversion::Disabled, tr("Disabled") },
    { Qv::ColorSpaceConversion::AutoDetect, tr("Auto-detect") },
    { Qv::ColorSpaceConversion::SRgb, tr("sRGB") },
    { Qv::ColorSpaceConversion::DisplayP3, tr("Display P3") }
};

const QMap<Qv::FitMode, QString> QVOptionsDialog::mapFitMode =
{
    { Qv::FitMode::WholeImage, tr("Fit whole image") },
    { Qv::FitMode::OnlyHeight, tr("Fit height") },
    { Qv::FitMode::OnlyWidth, tr("Fit width") }
};

const QMap<Qv::PreloadMode, QString> QVOptionsDialog::mapPreloadMode =
{
    { Qv::PreloadMode::Disabled, tr("Disabled") },
    { Qv::PreloadMode::Adjacent, tr("Adjacent") },
    { Qv::PreloadMode::Extended, tr("Extended") }
};

const QMap<Qv::SortMode, QString> QVOptionsDialog::mapSortMode =
{
    { Qv::SortMode::Name, tr("Name") },
    { Qv::SortMode::DateModified, tr("Date Modified") },
    { Qv::SortMode::DateCreated, tr("Date Created") },
    { Qv::SortMode::Size, tr("Size") },
    { Qv::SortMode::Type, tr("Type") },
    { Qv::SortMode::Random, tr("Random") }
};

const QMap<Qv::TitleBarText, QString> QVOptionsDialog::mapTitleBarText =
{
    { Qv::TitleBarText::Basic, tr("Basic") },
    { Qv::TitleBarText::Minimal, tr("Minimal") },
    { Qv::TitleBarText::Practical, tr("Practical") },
    { Qv::TitleBarText::Verbose, tr("Verbose") },
    { Qv::TitleBarText::Custom, tr("Custom") }
};

const QMap<Qv::WindowResizeMode, QString> QVOptionsDialog::mapWindowResizeMode =
{
    { Qv::WindowResizeMode::Never, tr("Never") },
    { Qv::WindowResizeMode::WhenLaunching, tr("When launching") },
    { Qv::WindowResizeMode::WhenOpeningImages, tr("When opening images") }
};

template <typename TEnum>
static void populateComboBox(QComboBox *comboBox, const QMap<TEnum, QString> &values)
{
    comboBox->clear();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
    {
        comboBox->addItem(it.value(), static_cast<int>(it.key()));
    }
}

void QVOptionsDialog::populateComboBoxes()
{
    populateComboBox(ui->titlebarComboBox, mapTitleBarText);

    populateComboBox(ui->windowResizeComboBox, mapWindowResizeMode);

    populateComboBox(ui->afterMatchingSizeComboBox, mapAfterMatchingSize);

    populateComboBox(ui->cropModeComboBox, mapFitMode);

    populateComboBox(ui->colorSpaceConversionComboBox, mapColorSpaceConversion);

    populateComboBox(ui->sortComboBox, mapSortMode);

    populateComboBox(ui->preloadingComboBox, mapPreloadMode);

    populateComboBox(ui->afterDeletionComboBox, mapAfterDelete);
}
