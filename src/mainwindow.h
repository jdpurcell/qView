#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "qvinfodialog.h"
#include "qvimagecore.h"
#include "qvgraphicsview.h"
#include "openwith.h"

#include <QMainWindow>
#include <QShortcut>
#include <QNetworkAccessManager>
#include <QStack>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    struct DeletedPaths
    {
        QString pathInTrash;
        QString previousPath;
    };

    struct ViewportPosition
    {
        int widgetY;
        int obscuredHeight;
    };

    explicit MainWindow(QWidget *parent = nullptr, const QJsonObject &windowSessionState = {});
    ~MainWindow() override;

    void requestPopulateOpenWithMenu();

    void populateOpenWithMenu(const QList<OpenWith::OpenWithItem> openWithItems);

    void refreshProperties();

    void buildWindowTitle();

    void updateWindowFilePath();

    void updateMenuBarVisible();

    bool getWindowOnTop() const;

    bool getTitlebarHidden() const;

    void setTitlebarHidden(const bool shouldHide);

    void setWindowSize(const bool isReapplying = false, const bool isExplicitRequest = false);

    bool getIsPixmapLoaded() const;

    void setJustLaunchedWithImage(bool value);

    QScreen *screenContaining(const QRect &rect);

    const QJsonObject getSessionState() const;

    void loadSessionState(const QJsonObject &state, const bool isInitialPhase);

    void openRecent(int i);

    void openUrl(const QUrl &url);

    void pickUrl();

    void reloadFile();

    void openContainingFolder();

    void openWith(const OpenWith::OpenWithItem &exec);

    void showFileInfo();

    void askDeleteFile(bool permanent = false);

    void deleteFile(bool permanent);

    void undoDelete();

    void copy();

    void paste();

    void rename();

    void zoomIn();

    void zoomOut();

    void zoomCustom();

    void originalSize();

    void setZoomToFit(const bool value);

    void setFillWindow(const bool value);

    void setNavigationResetsZoom(const bool value);

    void setSortMode(const Qv::SortMode mode);

    void setSortDescending(const bool descending);

    void rotateRight();

    void rotateLeft();

    void mirror();

    void flip();

    void resetTransformation();

    void firstFile();

    void previousFile();

    void nextFile();

    void lastFile();

    void randomFile();
    void previousRandomFile();

    void saveFrameAs();

    void pause();

    void nextFrame();

    void previousFrame();

    void decreaseSpeed();

    void resetSpeed();

    void increaseSpeed();

    void toggleFullScreen();

    void toggleWindowOnTop();

    void toggleTitlebarHidden();

    int getTitlebarOverlap() const;

    ViewportPosition getViewportPosition() const;

    const QVImageCore::FileDetails& getCurrentFileDetails() const { return graphicsView->getCurrentFileDetails(); }

    qint64 getLastActivatedTimestamp() const { return lastActivated.msecsSinceReference(); }

    bool getIsClosing() const { return isClosing; }

public slots:
    void openFile(const QString &fileName, const QString &baseDir = "");

    void toggleSlideshow();

    void slideshowAction();

    void cancelSlideshow();

    void fileChanged(const bool isRestoringState);

    void zoomLevelChanged();

    void syncCalculatedZoomMode();

    void syncNavigationResetsZoom();

    void syncSortParameters();

    void disableActions();

protected:
    bool event(QEvent *event) override;

    void contextMenuEvent(QContextMenuEvent *event) override;

    void showEvent(QShowEvent *event) override;

    void closeEvent(QCloseEvent *event) override;

    void changeEvent(QEvent *event) override;

    void paintEvent(QPaintEvent *event) override;

    void fullscreenChanged();

    void pauseChanged();

protected slots:
    void settingsUpdated();
    void shortcutsUpdated();

private:
    Ui::MainWindow *ui;
    QVGraphicsView *graphicsView;

    QMenu *contextMenu;
    QMenu *virtualMenu;

    QTimer *slideshowTimer;
    QTimer *zoomTitlebarUpdateTimer;

    QShortcut *escShortcut;

    QVInfoDialog *info;

    QColor customBackgroundColor;
    bool checkerboardBackground {false};
    bool menuBarEnabled {false};

    QJsonObject sessionStateToLoad;
    bool justLaunchedWithImage {false};
    bool isClosing {false};
    QElapsedTimer lastActivated;

    Qt::WindowStates storedWindowState {Qt::WindowNoState};
    bool storedTitlebarHidden {false};
    bool slideshowSetOnTopFlag {false};

    QNetworkAccessManager networkAccessManager;

    QStack<DeletedPaths> lastDeletedFiles;

    QTimer *populateOpenWithTimer;
    QFutureWatcher<QList<OpenWith::OpenWithItem>> openWithFutureWatcher;
};

#endif // MAINWINDOW_H
