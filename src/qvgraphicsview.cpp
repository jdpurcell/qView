#include "qvgraphicsview.h"
#include "qvapplication.h"
#include "qvinfodialog.h"
#include "qvcocoafunctions.h"
#include <QWheelEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QSettings>
#include <QMessageBox>
#include <QMovie>
#include <QtMath>
#include <QGestureEvent>
#include <QScrollBar>

QVGraphicsView::QVGraphicsView(QWidget *parent) : QGraphicsView(parent)
{
    // GraphicsView setup
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    viewport()->setAutoFillBackground(false);
    viewport()->setMouseTracking(true);
    grabGesture(Qt::PinchGesture);

    // Scene setup
    auto *scene = new QGraphicsScene(-1000000.0, -1000000.0, 2000000.0, 2000000.0, this);
    setScene(scene);

    scrollHelper = new ScrollHelper(this,
        [this](ScrollHelper::Parameters &p)
        {
            p.contentRect = getContentRect().toRect();
            p.usableViewportRect = getUsableViewportRect();
            p.shouldConstrain = isConstrainedPositioningEnabled;
            p.shouldCenter = isConstrainedSmallCenteringEnabled;
        });

    connect(&imageCore, &QVImageCore::animatedFrameChanged, this, &QVGraphicsView::animatedFrameChanged);
    connect(&imageCore, &QVImageCore::fileChanged, this, &QVGraphicsView::postLoad);

    expensiveScaleTimer = new QTimer(this);
    expensiveScaleTimer->setSingleShot(true);
    expensiveScaleTimer->setInterval(50);
    connect(expensiveScaleTimer, &QTimer::timeout, this, [this]{applyExpensiveScaling();});

    constrainBoundsTimer = new QTimer(this);
    constrainBoundsTimer->setSingleShot(true);
    constrainBoundsTimer->setInterval(500);
    connect(constrainBoundsTimer, &QTimer::timeout, this, [this]{scrollHelper->constrain();});

    emitZoomLevelChangedTimer = new QTimer(this);
    emitZoomLevelChangedTimer->setSingleShot(true);
    emitZoomLevelChangedTimer->setInterval(50);
    connect(emitZoomLevelChangedTimer, &QTimer::timeout, this, [this]{emit zoomLevelChanged();});

    hideCursorTimer = new QTimer(this);
    hideCursorTimer->setSingleShot(true);
    hideCursorTimer->setInterval(1000);
    connect(hideCursorTimer, &QTimer::timeout, this, [this]{setCursorVisible(false);});

    loadedPixmapItem = new QGraphicsPixmapItem();
    scene->addItem(loadedPixmapItem);

    // Connect to settings signal
    connect(&qvApp->getSettingsManager(), &SettingsManager::settingsUpdated, this, &QVGraphicsView::settingsUpdated);
    settingsUpdated();
}

// Events

void QVGraphicsView::resizeEvent(QResizeEvent *event)
{
    if (const auto mainWindow = getMainWindow())
        if (mainWindow->getIsClosing())
            return;

    QGraphicsView::resizeEvent(event);
    fitOrConstrainImage();
}

void QVGraphicsView::paintEvent(QPaintEvent *event)
{
    // This is the most reliable place to detect DPI changes. QWindow::screenChanged()
    // doesn't detect when the DPI is changed on the current monitor, for example.
    handleDpiAdjustmentChange();

    QGraphicsView::paintEvent(event);
}

void QVGraphicsView::dropEvent(QDropEvent *event)
{
    QGraphicsView::dropEvent(event);
    loadMimeData(event->mimeData());
}

void QVGraphicsView::dragEnterEvent(QDragEnterEvent *event)
{
    QGraphicsView::dragEnterEvent(event);
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}

void QVGraphicsView::dragMoveEvent(QDragMoveEvent *event)
{
    QGraphicsView::dragMoveEvent(event);
    event->acceptProposedAction();
}

void QVGraphicsView::dragLeaveEvent(QDragLeaveEvent *event)
{
    QGraphicsView::dragLeaveEvent(event);
    event->accept();
}

void QVGraphicsView::mousePressEvent(QMouseEvent *event)
{
    const auto initializeDrag = [this, event]() {
        pressedMouseButton = event->button();
        mousePressModifiers = event->modifiers();
        setCursorVisible(true);
        viewport()->setCursor(Qt::ClosedHandCursor);
        lastMousePos = event->pos();
    };

    if (event->button() == Qt::LeftButton)
    {
        const bool isAltAction = event->modifiers().testFlag(Qt::ControlModifier);
        if ((isAltAction ? altDragAction : dragAction) != Qv::ViewportDragAction::None)
        {
            initializeDrag();
        }
        return;
    }
    else if (event->button() == Qt::MouseButton::MiddleButton)
    {
        const bool isAltAction = event->modifiers().testFlag(Qt::ControlModifier);
        if (middleButtonMode == Qv::ClickOrDrag::Click)
        {
            executeClickAction(isAltAction ? altMiddleClickAction : middleClickAction);
        }
        else if (middleButtonMode == Qv::ClickOrDrag::Drag &&
            (isAltAction ? altMiddleDragAction : middleDragAction) != Qv::ViewportDragAction::None)
        {
            initializeDrag();
        }
        return;
    }
    else if (event->button() == Qt::MouseButton::BackButton)
    {
        goToFile(GoToFileMode::previous);
        return;
    }
    else if (event->button() == Qt::MouseButton::ForwardButton)
    {
        goToFile(GoToFileMode::next);
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void QVGraphicsView::mouseReleaseEvent(QMouseEvent *event)
{
    if (pressedMouseButton != Qt::NoButton)
    {
        pressedMouseButton = Qt::NoButton;
        mousePressModifiers = Qt::NoModifier;
        setCursorVisible(true);
        viewport()->setCursor(Qt::ArrowCursor);
        scrollHelper->constrain();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void QVGraphicsView::mouseMoveEvent(QMouseEvent *event)
{
    setCursorVisible(true);

    if (pressedMouseButton != Qt::NoButton)
    {
        const bool isAltAction = mousePressModifiers.testFlag(Qt::ControlModifier);
        const Qv::ViewportDragAction targetAction =
            pressedMouseButton == Qt::LeftButton ? (isAltAction ? altDragAction : dragAction) :
            pressedMouseButton == Qt::MiddleButton ? (isAltAction ? altMiddleDragAction : middleDragAction) :
            Qv::ViewportDragAction::None;
        const QPoint delta = event->pos() - lastMousePos;
        bool isMovingWindow = false;
        executeDragAction(targetAction, delta, isMovingWindow);
        if (!isMovingWindow)
            lastMousePos = event->pos();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void QVGraphicsView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MouseButton::LeftButton)
    {
        const bool isAltAction = event->modifiers().testFlag(Qt::ControlModifier);
        executeClickAction(isAltAction ? altDoubleClickAction : doubleClickAction);
        return;
    }

    QGraphicsView::mouseDoubleClickEvent(event);
}

bool QVGraphicsView::event(QEvent *event)
{
    if (event->type() == QEvent::Gesture)
    {
        QGestureEvent *gestureEvent = static_cast<QGestureEvent*>(event);
        if (QGesture *pinch = gestureEvent->gesture(Qt::PinchGesture))
        {
            QPinchGesture *pinchGesture = static_cast<QPinchGesture*>(pinch);
            if (pinchGesture->changeFlags() & QPinchGesture::ScaleFactorChanged)
            {
                const QPoint hotPoint = mapFromGlobal(pinchGesture->hotSpot().toPoint());
                zoomRelative(pinchGesture->scaleFactor(), hotPoint);
            }
            return true;
        }
    }
    else if (event->type() == QEvent::ShortcutOverride && !turboNavMode.has_value())
    {
        const QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        const ActionManager &actionManager = qvApp->getActionManager();
        if (actionManager.wouldTriggerAction(keyEvent, "previousfile") || actionManager.wouldTriggerAction(keyEvent, "nextfile"))
        {
            // Accept event to override shortcut and deliver as key press instead
            event->accept();
            return true;
        }
    }
    else if (event->type() == QEvent::KeyRelease && turboNavMode.has_value())
    {
        const QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        if (!keyEvent->isAutoRepeat() &&
            (ActionManager::wouldTriggerAction(keyEvent, navPrevShortcuts) || ActionManager::wouldTriggerAction(keyEvent, navNextShortcuts)))
        {
            cancelTurboNav();
        }
    }
    else if (event->type() == QEvent::FocusOut)
    {
        cancelTurboNav();
    }

    return QGraphicsView::event(event);
}

void QVGraphicsView::wheelEvent(QWheelEvent *event)
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    const QPoint eventPos = event->position().toPoint();
#else
    const QPoint eventPos = event->pos();
#endif
    const bool isAltAction = event->modifiers().testFlag(Qt::ControlModifier);
    const Qv::ViewportScrollAction horizontalAction = isAltAction ? altHorizontalScrollAction : horizontalScrollAction;
    const Qv::ViewportScrollAction verticalAction = isAltAction ? altVerticalScrollAction : verticalScrollAction;
    const bool hasHorizontalAction = horizontalAction != Qv::ViewportScrollAction::None;
    const bool hasVerticalAction = verticalAction != Qv::ViewportScrollAction::None;
    if (!hasHorizontalAction && !hasVerticalAction)
        return;
    const QPoint baseDelta =
        hasHorizontalAction && !hasVerticalAction ? QPoint(event->angleDelta().x(), 0) :
        !hasHorizontalAction && hasVerticalAction ? QPoint(0, event->angleDelta().y()) :
        event->angleDelta();
    const QPoint effectiveDelta =
        horizontalAction == verticalAction && Qv::scrollActionIsSelfCompatible(horizontalAction) ? baseDelta :
        scrollAxisLocker.filterMovement(baseDelta, event->phase(), hasHorizontalAction != hasVerticalAction);
    const Qv::ViewportScrollAction effectiveAction =
        effectiveDelta.x() != 0 ? horizontalAction :
        effectiveDelta.y() != 0 ? verticalAction :
        Qv::ViewportScrollAction::None;
    if (effectiveAction == Qv::ViewportScrollAction::None)
        return;
    const bool hasShiftModifier = event->modifiers().testFlag(Qt::ShiftModifier);

    executeScrollAction(effectiveAction, effectiveDelta, eventPos, hasShiftModifier);
}

void QVGraphicsView::keyPressEvent(QKeyEvent *event)
{
    if (turboNavMode.has_value())
    {
        if (ActionManager::wouldTriggerAction(event, navPrevShortcuts) || ActionManager::wouldTriggerAction(event, navNextShortcuts))
        {
            lastTurboNavKeyPress.start();
            return;
        }
    }
    else
    {
        const ActionManager &actionManager = qvApp->getActionManager();
        const bool navPrev = actionManager.wouldTriggerAction(event, "previousfile");
        const bool navNext = actionManager.wouldTriggerAction(event, "nextfile");
        if (navPrev || navNext)
        {
            const GoToFileMode navMode = navPrev ? GoToFileMode::previous : GoToFileMode::next;
            if (event->isAutoRepeat())
            {
                turboNavMode = navMode;
                lastTurboNav.start();
                lastTurboNavKeyPress.start();
                // Remove keyboard shortcuts while turbo navigation is in progress to eliminate any
                // potential overhead. Especially important on macOS which seems to enforce throttling
                // for menu invocations caused by key repeats, which blocks the UI thread (try setting
                // the key repeat rate to max without unbinding the shortcuts - it's really bad).
                navPrevShortcuts = actionManager.getAction("previousfile")->shortcuts();
                navNextShortcuts = actionManager.getAction("nextfile")->shortcuts();
                actionManager.setActionShortcuts("previousfile", {});
                actionManager.setActionShortcuts("nextfile", {});
            }
            goToFile(navMode);
            return;
        }
    }

    // The base class has logic to scroll in response to certain key presses, but we'll
    // handle that ourselves here instead to ensure any bounds constraints are enforced.
    const int scrollXSmallSteps = event->key() == Qt::Key_Left ? -1 : event->key() == Qt::Key_Right ? 1 : 0;
    const int scrollYSmallSteps = event->key() == Qt::Key_Up ? -1 : event->key() == Qt::Key_Down ? 1 : 0;
    const int scrollYLargeSteps = event == QKeySequence::MoveToPreviousPage ? -1 : event == QKeySequence::MoveToNextPage ? 1 : 0;
    if (scrollXSmallSteps != 0 || scrollYSmallSteps != 0 || scrollYLargeSteps != 0)
    {
        const QPoint delta {
            (horizontalScrollBar()->singleStep() * scrollXSmallSteps) * (isRightToLeft() ? -1 : 1),
            (verticalScrollBar()->singleStep() * scrollYSmallSteps) + (verticalScrollBar()->pageStep() * scrollYLargeSteps)
        };
        scrollHelper->move(delta);
        constrainBoundsTimer->start();
        return;
    }

    QGraphicsView::keyPressEvent(event);
}

// Functions

void QVGraphicsView::executeClickAction(const Qv::ViewportClickAction action)
{
    if (action == Qv::ViewportClickAction::ZoomToFit)
    {
        setCalculatedZoomMode(Qv::CalculatedZoomMode::ZoomToFit);
    }
    else if (action == Qv::ViewportClickAction::FillWindow)
    {
        setCalculatedZoomMode(Qv::CalculatedZoomMode::FillWindow);
    }
    else if (action == Qv::ViewportClickAction::OriginalSize)
    {
        setCalculatedZoomMode(Qv::CalculatedZoomMode::OriginalSize);
    }
    else if (action == Qv::ViewportClickAction::ToggleFullScreen)
    {
        if (const auto mainWindow = getMainWindow())
            mainWindow->toggleFullScreen();
    }
    else if (action == Qv::ViewportClickAction::ToggleTitlebarHidden)
    {
        if (const auto mainWindow = getMainWindow())
            mainWindow->toggleTitlebarHidden();
    }
}

void QVGraphicsView::executeDragAction(const Qv::ViewportDragAction action, const QPoint delta, bool &isMovingWindow)
{
    if (action == Qv::ViewportDragAction::Pan)
    {
        scrollHelper->move(QPointF(-delta.x() * (isRightToLeft() ? -1 : 1), -delta.y()));
    }
    else if (action == Qv::ViewportDragAction::MoveWindow)
    {
        window()->move(window()->pos() + delta);
        isMovingWindow = true;
    }
}

void QVGraphicsView::executeScrollAction(const Qv::ViewportScrollAction action, const QPoint delta, const QPoint mousePos, const bool hasShiftModifier)
{
    const int deltaPerWheelStep = 120;
    const int rtlFlip = isRightToLeft() ? -1 : 1;

    const auto getUniAxisDelta = [delta, rtlFlip]() {
        return
            delta.x() != 0 && delta.y() == 0 ? delta.x() * rtlFlip :
            delta.x() == 0 && delta.y() != 0 ? delta.y() :
            0;
    };

    if (action == Qv::ViewportScrollAction::Pan)
    {
        const qreal scrollDivisor = 2.0; // To make scrolling less sensitive
        qreal scrollX = -delta.x() * rtlFlip / scrollDivisor;
        qreal scrollY = -delta.y() / scrollDivisor;

        if (hasShiftModifier)
            std::swap(scrollX, scrollY);

        scrollHelper->move(QPointF(scrollX, scrollY));
        constrainBoundsTimer->start();
    }
    else if (action == Qv::ViewportScrollAction::Zoom)
    {
        if (!getCurrentFileDetails().isPixmapLoaded)
            return;

        const int uniAxisDelta = getUniAxisDelta();
        const qreal fractionalWheelSteps = qFabs(uniAxisDelta) / deltaPerWheelStep;
        const qreal zoomAmountPerWheelStep = zoomMultiplier - 1.0;
        qreal zoomFactor = 1.0 + (fractionalWheelSteps * zoomAmountPerWheelStep);

        if (uniAxisDelta < 0)
            zoomFactor = qPow(zoomFactor, -1);

        if (isCursorVisible)
            setCursorVisible(true);

        zoomRelative(zoomFactor, mousePos);
    }
    else if (action == Qv::ViewportScrollAction::Navigate)
    {
        SwipeData swipeData = scrollAxisLocker.getCustomData().value<SwipeData>();
        if (swipeData.triggeredAction && scrollActionCooldown)
            return;
        swipeData.totalDelta += getUniAxisDelta();
        if (qAbs(swipeData.totalDelta) >= deltaPerWheelStep)
        {
            if (swipeData.totalDelta < 0)
                goToFile(GoToFileMode::next);
            else
                goToFile(GoToFileMode::previous);
            swipeData.triggeredAction = true;
            swipeData.totalDelta %= deltaPerWheelStep;
        }
        scrollAxisLocker.setCustomData(QVariant::fromValue(swipeData));
    }
}

QMimeData *QVGraphicsView::getMimeData() const
{
    auto *mimeData = new QMimeData();
    if (!getCurrentFileDetails().isPixmapLoaded)
        return mimeData;

    mimeData->setUrls({QUrl::fromLocalFile(imageCore.getCurrentFileDetails().fileInfo.absoluteFilePath())});
    mimeData->setImageData(imageCore.getLoadedPixmap().toImage());
    return mimeData;
}

void QVGraphicsView::loadMimeData(const QMimeData *mimeData)
{
    if (mimeData == nullptr)
        return;

    if (!mimeData->hasUrls())
        return;

    const QList<QUrl> urlList = mimeData->urls();

    bool first = true;
    for (const auto &url : urlList)
    {
        if (first)
        {
            loadFile(url.toString());
            emit cancelSlideshow();
            first = false;
            continue;
        }
        QVApplication::openFile(url.toString());
    }
}

void QVGraphicsView::loadFile(const QString &fileName)
{
    imageCore.loadFile(fileName);
}

void QVGraphicsView::reloadFile()
{
    if (!getCurrentFileDetails().isPixmapLoaded)
        return;

    imageCore.loadFile(getCurrentFileDetails().fileInfo.absoluteFilePath(), true);
}

void QVGraphicsView::postLoad()
{
    scrollHelper->cancelAnimation();

    // Set the pixmap to the new image and reset the transform's scale to a known value
    removeExpensiveScaling();

    if (!loadIsFromSessionRestore)
    {
        if (navigationResetsZoom && calculatedZoomMode != defaultCalculatedZoomMode)
            setCalculatedZoomMode(defaultCalculatedZoomMode);
        else
            fitOrConstrainImage();
    }

    expensiveScaleTimer->start();

    qvApp->getActionManager().addFileToRecentsList(getCurrentFileDetails().fileInfo);

    emit fileChanged(loadIsFromSessionRestore);

    loadIsFromSessionRestore = false;

    if (turboNavMode.has_value())
    {
        const qint64 navDelay = qMax(turboNavInterval - lastTurboNav.elapsed(), 0LL);
        QTimer::singleShot(navDelay, this, [this]() {
            if (!turboNavMode.has_value())
                return;
            if (lastTurboNavKeyPress.elapsed() >= qMax(qvApp->keyboardAutoRepeatInterval() * 1.5, 250.0))
            {
                // Backup mechanism in case we somehow stop receiving key presses and aren't
                // notified of it in some other way (e.g. key release, lost focus), as can happen
                // in macOS if the menu bar gets clicked on while navigation is in progress.
                cancelTurboNav();
                return;
            }
            lastTurboNav.start();
            goToFile(turboNavMode.value());
        });
    }
}

void QVGraphicsView::zoomIn()
{
    zoomRelative(zoomMultiplier);
}

void QVGraphicsView::zoomOut()
{
    zoomRelative(qPow(zoomMultiplier, -1));
}

void QVGraphicsView::zoomRelative(const qreal relativeLevel, const std::optional<QPoint> &mousePos)
{
    const qreal absoluteLevel = zoomLevel * relativeLevel;

    if (absoluteLevel >= 500 || absoluteLevel <= 0.01)
        return;

    zoomAbsolute(absoluteLevel, mousePos);
}

void QVGraphicsView::zoomAbsolute(const qreal absoluteLevel, const std::optional<QPoint> &mousePos, const bool isApplyingCalculation)
{
    if (!isApplyingCalculation || !Qv::calculatedZoomModeIsSticky(calculatedZoomMode.value()))
        setCalculatedZoomMode({});

    const std::optional<QPoint> pos = !mousePos.has_value() ? std::nullopt : isCursorZoomEnabled && isCursorVisible ? mousePos : getUsableViewportRect().center();
    if (pos != lastZoomEventPos)
    {
        lastZoomEventPos = pos;
        lastZoomRoundingError = QPointF();
    }
    const QPointF scenePos = pos.has_value() ? mapToScene(pos.value()) - lastZoomRoundingError : QPointF();

    if (appliedExpensiveScaleZoomLevel != 0.0)
    {
        const qreal baseTransformScale = 1.0 / devicePixelRatioF();
        const qreal relativeLevel = absoluteLevel / appliedExpensiveScaleZoomLevel;
        setTransformScale(baseTransformScale * relativeLevel);
    }
    else
    {
        setTransformScale(absoluteLevel * appliedDpiAdjustment);
    }
    zoomLevel = absoluteLevel;

    scrollHelper->cancelAnimation();

    if (pos.has_value())
    {
        const QPointF move = mapFromScene(scenePos) - pos.value();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + (move.x() * (isRightToLeft() ? -1 : 1)));
        verticalScrollBar()->setValue(verticalScrollBar()->value() + move.y());
        lastZoomRoundingError = mapToScene(pos.value()) - scenePos;
        constrainBoundsTimer->start();
    }
    else if (!loadIsFromSessionRestore)
    {
        centerImage();
    }

    handleSmoothScalingChange();

    emitZoomLevelChangedTimer->start();
}

const std::optional<Qv::CalculatedZoomMode> &QVGraphicsView::getCalculatedZoomMode() const
{
    return calculatedZoomMode;
}

void QVGraphicsView::setCalculatedZoomMode(const std::optional<Qv::CalculatedZoomMode> &value)
{
    if (calculatedZoomMode == value)
    {
        if (calculatedZoomMode.has_value())
            centerImage();
        return;
    }

    calculatedZoomMode = value;
    if (calculatedZoomMode.has_value())
        recalculateZoom();

    emit calculatedZoomModeChanged();
}

bool QVGraphicsView::getNavigationResetsZoom() const
{
    return navigationResetsZoom;
}

void QVGraphicsView::setNavigationResetsZoom(const bool value)
{
    if (navigationResetsZoom == value)
        return;

    navigationResetsZoom = value;

    emit navigationResetsZoomChanged();
}

void QVGraphicsView::applyExpensiveScaling()
{
    if (!isExpensiveScalingRequested())
        return;

    // Calculate scaled resolution
    const qreal dpiAdjustment = getDpiAdjustment();
    const QSizeF mappedSize = QSizeF(getCurrentFileDetails().loadedPixmapSize) * zoomLevel * dpiAdjustment * devicePixelRatioF();

    // Set image to scaled version
    loadedPixmapItem->setPixmap(imageCore.scaleExpensively(mappedSize));

    // Set appropriate scale factor
    const qreal newTransformScale = 1.0 / devicePixelRatioF();
    setTransformScale(newTransformScale);
    appliedDpiAdjustment = dpiAdjustment;
    appliedExpensiveScaleZoomLevel = zoomLevel;
}

void QVGraphicsView::removeExpensiveScaling()
{
    // Return to original size
    if (getCurrentFileDetails().isMovieLoaded)
        loadedPixmapItem->setPixmap(getLoadedMovie().currentPixmap());
    else
        loadedPixmapItem->setPixmap(getLoadedPixmap());

    // Set appropriate scale factor
    const qreal dpiAdjustment = getDpiAdjustment();
    const qreal newTransformScale = zoomLevel * dpiAdjustment;
    setTransformScale(newTransformScale);
    appliedDpiAdjustment = dpiAdjustment;
    appliedExpensiveScaleZoomLevel = 0.0;
}

void QVGraphicsView::animatedFrameChanged(QRect rect)
{
    Q_UNUSED(rect)

    if (isExpensiveScalingRequested())
    {
        applyExpensiveScaling();
    }
    else
    {
        loadedPixmapItem->setPixmap(getLoadedMovie().currentPixmap());
    }
}

void QVGraphicsView::recalculateZoom()
{
    if (!getCurrentFileDetails().isPixmapLoaded || !calculatedZoomMode.has_value())
        return;

    QSizeF effectiveImageSize = getEffectiveOriginalSize();
    QSize viewSize = getUsableViewportRect(true).size();

    if (viewSize.isEmpty())
        return;

    qreal fitXRatio = viewSize.width() / effectiveImageSize.width();
    qreal fitYRatio = viewSize.height() / effectiveImageSize.height();

    qreal targetRatio;

    // Each mode will check if the rounded image size already produces the desired fit,
    // in which case we can use exactly 1.0 to avoid unnecessary scaling

    switch (calculatedZoomMode.value()) {
    case Qv::CalculatedZoomMode::ZoomToFit:
        if ((qRound(effectiveImageSize.height()) == viewSize.height() && qRound(effectiveImageSize.width()) <= viewSize.width()) ||
            (qRound(effectiveImageSize.width()) == viewSize.width() && qRound(effectiveImageSize.height()) <= viewSize.height()))
        {
            targetRatio = 1.0;
        }
        else
        {
            QSize xRatioSize = (effectiveImageSize * fitXRatio * devicePixelRatioF()).toSize();
            QSize yRatioSize = (effectiveImageSize * fitYRatio * devicePixelRatioF()).toSize();
            QSize maxSize = (QSizeF(viewSize) * devicePixelRatioF()).toSize();
            // If the fit ratios are extremely close, it's possible that both are sufficient to
            // contain the image, but one results in the opposing dimension getting rounded down
            // to just under the view size, so use the larger of the two ratios in that case.
            if (xRatioSize.boundedTo(maxSize) == xRatioSize && yRatioSize.boundedTo(maxSize) == yRatioSize)
                targetRatio = qMax(fitXRatio, fitYRatio);
            else
                targetRatio = qMin(fitXRatio, fitYRatio);
        }
        break;
    case Qv::CalculatedZoomMode::FillWindow:
        if ((qRound(effectiveImageSize.height()) == viewSize.height() && qRound(effectiveImageSize.width()) >= viewSize.width()) ||
            (qRound(effectiveImageSize.width()) == viewSize.width() && qRound(effectiveImageSize.height()) >= viewSize.height()))
        {
            targetRatio = 1.0;
        }
        else
        {
            targetRatio = qMax(fitXRatio, fitYRatio);
        }
        break;
    case Qv::CalculatedZoomMode::OriginalSize:
        targetRatio = 1.0;
        break;
    }

    if (fitZoomLimit.has_value() && targetRatio > fitZoomLimit.value())
        targetRatio = fitZoomLimit.value();

    zoomAbsolute(targetRatio, {}, true);
}

void QVGraphicsView::centerImage()
{
    const QRect viewRect = getUsableViewportRect();
    const QRect contentRect = getContentRect().toRect();
    const int hOffset = isRightToLeft() ?
        horizontalScrollBar()->minimum() + horizontalScrollBar()->maximum() - contentRect.left() :
        contentRect.left();
    const int vOffset = contentRect.top() - viewRect.top();
    const int hOverflow = contentRect.width() - viewRect.width();
    const int vOverflow = contentRect.height() - viewRect.height();

    horizontalScrollBar()->setValue(hOffset + (hOverflow / (isRightToLeft() ? -2 : 2)));
    verticalScrollBar()->setValue(vOffset + (vOverflow / 2));

    scrollHelper->cancelAnimation();
}

void QVGraphicsView::setCursorVisible(const bool visible)
{
    const bool autoHideCursor = isCursorAutoHideFullscreenEnabled && window()->isFullScreen();
    if (visible)
    {
        if (autoHideCursor && pressedMouseButton == Qt::NoButton)
            hideCursorTimer->start();
        else
            hideCursorTimer->stop();

        if (isCursorVisible) return;

        window()->setCursor(Qt::ArrowCursor);
        viewport()->setCursor(Qt::ArrowCursor);
        isCursorVisible = true;
    }
    else
    {
        if (!isCursorVisible) return;

        window()->setCursor(Qt::BlankCursor);
        viewport()->setCursor(Qt::BlankCursor);
        isCursorVisible = false;
    }
}

const QJsonObject QVGraphicsView::getSessionState() const
{
    QJsonObject state;

    const QTransform transform = getTransformWithNoScaling();
    const QJsonArray transformValues {
        static_cast<int>(transform.m11()),
        static_cast<int>(transform.m22()),
        static_cast<int>(transform.m21()),
        static_cast<int>(transform.m12())
    };
    state["transform"] = transformValues;

    state["zoomLevel"] = zoomLevel;

    state["hScroll"] = horizontalScrollBar()->value();

    state["vScroll"] = verticalScrollBar()->value();

    state["navResetsZoom"] = navigationResetsZoom;

    if (calculatedZoomMode.has_value())
        state["calcZoomMode"] = static_cast<int>(calculatedZoomMode.value());

    return state;
}

void QVGraphicsView::loadSessionState(const QJsonObject &state)
{
    const QJsonArray transformValues = state["transform"].toArray();
    const QTransform transform {
        static_cast<double>(transformValues.at(0).toInt()),
        static_cast<double>(transformValues.at(3).toInt()),
        static_cast<double>(transformValues.at(2).toInt()),
        static_cast<double>(transformValues.at(1).toInt()),
        0,
        0
    };
    setTransform(transform);

    zoomAbsolute(state["zoomLevel"].toDouble());

    horizontalScrollBar()->setValue(state["hScroll"].toInt());

    verticalScrollBar()->setValue(state["vScroll"].toInt());

    navigationResetsZoom = state["navResetsZoom"].toBool();

    calculatedZoomMode = state.contains("calcZoomMode") ? std::make_optional(static_cast<Qv::CalculatedZoomMode>(state["calcZoomMode"].toInt())) : std::nullopt;

    emit navigationResetsZoomChanged();
    emit calculatedZoomModeChanged();
}

void QVGraphicsView::setLoadIsFromSessionRestore(const bool value)
{
    loadIsFromSessionRestore = value;
}

void QVGraphicsView::goToFile(const GoToFileMode &mode, int index)
{
    bool shouldRetryFolderInfoUpdate = false;

    // Update folder info only after a little idle time as an optimization for when
    // the user is rapidly navigating through files.
    if (!getCurrentFileDetails().timeSinceLoaded.isValid() || getCurrentFileDetails().timeSinceLoaded.hasExpired(3000))
    {
        // Make sure the file still exists because if it disappears from the file listing we'll lose
        // track of our index within the folder. Use the static 'exists' method to avoid caching.
        // If we skip updating now, flag it for retry later once we locate a new file.
        if (QFile::exists(getCurrentFileDetails().fileInfo.absoluteFilePath()))
            imageCore.updateFolderInfo();
        else
            shouldRetryFolderInfoUpdate = true;
    }

    const auto &fileList = getCurrentFileDetails().folderFileInfoList;
    if (fileList.isEmpty())
        return;

    int newIndex = getCurrentFileDetails().loadedIndexInFolder;
    int searchDirection = 0;

    switch (mode) {
    case GoToFileMode::constant:
    {
        newIndex = index;
        break;
    }
    case GoToFileMode::first:
    {
        newIndex = 0;
        searchDirection = 1;
        break;
    }
    case GoToFileMode::previous:
    {
        if (newIndex == 0)
        {
            if (isLoopFoldersEnabled)
                newIndex = fileList.size()-1;
            else
                emit cancelSlideshow();
        }
        else
            newIndex--;
        searchDirection = -1;
        break;
    }
    case GoToFileMode::next:
    {
        if (fileList.size()-1 == newIndex)
        {
            if (isLoopFoldersEnabled)
                newIndex = 0;
            else
                emit cancelSlideshow();
        }
        else
            newIndex++;
        searchDirection = 1;
        break;
    }
    case GoToFileMode::last:
    {
        newIndex = fileList.size()-1;
        searchDirection = -1;
        break;
    }
    case GoToFileMode::random:
    {
        if (fileList.size() > 1)
        {
            int randomIndex = QRandomGenerator::global()->bounded(fileList.size()-1);
            newIndex = randomIndex + (randomIndex >= newIndex ? 1 : 0);
        }
        searchDirection = 1;
        break;
    }
    }

    while (searchDirection == 1 && newIndex < fileList.size()-1 && !QFile::exists(fileList.value(newIndex).absoluteFilePath))
        newIndex++;
    while (searchDirection == -1 && newIndex > 0 && !QFile::exists(fileList.value(newIndex).absoluteFilePath))
        newIndex--;

    const QString nextImageFilePath = fileList.value(newIndex).absoluteFilePath;

    if (!QFile::exists(nextImageFilePath) || nextImageFilePath == getCurrentFileDetails().fileInfo.absoluteFilePath())
        return;

    if (shouldRetryFolderInfoUpdate)
    {
        // If the user just deleted a file through qView, closeImage will have been called which empties
        // currentFileDetails.fileInfo. In this case updateFolderInfo can't infer the directory from
        // fileInfo like it normally does, so we'll explicity pass in the folder here.
        imageCore.updateFolderInfo(QFileInfo(nextImageFilePath).path());
    }

    loadFile(nextImageFilePath);
}

void QVGraphicsView::fitOrConstrainImage()
{
    if (calculatedZoomMode.has_value())
        recalculateZoom();
    else
        scrollHelper->constrain(true);
}

bool QVGraphicsView::isSmoothScalingRequested() const
{
    return smoothScalingMode != Qv::SmoothScalingMode::Disabled &&
        (!smoothScalingLimit.has_value() || zoomLevel < smoothScalingLimit.value());
}

bool QVGraphicsView::isExpensiveScalingRequested() const
{
    if (!isSmoothScalingRequested() || smoothScalingMode != Qv::SmoothScalingMode::Expensive || !getCurrentFileDetails().isPixmapLoaded)
        return false;

    // If we are above maximum scaling size
    const QSize contentSize = getContentRect().size().toSize();
    const QSize maxSize = getUsableViewportRect(true).size() * (expensiveScalingAboveWindowSize ? 3 : 1) + QSize(1, 1);
    return contentSize.width() <= maxSize.width() && contentSize.height() <= maxSize.height();
}

QSizeF QVGraphicsView::getEffectiveOriginalSize() const
{
    return getTransformWithNoScaling().mapRect(QRectF(QPoint(), getCurrentFileDetails().loadedPixmapSize)).size() * getDpiAdjustment();
}

QRectF QVGraphicsView::getContentRect() const
{
    return transform().mapRect(loadedPixmapItem->boundingRect());
}

QRect QVGraphicsView::getUsableViewportRect(const bool addOverscan) const
{
#ifdef COCOA_LOADED
    int obscuredHeight = QVCocoaFunctions::getObscuredHeight(window()->windowHandle());
#else
    int obscuredHeight = 0;
#endif
    QRect rect = viewport()->rect();
    rect.setTop(obscuredHeight);
    if (addOverscan)
        rect.adjust(-fitOverscan, -fitOverscan, fitOverscan, fitOverscan);
    return rect;
}

void QVGraphicsView::setTransformScale(qreal value)
{
#ifdef Q_OS_WIN
    // On Windows, the positioning of scaled pixels seems to follow a floor rule rather
    // than rounding, so increase the scale just a hair to cover rounding errors in case
    // the desired scale was targeting an integer pixel boundary.
    value *= 1.0 + std::numeric_limits<double>::epsilon();
#endif
    setTransform(getTransformWithNoScaling().scale(value, value));
}

QTransform QVGraphicsView::getTransformWithNoScaling() const
{
    const QTransform t = transform();
    // Only intended to handle combinations of scaling, mirroring, flipping, and rotation
    // in increments of 90 degrees. A seemingly simpler approach would be to scale the
    // transform by the inverse of its scale factor, but the resulting scale factor may
    // not exactly equal 1 due to floating point rounding errors.
    if (t.type() == QTransform::TxRotate)
        return { 0, t.m12() < 0 ? -1.0 : 1.0, t.m21() < 0 ? -1.0 : 1.0, 0, 0, 0 };
    else
        return { t.m11() < 0 ? -1.0 : 1.0, 0, 0, t.m22() < 0 ? -1.0 : 1.0, 0, 0 };
}

qreal QVGraphicsView::getDpiAdjustment() const
{
    return isOneToOnePixelSizingEnabled ? 1.0 / devicePixelRatioF() : 1.0;
}

void QVGraphicsView::handleDpiAdjustmentChange()
{
    if (appliedDpiAdjustment == getDpiAdjustment())
        return;

    removeExpensiveScaling();

    fitOrConstrainImage();

    expensiveScaleTimer->start();
}

void QVGraphicsView::handleSmoothScalingChange()
{
    loadedPixmapItem->setTransformationMode(isSmoothScalingRequested() ? Qt::SmoothTransformation : Qt::FastTransformation);

    if (isExpensiveScalingRequested())
        expensiveScaleTimer->start();
    else if (appliedExpensiveScaleZoomLevel != 0.0)
        removeExpensiveScaling();
}

void QVGraphicsView::cancelTurboNav()
{
    if (!turboNavMode.has_value())
        return;

    const ActionManager &actionManager = qvApp->getActionManager();
    turboNavMode = {};
    actionManager.setActionShortcuts("previousfile", navPrevShortcuts);
    actionManager.setActionShortcuts("nextfile", navNextShortcuts);
    navPrevShortcuts = {};
    navNextShortcuts = {};
}

MainWindow* QVGraphicsView::getMainWindow() const
{
    return qobject_cast<MainWindow*>(window());
}

void QVGraphicsView::settingsUpdated()
{
    auto &settingsManager = qvApp->getSettingsManager();

    //smooth scaling
    smoothScalingMode = settingsManager.getEnum<Qv::SmoothScalingMode>("smoothscalingmode");

    //scaling two
    expensiveScalingAboveWindowSize = settingsManager.getBoolean("scalingtwoenabled");

    //smooth scaling limit
    smoothScalingLimit = settingsManager.getBoolean("smoothscalinglimitenabled") ? std::make_optional(settingsManager.getInteger("smoothscalinglimitpercent") / 100.0) : std::nullopt;

    //calculatedzoommode
    defaultCalculatedZoomMode = settingsManager.getEnum<Qv::CalculatedZoomMode>("calculatedzoommode");

    //scalefactor
    zoomMultiplier = 1.0 + (settingsManager.getInteger("scalefactor") / 100.0);

    //fit zoom limit
    fitZoomLimit = settingsManager.getBoolean("fitzoomlimitenabled") ? std::make_optional(settingsManager.getInteger("fitzoomlimitpercent") / 100.0) : std::nullopt;

    //fit overscan
    fitOverscan = settingsManager.getInteger("fitoverscan");

    //cursor zoom
    isCursorZoomEnabled = settingsManager.getBoolean("cursorzoom");

    //one-to-one pixel sizing
    isOneToOnePixelSizingEnabled = settingsManager.getBoolean("onetoonepixelsizing");

    //constrained positioning
    isConstrainedPositioningEnabled = settingsManager.getBoolean("constrainimageposition");

    //constrained small centering
    isConstrainedSmallCenteringEnabled = settingsManager.getBoolean("constraincentersmallimage");

    //nav speed
    turboNavInterval = settingsManager.getInteger("navspeed");

    //loop folders
    isLoopFoldersEnabled = settingsManager.getBoolean("loopfoldersenabled");

    //mouse actions
    doubleClickAction = settingsManager.getEnum<Qv::ViewportClickAction>("viewportdoubleclickaction");
    altDoubleClickAction = settingsManager.getEnum<Qv::ViewportClickAction>("viewportaltdoubleclickaction");
    dragAction = settingsManager.getEnum<Qv::ViewportDragAction>("viewportdragaction");
    altDragAction = settingsManager.getEnum<Qv::ViewportDragAction>("viewportaltdragaction");
    middleButtonMode = settingsManager.getEnum<Qv::ClickOrDrag>("viewportmiddlebuttonmode");
    middleClickAction = settingsManager.getEnum<Qv::ViewportClickAction>("viewportmiddleclickaction");
    altMiddleClickAction = settingsManager.getEnum<Qv::ViewportClickAction>("viewportaltmiddleclickaction");
    middleDragAction = settingsManager.getEnum<Qv::ViewportDragAction>("viewportmiddledragaction");
    altMiddleDragAction = settingsManager.getEnum<Qv::ViewportDragAction>("viewportaltmiddledragaction");
    verticalScrollAction = settingsManager.getEnum<Qv::ViewportScrollAction>("viewportverticalscrollaction");
    horizontalScrollAction = settingsManager.getEnum<Qv::ViewportScrollAction>("viewporthorizontalscrollaction");
    altVerticalScrollAction = settingsManager.getEnum<Qv::ViewportScrollAction>("viewportaltverticalscrollaction");
    altHorizontalScrollAction = settingsManager.getEnum<Qv::ViewportScrollAction>("viewportalthorizontalscrollaction");
    scrollActionCooldown = settingsManager.getBoolean("scrollactioncooldown");

    //cursor auto-hiding
    isCursorAutoHideFullscreenEnabled = settingsManager.getBoolean("cursorautohidefullscreenenabled");
    hideCursorTimer->setInterval(settingsManager.getDouble("cursorautohidefullscreendelay") * 1000.0);

    // End of settings variables

    handleSmoothScalingChange();

    handleDpiAdjustmentChange();

    fitOrConstrainImage();

    setCursorVisible(true);
}

void QVGraphicsView::closeImage()
{
    imageCore.closeImage();
}

void QVGraphicsView::jumpToNextFrame()
{
    imageCore.jumpToNextFrame();
}

void QVGraphicsView::setPaused(const bool &desiredState)
{
    imageCore.setPaused(desiredState);
}

void QVGraphicsView::setSpeed(const int &desiredSpeed)
{
    imageCore.setSpeed(desiredSpeed);
}

void QVGraphicsView::rotateImage(const int relativeAngle)
{
    rotate(relativeAngle);
}

void QVGraphicsView::mirrorImage()
{
    scale(-1, 1);
}

void QVGraphicsView::flipImage()
{
    scale(1, -1);
}
