#ifndef QVGRAPHICSVIEW_H
#define QVGRAPHICSVIEW_H

#include "qvnamespace.h"
#include "qvimagecore.h"
#include "axislocker.h"
#include "logicalpixelfitter.h"
#include "scrollhelper.h"
#include <optional>
#include <QGraphicsView>
#include <QImageReader>
#include <QMimeData>
#include <QDir>
#include <QTimer>
#include <QFileInfo>

class MainWindow;

class QVGraphicsView : public QGraphicsView
{
    Q_OBJECT

public:
    QVGraphicsView(QWidget *parent = nullptr);

    struct SwipeData
    {
        int totalDelta;
        bool triggeredAction;
    };

    QMimeData* getMimeData() const;
    void loadMimeData(const QMimeData *mimeData);
    void loadFile(const QString &fileName, const QString &baseDir = "");

    void reloadFile();

    void zoomIn();

    void zoomOut();

    void zoomRelative(const qreal relativeLevel, const std::optional<QPoint> &mousePos = {});

    void zoomAbsolute(const qreal absoluteLevel, const std::optional<QPoint> &targetPos = {}, const bool isApplyingCalculation = false);

    const std::optional<Qv::CalculatedZoomMode> &getCalculatedZoomMode() const;
    void setCalculatedZoomMode(const std::optional<Qv::CalculatedZoomMode> &value, const bool isNavigating = false, const std::optional<QPoint> &mousePos = {});

    bool getNavigationResetsZoom() const { return navigationResetsZoom; }
    void setNavigationResetsZoom(const bool value);

    Qv::SortMode getSortMode() const { return imageCore.getSortMode(); }
    void setSortMode(const Qv::SortMode mode) { imageCore.setSortMode(mode); }
    bool getSortDescending() const { return imageCore.getSortDescending(); }
    void setSortDescending(const bool descending) { imageCore.setSortDescending(descending); }

    void applyExpensiveScaling();
    void removeExpensiveScaling();

    void recalculateZoom();

    void centerImage();

    void setCursorVisible(const bool visible);

    const QJsonObject getSessionState() const;

    void loadSessionState(const QJsonObject &state);

    void setLoadIsFromSessionRestore(const bool value);

    void goToFile(const Qv::GoToFileMode mode, const int index = 0);

    void settingsUpdated(const bool isInitialLoad);

    void closeImage(const bool stayInDir = false);
    void jumpToNextFrame();
    void jumpToPreviousFrame();
    void setPaused(const bool &desiredState);
    void setSpeed(const int &desiredSpeed);
    void rotateImage(const int relativeAngle);
    void mirrorImage();
    void flipImage();
    void resetTransformation();

    void fitOrConstrainImage();

    QSizeF getEffectiveOriginalSize() const;

    LogicalPixelFitter getPixelFitter() const;

    const QVImageCore::FileDetails& getCurrentFileDetails() const { return imageCore.getCurrentFileDetails(); }
    const QMovie& getLoadedMovie() const { return imageCore.getLoadedMovie(); }
    qreal getZoomLevel() const { return zoomLevel; }

    int getFitOverscan() const { return fitOverscan; }

signals:
    void cancelSlideshow();

    void fileChanged(const bool isRestoringState);

    void zoomLevelChanged();

    void calculatedZoomModeChanged();

    void navigationResetsZoomChanged();

    void sortParametersChanged();

protected:
    void wheelEvent(QWheelEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

    void paintEvent(QPaintEvent *event) override;

    void dropEvent(QDropEvent *event) override;

    void dragEnterEvent(QDragEnterEvent *event) override;

    void dragMoveEvent(QDragMoveEvent *event) override;

    void dragLeaveEvent(QDragLeaveEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseDoubleClickEvent(QMouseEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;

    bool event(QEvent *event) override;

    void focusInEvent(QFocusEvent *event) override;

    void focusOutEvent(QFocusEvent *event) override;

    void executeClickAction(const Qv::ViewportClickAction action, const QPoint mousePos);

    void executeDragAction(const Qv::ViewportDragAction action, const QPoint delta, bool &isMovingWindow);

    void executeScrollAction(const Qv::ViewportScrollAction action, const QPoint delta, const QPoint mousePos, const bool hasShiftModifier);

    bool isSmoothScalingRequested() const;

    bool isExpensiveScalingRequested() const;

    void matchContentCenter(const QRect target);

    std::optional<Qv::GoToFileMode> getNavigationRegion(const QPoint mousePos) const;

    QRect getContentRect() const;

    QRect getUsableViewportRect(const bool addOverscan = false) const;

    void setTransformScale(const qreal absoluteScale);

    void setTransformWithNormalization(const QTransform &matrix);

    QTransform getUnspecializedTransform() const;

    QTransform normalizeTransformOrigin(const QTransform &matrix, const QSizeF &pixmapSize) const;

    qreal getDpiAdjustment() const;

    void handleDpiAdjustmentChange();

    void handleSmoothScalingChange();

    int getRtlFlip() const;

    void cancelTurboNav();

    MainWindow* getMainWindow() const;

private slots:
    void animatedFrameChanged(QRect rect);

    void beforeLoad();

    void postLoad();

private:
    QGraphicsPixmapItem *loadedPixmapItem;

    Qv::SmoothScalingMode smoothScalingMode {Qv::SmoothScalingMode::Disabled};
    std::optional<qreal> smoothScalingLimit;
    bool expensiveScalingAboveWindowSize {false};
    std::optional<qreal> fitZoomLimit;
    int fitOverscan {0};
    bool zoomToCursor {true};
    bool useOneToOnePixelSizing {true};
    bool constrainImagePosition {true};
    bool constrainToCenterWhenSmaller {true};
    bool disableDelayedConstraint {false};
    Qv::CalculatedZoomMode defaultCalculatedZoomMode {Qv::CalculatedZoomMode::ZoomToFit};
    qreal zoomMultiplier {1.25};

    bool enableNavigationRegions {false};
    Qv::ViewportClickAction doubleClickAction {Qv::ViewportClickAction::None};
    Qv::ViewportClickAction altDoubleClickAction {Qv::ViewportClickAction::None};
    Qv::ViewportDragAction dragAction {Qv::ViewportDragAction::None};
    Qv::ViewportDragAction altDragAction {Qv::ViewportDragAction::None};
    Qv::ViewportClickAction middleClickAction {Qv::ViewportClickAction::None};
    Qv::ViewportClickAction altMiddleClickAction {Qv::ViewportClickAction::None};
    Qv::ClickOrDrag middleButtonMode {Qv::ClickOrDrag::Click};
    Qv::ViewportDragAction middleDragAction {Qv::ViewportDragAction::None};
    Qv::ViewportDragAction altMiddleDragAction {Qv::ViewportDragAction::None};
    Qv::ViewportScrollAction verticalScrollAction {Qv::ViewportScrollAction::None};
    Qv::ViewportScrollAction horizontalScrollAction {Qv::ViewportScrollAction::None};
    Qv::ViewportScrollAction altVerticalScrollAction {Qv::ViewportScrollAction::None};
    Qv::ViewportScrollAction altHorizontalScrollAction {Qv::ViewportScrollAction::None};
    bool scrollActionCooldown {false};

    std::optional<Qv::CalculatedZoomMode> calculatedZoomMode;
    bool globalNavigationResetsZoom {true};
    bool navigationResetsZoom {true};
    bool loadIsFromSessionRestore {false};
    qreal zoomLevel {1.0};
    qreal appliedDpiAdjustment {1.0};
    qreal appliedExpensiveScaleZoomLevel {0.0};
    std::optional<QPoint> lastZoomEventPos;
    QPointF lastZoomRoundingError;
    bool isCursorAutoHideFullscreenEnabled {true};
    bool isCursorVisible {true};
    QRect lastImageContentRect;

    QVImageCore imageCore {this};

    QTimer *expensiveScaleTimer;
    QTimer *constrainBoundsTimer;
    QTimer *hideCursorTimer;

    ScrollHelper *scrollHelper;
    AxisLocker scrollAxisLocker;
    Qt::MouseButton pressedMouseButton {Qt::MouseButton::NoButton};
    Qt::KeyboardModifiers mousePressModifiers {Qt::KeyboardModifier::NoModifier};
    bool isDelayingDrag {false};
    bool isLastMousePosDubious {false};
    QPoint lastMousePos;
    QElapsedTimer lastFocusIn;

    std::optional<Qv::GoToFileMode> turboNavMode;
    QList<QKeySequence> navPrevShortcuts;
    QList<QKeySequence> navNextShortcuts;
    QList<QKeySequence> navRandomShortcuts;
    QElapsedTimer lastTurboNav;
    QElapsedTimer lastTurboNavKeyPress;
    int turboNavInterval {0};

    const int startDragDistance {3};
};
Q_DECLARE_METATYPE(QVGraphicsView::SwipeData)
#endif // QVGRAPHICSVIEW_H
