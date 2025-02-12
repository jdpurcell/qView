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
            p.contentRect = getContentRect();
            p.usableViewportRect = getUsableViewportRect();
            p.shouldConstrain = isConstrainedPositioningEnabled;
            p.shouldCenter = isConstrainedSmallCenteringEnabled;
        });

    connect(&imageCore, &QVImageCore::animatedFrameChanged, this, &QVGraphicsView::animatedFrameChanged);
    connect(&imageCore, &QVImageCore::fileChanging, this, &QVGraphicsView::beforeLoad);
    connect(&imageCore, &QVImageCore::fileChanged, this, &QVGraphicsView::postLoad);

    expensiveScaleTimer = new QTimer(this);
    expensiveScaleTimer->setSingleShot(true);
    expensiveScaleTimer->setInterval(50);
    connect(expensiveScaleTimer, &QTimer::timeout, this, [this]{applyExpensiveScaling();});

    constrainBoundsTimer = new QTimer(this);
    constrainBoundsTimer->setSingleShot(true);
    constrainBoundsTimer->setInterval(500);
    connect(constrainBoundsTimer, &QTimer::timeout, this, [this]{scrollHelper->constrain();});

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

    if (getCurrentFileDetails().isPixmapLoaded)
    {
        const QPoint sizeDelta = QRect(QPoint(), event->size()).bottomRight() - QRect(QPoint(), event->oldSize()).bottomRight();
        scrollHelper->move(QPointF(sizeDelta) / -2.0);
        fitOrConstrainImage();
    }
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
    const auto initializeDrag = [this, event](const bool delayStart = false) {
        pressedMouseButton = event->button();
        mousePressModifiers = event->modifiers();
        isDelayingDrag = delayStart;
        if (!isDelayingDrag)
            viewport()->setCursor(Qt::ClosedHandCursor);
        isLastMousePosDubious = event->type() == QEvent::MouseButtonDblClick && QVApplication::isMouseEventSynthesized(event);
        lastMousePos = event->pos();
        setCursorVisible(true);
    };

    if (event->button() == Qt::LeftButton)
    {
        const bool isAltAction = event->modifiers().testFlag(Qt::ControlModifier);
        if ((isAltAction ? altDragAction : dragAction) != Qv::ViewportDragAction::None)
        {
            const bool justGotFocus = lastFocusIn.isValid() && lastFocusIn.elapsed() < 100;
            const bool delayDragStart = !isAltAction && enableNavigationRegions && !justGotFocus && getNavigationRegion(event->pos()).has_value();
            initializeDrag(delayDragStart);
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
        goToFile(Qv::GoToFileMode::Previous);
        return;
    }
    else if (event->button() == Qt::MouseButton::ForwardButton)
    {
        goToFile(Qv::GoToFileMode::Next);
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void QVGraphicsView::mouseReleaseEvent(QMouseEvent *event)
{
    if (pressedMouseButton != Qt::NoButton)
    {
        if (isDelayingDrag && pressedMouseButton == Qt::LeftButton)
        {
            const std::optional<Qv::GoToFileMode> navRegion = getNavigationRegion(lastMousePos);
            if (navRegion.has_value())
                goToFile(navRegion.value());
        }
        pressedMouseButton = Qt::NoButton;
        mousePressModifiers = Qt::NoModifier;
        isDelayingDrag = false;
        viewport()->setCursor(Qt::ArrowCursor);
        setCursorVisible(true);
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
        const QPoint delta = event->pos() - lastMousePos;
        if (isDelayingDrag)
        {
            if (isLastMousePosDubious)
            {
                // On the second press of a double tap on a touch screen, the position may
                // have been copied from the first press, so we can't rely on it
                isLastMousePosDubious = false;
                lastMousePos = event->pos();
                return;
            }
            if (qMax(qAbs(delta.x()), qAbs(delta.y())) < startDragDistance)
                return;
            isDelayingDrag = false;
            viewport()->setCursor(Qt::ClosedHandCursor);
        }
        const bool isAltAction = mousePressModifiers.testFlag(Qt::ControlModifier);
        const Qv::ViewportDragAction targetAction =
            pressedMouseButton == Qt::LeftButton ? (isAltAction ? altDragAction : dragAction) :
            pressedMouseButton == Qt::MiddleButton ? (isAltAction ? altMiddleDragAction : middleDragAction) :
            Qv::ViewportDragAction::None;
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
        const bool isInNavRegion = !isAltAction && enableNavigationRegions && getNavigationRegion(lastMousePos).has_value();
        if (!isInNavRegion)
        {
            executeClickAction(isAltAction ? altDoubleClickAction : doubleClickAction);
            return;
        }
    }

    // Pass unhandled events to QWidget instead of QGraphicsView otherwise we won't
    // receive a press event for the second click of a double click
    QWidget::mouseDoubleClickEvent(event);
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

    return QGraphicsView::event(event);
}

void QVGraphicsView::focusInEvent(QFocusEvent *event)
{
    lastFocusIn.start();

    QGraphicsView::focusInEvent(event);
}

void QVGraphicsView::focusOutEvent(QFocusEvent *event)
{
    cancelTurboNav();

    QGraphicsView::focusOutEvent(event);
}

void QVGraphicsView::wheelEvent(QWheelEvent *event)
{
    const QPoint eventPos = event->position().toPoint();
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
            const Qv::GoToFileMode navMode = navPrev ? Qv::GoToFileMode::Previous : Qv::GoToFileMode::Next;
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
            (horizontalScrollBar()->singleStep() * scrollXSmallSteps) * getRtlFlip(),
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
    else if (action == Qv::ViewportClickAction::CenterImage)
    {
        centerImage();
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
        scrollHelper->move(QPointF(-delta.x() * getRtlFlip(), -delta.y()));
    }
    else if (action == Qv::ViewportDragAction::MoveWindow)
    {
        const auto windowState = window()->windowState();
#ifndef Q_OS_MACOS
        if (windowState.testFlag(Qt::WindowMaximized))
            return;
#endif
        if (windowState.testFlag(Qt::WindowFullScreen))
            return;
        window()->move(window()->pos() + delta);
        isMovingWindow = true;
    }
}

void QVGraphicsView::executeScrollAction(const Qv::ViewportScrollAction action, const QPoint delta, const QPoint mousePos, const bool hasShiftModifier)
{
    const int deltaPerWheelStep = 120;
    const int rtlFlip = getRtlFlip();

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
                goToFile(Qv::GoToFileMode::Next);
            else
                goToFile(Qv::GoToFileMode::Previous);
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

void QVGraphicsView::beforeLoad()
{
    // If a prior pixmap is still loaded, capture its content rect
    if (getCurrentFileDetails().isPixmapLoaded)
        lastImageContentRect = getContentRect();
}

void QVGraphicsView::postLoad()
{
    scrollHelper->cancelAnimation();

    // Set the pixmap to the new image and reset the transform's scale to a known value
    removeExpensiveScaling();

    // If we have a content rect for the prior pixmap, scroll the new pixmap to align their centers
    if (lastImageContentRect.isValid())
        matchContentCenter(lastImageContentRect);

    qvApp->getActionManager().addFileToRecentsList(getCurrentFileDetails().fileInfo);

    emit fileChanged(loadIsFromSessionRestore);

    if (!loadIsFromSessionRestore)
    {
        if (navigationResetsZoom && calculatedZoomMode != defaultCalculatedZoomMode)
            setCalculatedZoomMode(defaultCalculatedZoomMode, true);
        else
            fitOrConstrainImage();
    }
    loadIsFromSessionRestore = false;

    expensiveScaleTimer->start();

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
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + (move.x() * getRtlFlip()));
        verticalScrollBar()->setValue(verticalScrollBar()->value() + move.y());
        lastZoomRoundingError = mapToScene(pos.value()) - scenePos;
        constrainBoundsTimer->start();
    }
    else if (!loadIsFromSessionRestore)
    {
        centerImage();
    }

    handleSmoothScalingChange();

    emit zoomLevelChanged();
}

const std::optional<Qv::CalculatedZoomMode> &QVGraphicsView::getCalculatedZoomMode() const
{
    return calculatedZoomMode;
}

void QVGraphicsView::setCalculatedZoomMode(const std::optional<Qv::CalculatedZoomMode> &value, const bool isNavigating)
{
    if (calculatedZoomMode == value)
    {
        if (calculatedZoomMode.has_value())
            centerImage();
        return;
    }

    if (value == Qv::CalculatedZoomMode::OriginalSize && zoomLevel == 1 && !isNavigating && qvApp->getSettingsManager().getBoolean("originalsizeastoggle"))
    {
        setCalculatedZoomMode(defaultCalculatedZoomMode != Qv::CalculatedZoomMode::OriginalSize ? defaultCalculatedZoomMode : Qv::CalculatedZoomMode::ZoomToFit);
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

    const QSizeF imageSize = getEffectiveOriginalSize();
    const QSize viewSize = getUsableViewportRect(true).size();

    if (viewSize.isEmpty())
        return;

    const LogicalPixelFitter fitter = getPixelFitter();
    const qreal fitXRatio = fitter.unsnapWidth(viewSize.width()) / imageSize.width();
    const qreal fitYRatio = fitter.unsnapHeight(viewSize.height()) / imageSize.height();

    qreal targetRatio;

    // Each mode will check if the rounded image size already produces the desired fit,
    // in which case we can use exactly 1.0 to avoid unnecessary scaling
    const int imageOverflowX = fitter.snapWidth(imageSize.width()) - viewSize.width();
    const int imageOverflowY = fitter.snapHeight(imageSize.height()) - viewSize.height();

    switch (calculatedZoomMode.value()) {
    case Qv::CalculatedZoomMode::ZoomToFit:
        // In rare cases, if the window sizing code just barely increased the size to enforce
        // the minimum and intends for a tiny upscale to occur (e.g. to 100.3%), that could get
        // misdetected as the special case for 1.0 here and leave an unintentional 1 pixel
        // border. So if we match on only one dimension, make sure the other dimension will have
        // at least a few pixels of border showing.
        if ((imageOverflowX == 0 && (imageOverflowY == 0 || imageOverflowY <= -2)) ||
            (imageOverflowY == 0 && (imageOverflowX == 0 || imageOverflowX <= -2)))
        {
            targetRatio = 1.0;
        }
        else
        {
            // If the fit ratios are extremely close, it's possible that both are sufficient to
            // contain the image, but one results in the opposing dimension getting rounded down
            // to just under the view size, so use the larger of the two ratios in that case.
            const bool isOverallFitToXRatio = fitter.snapHeight(imageSize.height() * fitXRatio) == viewSize.height();
            const bool isOverallFitToYRatio = fitter.snapWidth(imageSize.width() * fitYRatio) == viewSize.width();
            if (isOverallFitToXRatio || isOverallFitToYRatio)
                targetRatio = qMax(fitXRatio, fitYRatio);
            else
                targetRatio = qMin(fitXRatio, fitYRatio);
        }
        break;
    case Qv::CalculatedZoomMode::FillWindow:
        if ((imageOverflowX == 0 && imageOverflowY >= 0) ||
            (imageOverflowY == 0 && imageOverflowX >= 0))
        {
            targetRatio = 1.0;
        }
        else
        {
            targetRatio = qMax(fitXRatio, fitYRatio);
        }
        break;
    default:
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
    const QRect contentRect = getContentRect();
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

    const QTransform transform = getUnspecializedTransform();
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

void QVGraphicsView::goToFile(const Qv::GoToFileMode mode, const int index)
{
    const QVImageCore::GoToFileResult result = imageCore.goToFile(mode, index);

    if (result.reachedEnd)
        emit cancelSlideshow();
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

    // Don't go over the maximum scaling size (a small tolerance is added to cover rounding errors)
    const QSize contentSize = getContentRect().size();
    const QSize maxSize = getUsableViewportRect(true).size() * (expensiveScalingAboveWindowSize ? 3 : 1) + QSize(2, 2);
    return contentSize.width() <= maxSize.width() && contentSize.height() <= maxSize.height();
}

QSizeF QVGraphicsView::getEffectiveOriginalSize() const
{
    return getUnspecializedTransform().mapRect(QRectF(QPoint(), getCurrentFileDetails().loadedPixmapSize)).size() * getDpiAdjustment();
}

LogicalPixelFitter QVGraphicsView::getPixelFitter() const
{
    const MainWindow::ViewportPosition viewportPos = getMainWindow()->getViewportPosition();
    return LogicalPixelFitter(devicePixelRatioF(), QPoint(0, viewportPos.widgetY + viewportPos.obscuredHeight));
}

void QVGraphicsView::matchContentCenter(const QRect target)
{
    const QPointF delta = QRectF(getContentRect()).center() - QRectF(target).center();
    scrollHelper->move(QPointF(delta.x() * getRtlFlip(), delta.y()));
}

std::optional<Qv::GoToFileMode> QVGraphicsView::getNavigationRegion(const QPoint mousePos) const
{
    const int regionWidth = qMin(width() / 3, 175);
    if (mousePos.x() < regionWidth)
        return isRightToLeft() ? Qv::GoToFileMode::Next : Qv::GoToFileMode::Previous;
    if (mousePos.x() >= width() - regionWidth)
        return isRightToLeft() ? Qv::GoToFileMode::Previous : Qv::GoToFileMode::Next;
    return {};
}

QRect QVGraphicsView::getContentRect() const
{
    // Avoid using loadedPixmapItem and the active transform because the pixmap may have expensive scaling applied
    // which introduces a rounding error to begin with, and even worse, the error will be magnified if we're in the
    // the process of zooming in and haven't re-applied the expensive scaling yet. If that's the case, callers need
    // to know what the content rect will be once the dust settles rather than what's being temporarily displayed.
    const QSizeF pixmapSize = getCurrentFileDetails().loadedPixmapSize;
    const QRectF pixmapBoundingRect = QRectF(QPoint(), pixmapSize);
    const qreal pixmapScale = zoomLevel * appliedDpiAdjustment;
    const QTransform pixmapTransform = normalizeTransformOrigin(getUnspecializedTransform().scale(pixmapScale, pixmapScale), pixmapSize);
    const QRectF contentRect = pixmapTransform.mapRect(pixmapBoundingRect);
    return QRect(contentRect.topLeft().toPoint(), getPixelFitter().snapSize(contentRect.size()));
}

QRect QVGraphicsView::getUsableViewportRect(const bool addOverscan) const
{
    QRect rect = viewport()->rect();
    rect.setTop(getMainWindow()->getViewportPosition().obscuredHeight);
    if (addOverscan)
        rect.adjust(-fitOverscan, -fitOverscan, fitOverscan, fitOverscan);
    return rect;
}

void QVGraphicsView::setTransformScale(const qreal value)
{
    setTransformWithNormalization(getUnspecializedTransform().scale(value, value));
}

void QVGraphicsView::setTransformWithNormalization(const QTransform &matrix)
{
    setTransform(normalizeTransformOrigin(matrix, loadedPixmapItem->boundingRect().size()));
}

QTransform QVGraphicsView::getUnspecializedTransform() const
{
    // Returns a transform that represents the currently applied mirroring, flipping, and rotation
    // (only in increments of 90 degrees) operations, but with no scaling or translation.
    const QTransform t = transform();
    if (t.type() == QTransform::TxRotate)
        return { 0, t.m12() < 0 ? -1.0 : 1.0, t.m21() < 0 ? -1.0 : 1.0, 0, 0, 0 };
    else
        return { t.m11() < 0 ? -1.0 : 1.0, 0, 0, t.m22() < 0 ? -1.0 : 1.0, 0, 0 };
}

QTransform QVGraphicsView::normalizeTransformOrigin(const QTransform &matrix, const QSizeF &pixmapSize) const
{
    // This applies translation to compensate for mirroring, flipping, and rotation to ensure that
    // a pixmap will have its resulting top left at 0, 0. In theory this shouldn't matter, but it
    // works around a glitch where Qt sometimes won't paint the last pixel on the right of the
    // viewport if an image is rotated 90 degrees and just touching the right edge.
    const int horizontalFactor = matrix.m11() < 0 ? -1 * getRtlFlip() : matrix.m12() < 0 ? -1 : 0;
    const int verticalFactor = matrix.m22() < 0 ? -1 : matrix.m21() < 0 ? -1 * getRtlFlip() : 0;
    QTransform t { matrix.m11(), matrix.m12(), matrix.m21(), matrix.m22(), 0, 0 };
    return t.translate(pixmapSize.width() * horizontalFactor, pixmapSize.height() * verticalFactor);
}

qreal QVGraphicsView::getDpiAdjustment() const
{
    // Although inverting this potentially introduces a rounding error, it is inevitable. For
    // example with 1:1 pixel sizing @ 100% zoom, the transform's scale must be set to the
    // inverted value. Pre-inverting it here helps keep things consistent, e.g. so that the
    // content rect calculation has the same error that will happen during painting.
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

int QVGraphicsView::getRtlFlip() const
{
    return isRightToLeft() ? -1 : 1;
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

    //mouse actions
    enableNavigationRegions = settingsManager.getBoolean("navigationregionsenabled");
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
    const QRect oldRect = getContentRect();
    const QTransform t = transform();
    const bool isMirroredOrFlipped = t.isRotating() ? ((t.m12() < 0) == (t.m21() < 0)) : ((t.m11() < 0) != (t.m22() < 0));
    setTransformWithNormalization(transform().rotate(relativeAngle * (isMirroredOrFlipped ? -1 : 1)));
    matchContentCenter(oldRect);
}

void QVGraphicsView::mirrorImage()
{
    const QRect oldRect = getContentRect();
    const int rotateCorrection = transform().isRotating() ? -1 : 1;
    setTransformWithNormalization(transform().scale(-1 * rotateCorrection, 1 * rotateCorrection));
    matchContentCenter(oldRect);
}

void QVGraphicsView::flipImage()
{
    const QRect oldRect = getContentRect();
    const int rotateCorrection = transform().isRotating() ? -1 : 1;
    setTransformWithNormalization(transform().scale(1 * rotateCorrection, -1 * rotateCorrection));
    matchContentCenter(oldRect);
}

void QVGraphicsView::resetTransformation()
{
    const QRect oldRect = getContentRect();
    const QTransform t = transform();
    const qreal scale = qFabs(t.isRotating() ? t.m21() : t.m11());
    setTransformWithNormalization(QTransform::fromScale(scale, scale));
    matchContentCenter(oldRect);
}
