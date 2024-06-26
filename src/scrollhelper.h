#ifndef SCROLLHELPER_H
#define SCROLLHELPER_H

#include <QAbstractScrollArea>
#include <QElapsedTimer>
#include <QScrollBar>
#include <QTimer>

class ScrollHelper : public QObject
{
    Q_OBJECT
public:
    struct Parameters
    {
        QRect contentRect;
        QRect usableViewportRect;
        bool shouldConstrain;
        bool shouldCenter;
    };

    using GetParametersCallback = std::function<void(Parameters &)>;

    explicit ScrollHelper(QAbstractScrollArea *parent, GetParametersCallback getParametersCallback);

    void move(QPointF delta);

    void constrain(bool skipAnimation = false);

    void cancelAnimation();

private:
    void beginAnimatedScroll(QPoint delta);

    void handleAnimatedScroll();

    void applyScrollDelta(QPoint delta);

    static void calculateScrollRange(int contentDimension, int viewportDimension, int offset, bool shouldCenter, int &minValue, int &maxValue);

    static qreal calculateScrollDelta(qreal currentValue, int minValue, int maxValue, qreal proposedDelta);

    static qreal smoothAnimation(qreal x);

    QScrollBar *hScrollBar;
    QScrollBar *vScrollBar;
    GetParametersCallback getParametersCallback;
    QPointF lastMoveRoundingError;
    QPoint overscrollDistance;

    QTimer *animatedScrollTimer;
    QPoint animatedScrollTotalDelta;
    QPoint animatedScrollAppliedDelta;
    QElapsedTimer animatedScrollElapsed;
    const qreal animatedScrollDuration {250.0};
};

#endif // SCROLLHELPER_H
