#ifndef SCROLLHELPER_H
#define SCROLLHELPER_H

#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QVariantAnimation>

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

    QPoint getScrollPosition();

    void setScrollPosition(QPoint pos);

    static void calculateScrollRange(int contentDimension, int viewportDimension, int offset, bool shouldCenter, int &minValue, int &maxValue);

    static qreal calculateScrollDelta(qreal currentValue, int minValue, int maxValue, qreal proposedDelta);

    QScrollBar *hScrollBar;
    QScrollBar *vScrollBar;
    GetParametersCallback getParametersCallback;
    QPointF lastMoveRoundingError;
    QPoint overscrollDistance;

    QVariantAnimation *scrollAnimation;
    const int animatedScrollDuration {250};
};

#endif // SCROLLHELPER_H
