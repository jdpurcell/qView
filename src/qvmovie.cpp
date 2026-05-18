// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:critical reason:data-parser

#include "qvmovie.h"

#include "qelapsedtimer.h"
#include "qimage.h"
#include "qimagereader.h"
#include "qpixmap.h"
#include "qrect.h"
#include "qelapsedtimer.h"
#include "qtimer.h"
#include "qlist.h"
#include "qbuffer.h"
#include "qdir.h"

#include <chrono>
#include <map>
#include <memory>

#define QMOVIE_INVALID_DELAY -1

QT_BEGIN_NAMESPACE

class QFrameInfo
{
public:
    QPixmap pixmap;
    int delay;
    bool endMark;
    inline QFrameInfo(bool endMark)
        : pixmap(QPixmap()), delay(QMOVIE_INVALID_DELAY), endMark(endMark)
    { }

    inline QFrameInfo()
        : pixmap(QPixmap()), delay(QMOVIE_INVALID_DELAY), endMark(false)
    { }

    inline QFrameInfo(QPixmap &&pixmap, int delay)
        : pixmap(std::move(pixmap)), delay(delay), endMark(false)
    { }

    inline bool isValid()
    {
        return endMark || !(pixmap.isNull() && (delay == QMOVIE_INVALID_DELAY));
    }

    inline bool isEndMarker()
    { return endMark; }

    static inline QFrameInfo endMarker()
    { return QFrameInfo(true); }
};
Q_DECLARE_TYPEINFO(QFrameInfo, Q_RELOCATABLE_TYPE);

class QVMoviePrivate
{
    Q_DECLARE_PUBLIC(QVMovie)

public:
    QVMoviePrivate();

    void init(QVMovie *qq, std::unique_ptr<QImageReader> r);

    bool isDone();
    bool next();
    int speedAdjustedDelay(int delay) const;
    bool isValid() const;
    bool jumpToFrame(int frameNumber);
    int frameCount() const;
    bool jumpToNextFrame();
    QFrameInfo infoForFrame(int frameNumber);
    void reset();
    void cancelNextLoad();

    inline void enterState(QVMovie::MovieState newState) {
        movieState = newState;
        emit q_ptr->stateChanged(newState);
    }

    void _q_loadNextFrame();
    void _q_loadNextFrame(bool starting);

    QVMovie *q_ptr = nullptr;
    std::unique_ptr<QImageReader> reader = nullptr;
    int speed = 100;

    QVMovie::MovieState movieState = QVMovie::NotRunning;
    QRect frameRect;
    QPixmap currentPixmap;
    int currentFrameNumber = -1;
    int nextFrameNumber = 0;
    int greatestFrameNumber = -1;
    int nextDelay = 0;
    std::optional<std::chrono::steady_clock::time_point> nextLoadTime = std::nullopt;
    int playCounter = -1;
    qint64 initialDevicePos = 0;
    QVMovie::CacheMode cacheMode = QVMovie::CacheNone;
    bool haveReadAll = false;
    bool isFirstIteration = true;
    std::map<int, QFrameInfo> frameMap;
    QString absoluteFilePath;

    QTimer *nextImageTimer = nullptr;
};

QVMoviePrivate::QVMoviePrivate()
{
}

void QVMoviePrivate::init(QVMovie *qq, std::unique_ptr<QImageReader> r)
{
    q_ptr = qq;
    reader = std::move(r);
    nextImageTimer = new QTimer(qq);
    nextImageTimer->setTimerType(Qt::PreciseTimer);
    nextImageTimer->setSingleShot(true);
    QObject::connect(nextImageTimer, &QTimer::timeout, qq, [this]() {
        _q_loadNextFrame();
    });
}

void QVMoviePrivate::reset()
{
    cancelNextLoad();
    if (reader->device())
        initialDevicePos = reader->device()->pos();
    currentFrameNumber = -1;
    nextFrameNumber = 0;
    greatestFrameNumber = -1;
    nextDelay = 0;
    playCounter = -1;
    haveReadAll = false;
    isFirstIteration = true;
    frameMap.clear();
}

void QVMoviePrivate::cancelNextLoad()
{
    nextLoadTime = std::nullopt;
    nextImageTimer->stop();
}

bool QVMoviePrivate::isDone()
{
    return (playCounter == 0);
}

int QVMoviePrivate::speedAdjustedDelay(int delay) const
{
    return int( (qint64(delay) * qint64(100) ) / qint64(speed) );
}

QFrameInfo QVMoviePrivate::infoForFrame(int frameNumber)
{
    Q_Q(QVMovie);

    if (frameNumber < 0)
        return QFrameInfo(); // Invalid

    if (haveReadAll && (frameNumber > greatestFrameNumber)) {
        if (frameNumber == greatestFrameNumber+1)
            return QFrameInfo::endMarker();
        return QFrameInfo(); // Invalid
    }

    // For an animated image format, the tradition is that QMovie calls read()
    // until canRead() == false, because the number of frames may not be known
    // in advance; but if we're abusing a multi-frame format as an animation,
    // canRead() may remain true, and we need to stop after reading the maximum
    // number of frames that the image provides.
    const bool supportsAnimation = reader->supportsOption(QImageIOHandler::Animation);
    const int stopAtFrame = supportsAnimation ? -1 : frameCount();

    // For an animated image format, QImageIOHandler::nextImageDelay() should
    // provide the time to wait until showing the next frame; but multi-frame
    // formats are not expected to provide this value, so use 1000 ms by default.
    const auto nextFrameDelay = [&]() { return supportsAnimation ? reader->nextImageDelay() : 1000; };

    if (cacheMode == QVMovie::CacheNone) {
        if (frameNumber != currentFrameNumber+1) {
            // Non-sequential frame access
            if (!reader->jumpToImage(frameNumber)) {
                if (frameNumber == 0) {
                    // Special case: Attempt to "rewind" so we can loop
                    // ### This could be implemented as QImageReader::rewind()
                    if (reader->device()->isSequential())
                        return QFrameInfo(); // Invalid
                    QString fileName = reader->fileName();
                    QByteArray format = reader->format();
                    QIODevice *device = reader->device();
                    QColor bgColor = reader->backgroundColor();
                    QSize scaledSize = reader->scaledSize();
                    if (fileName.isEmpty())
                        reader = std::make_unique<QImageReader>(device, format);
                    else
                        reader = std::make_unique<QImageReader>(absoluteFilePath, format);
                    if (!reader->canRead()) // Provoke a device->open() call
                        emit q->error(reader->error());
                    reader->device()->seek(initialDevicePos);
                    reader->setBackgroundColor(bgColor);
                    reader->setScaledSize(scaledSize);
                } else {
                    return QFrameInfo(); // Invalid
                }
            }
        }
        if (stopAtFrame > 0 ? (frameNumber < stopAtFrame) : reader->canRead()) {
            // reader says we can read. Attempt to actually read image
            // But if it's a non-animated multi-frame format and we know the frame count, stop there.
            if (stopAtFrame > 0)
                reader->jumpToImage(frameNumber);
            QImage anImage = reader->read();
            if (anImage.isNull()) {
                // Reading image failed.
                return QFrameInfo(); // Invalid
            }
            if (frameNumber > greatestFrameNumber)
                greatestFrameNumber = frameNumber;
            return QFrameInfo(QPixmap::fromImage(std::move(anImage)), nextFrameDelay());
        } else if (frameNumber != 0) {
            // We've read all frames now. Return an end marker
            haveReadAll = true;
            return QFrameInfo::endMarker();
        } else {
            // No readable frames
            haveReadAll = true;
            return QFrameInfo();
        }
    }

    // CacheMode == CacheAll
    if (frameNumber > greatestFrameNumber) {
        // Frame hasn't been read from file yet. Try to do it
        for (int i = greatestFrameNumber + 1; i <= frameNumber; ++i) {
            if (stopAtFrame > 0 ? (frameNumber < stopAtFrame) : reader->canRead()) {
                // reader says we can read. Attempt to actually read image
                // But if it's a non-animated multi-frame format and we know the frame count, stop there.
                if (stopAtFrame > 0)
                    reader->jumpToImage(frameNumber);
                QImage anImage = reader->read();
                if (anImage.isNull()) {
                    // Reading image failed.
                    return QFrameInfo(); // Invalid
                }
                greatestFrameNumber = i;
                QFrameInfo info(QPixmap::fromImage(std::move(anImage)), nextFrameDelay());
                // Cache it!
                auto &e = frameMap[i] = std::move(info);
                if (i == frameNumber) {
                    return e;
                }
            } else {
                // We've read all frames now. Return an end marker
                haveReadAll = true;
                return frameNumber == greatestFrameNumber + 1 ? QFrameInfo::endMarker() : QFrameInfo();
            }
        }
    }
    // Return info for requested (cached) frame
    const auto it = frameMap.find(frameNumber);
    return it == frameMap.cend() ? QFrameInfo() : it->second;
}

bool QVMoviePrivate::next()
{
    QFrameInfo info = infoForFrame(nextFrameNumber);
    if (!info.isValid())
        return false;
    if (info.isEndMarker()) {
        // We reached the end of the animation.
        if (isFirstIteration) {
            if (nextFrameNumber == 0) {
                // No frames could be read at all (error).
                return false;
            }
            // End of first iteration. Initialize play counter
            playCounter = reader->loopCount();
            isFirstIteration = false;
        }
        // Loop as appropriate
        if (playCounter != 0) {
            if (playCounter != -1) // Infinite?
                playCounter--;     // Nope
            nextFrameNumber = 0;
            return next();
        }
        // Loop no more. Done
        return false;
    }
    // Image and delay OK, update internal state
    currentFrameNumber = nextFrameNumber++;
    currentPixmap = info.pixmap;

    if (!speed)
        return true;

    nextDelay = speedAdjustedDelay(info.delay);
    return true;
}

void QVMoviePrivate::_q_loadNextFrame()
{
    _q_loadNextFrame(false);
}

void QVMoviePrivate::_q_loadNextFrame(bool starting)
{
    Q_Q(QVMovie);
    const auto loadStartTime = std::chrono::steady_clock::now();
    if (next()) {
        if (starting && movieState == QVMovie::NotRunning) {
            enterState(QVMovie::Running);
            emit q->started();
        }

        if (frameRect.size() != currentPixmap.rect().size()) {
            frameRect = currentPixmap.rect();
            emit q->resized(frameRect.size());
        }

        emit q->updated(frameRect);
        emit q->frameChanged(currentFrameNumber);

        if (speed && movieState == QVMovie::Running) {
            nextLoadTime = (nextLoadTime.has_value() ? nextLoadTime.value() : loadStartTime) + std::chrono::milliseconds(nextDelay);
            const int adjustedNextDelay = std::chrono::duration_cast<std::chrono::milliseconds>(nextLoadTime.value() - std::chrono::steady_clock::now()).count();
            if (adjustedNextDelay < -1000) {
                // If we get too far behind, don't try to catch up
                nextLoadTime = std::nullopt;
            }
            nextImageTimer->start(std::max(0, adjustedNextDelay));
        }
    } else {
        // Could not read another frame
        if (!isDone()) {
            emit q->error(reader->error());
        }

        // Graceful finish
        if (movieState != QVMovie::Paused) {
            nextFrameNumber = 0;
            nextLoadTime = std::nullopt;
            isFirstIteration = true;
            playCounter = -1;
            enterState(QVMovie::NotRunning);
            emit q->finished();
        }
    }
}

bool QVMoviePrivate::isValid() const
{
    Q_Q(const QVMovie);

    if (greatestFrameNumber >= 0)
        return true; // have we seen valid data
    bool canRead = reader->canRead();
    if (!canRead) {
        // let the consumer know it's broken
        //
        // ### the const_cast here is ugly, but 'const' of this method is
        // technically wrong right now, since it may cause the underlying device
        // to open.
        emit const_cast<QVMovie*>(q)->error(reader->error());
    }
    return canRead;
}

bool QVMoviePrivate::jumpToFrame(int frameNumber)
{
    if (frameNumber < 0)
        return false;
    if (currentFrameNumber == frameNumber)
        return true;
    nextFrameNumber = frameNumber;
    if (movieState == QVMovie::Running)
        cancelNextLoad();
    _q_loadNextFrame();
    return (nextFrameNumber == currentFrameNumber+1);
}

int QVMoviePrivate::frameCount() const
{
    int result;
    if ((result = reader->imageCount()) != 0)
        return result;
    if (haveReadAll)
        return greatestFrameNumber+1;
    return 0; // Don't know
}

bool QVMoviePrivate::jumpToNextFrame()
{
    return jumpToFrame(currentFrameNumber+1);
}

QVMovie::QVMovie(QObject *parent)
    : QObject(parent), d_ptr(new QVMoviePrivate)
{
    Q_D(QVMovie);
    d->init(this, std::make_unique<QImageReader>());
}

QVMovie::QVMovie(QIODevice *device, const QByteArray &format, QObject *parent)
    : QObject(parent), d_ptr(new QVMoviePrivate)
{
    Q_D(QVMovie);
    d->init(this, std::make_unique<QImageReader>(device, format));
    d->initialDevicePos = device->pos();
}

QVMovie::QVMovie(const QString &fileName, const QByteArray &format, QObject *parent)
    : QObject(parent), d_ptr(new QVMoviePrivate)
{
    Q_D(QVMovie);
    d->init(this, std::make_unique<QImageReader>(fileName, format));
    d->absoluteFilePath = QDir(fileName).absolutePath();
    if (d->reader->device())
        d->initialDevicePos = d->reader->device()->pos();
}

QVMovie::~QVMovie()
{
    Q_D(QVMovie);
    d->reader.reset();
}

void QVMovie::setDevice(QIODevice *device)
{
    Q_D(QVMovie);
    d->reader->setDevice(device);
    d->reset();
}

QIODevice *QVMovie::device() const
{
    Q_D(const QVMovie);
    return d->reader->device();
}

void QVMovie::setFileName(const QString &fileName)
{
    Q_D(QVMovie);
    d->absoluteFilePath = QDir(fileName).absolutePath();
    d->reader->setFileName(fileName);
    d->reset();
}

QString QVMovie::fileName() const
{
    Q_D(const QVMovie);
    return d->reader->fileName();
}

void QVMovie::setFormat(const QByteArray &format)
{
    Q_D(QVMovie);
    d->reader->setFormat(format);
}

QByteArray QVMovie::format() const
{
    Q_D(const QVMovie);
    return d->reader->format();
}

void QVMovie::setBackgroundColor(const QColor &color)
{
    Q_D(QVMovie);
    d->reader->setBackgroundColor(color);
}

QColor QVMovie::backgroundColor() const
{
    Q_D(const QVMovie);
    return d->reader->backgroundColor();
}

QVMovie::MovieState QVMovie::state() const
{
    Q_D(const QVMovie);
    return d->movieState;
}

QRect QVMovie::frameRect() const
{
    Q_D(const QVMovie);
    return d->frameRect;
}

QPixmap QVMovie::currentPixmap() const
{
    Q_D(const QVMovie);
    return d->currentPixmap;
}

QImage QVMovie::currentImage() const
{
    Q_D(const QVMovie);
    return d->currentPixmap.toImage();
}

bool QVMovie::isValid() const
{
    Q_D(const QVMovie);
    return d->isValid();
}

QImageReader::ImageReaderError QVMovie::lastError() const
{
    Q_D(const QVMovie);
    return d->reader->error();
}

QString QVMovie::lastErrorString() const
{
    Q_D(const QVMovie);
    return d->reader->errorString();
}

int QVMovie::frameCount() const
{
    Q_D(const QVMovie);
    return d->frameCount();
}

int QVMovie::nextFrameDelay() const
{
    Q_D(const QVMovie);
    return d->nextDelay;
}

int QVMovie::currentFrameNumber() const
{
    Q_D(const QVMovie);
    return d->currentFrameNumber;
}

bool QVMovie::jumpToNextFrame()
{
    Q_D(QVMovie);
    return d->jumpToNextFrame();
}

bool QVMovie::jumpToFrame(int frameNumber)
{
    Q_D(QVMovie);
    return d->jumpToFrame(frameNumber);
}

int QVMovie::loopCount() const
{
    Q_D(const QVMovie);
    return d->reader->loopCount();
}

void QVMovie::setPaused(bool paused)
{
    Q_D(QVMovie);
    if (paused) {
        if (d->movieState == NotRunning)
            return;
        d->enterState(Paused);
        d->cancelNextLoad();
    } else {
        if (d->movieState == Running)
            return;
        d->enterState(Running);
        d->_q_loadNextFrame();
    }
}

void QVMovie::setSpeed(int percentSpeed)
{
    Q_D(QVMovie);
    if (d->speed == percentSpeed || percentSpeed < 0)
        return;
    int oldSpeed = d->speed;
    d->speed = percentSpeed;
    if (d->movieState == Running) {
        if (!percentSpeed)
            d->cancelNextLoad();
        else if (!oldSpeed)
            d->_q_loadNextFrame();
    }
}

int QVMovie::speed() const
{
    Q_D(const QVMovie);
    return d->speed;
}

void QVMovie::start()
{
    Q_D(QVMovie);
    if (d->movieState == NotRunning) {
        d->_q_loadNextFrame(true);
    } else if (d->movieState == Paused) {
        setPaused(false);
    }
}

void QVMovie::stop()
{
    Q_D(QVMovie);
    if (d->movieState == NotRunning)
        return;
    d->enterState(NotRunning);
    d->cancelNextLoad();
    d->nextFrameNumber = 0;
}

QSize QVMovie::scaledSize()
{
    Q_D(QVMovie);
    return d->reader->scaledSize();
}

void QVMovie::setScaledSize(const QSize &size)
{
    Q_D(QVMovie);
    d->reader->setScaledSize(size);
}

QList<QByteArray> QVMovie::supportedFormats()
{
    QList<QByteArray> list = QImageReader::supportedImageFormats();

    QBuffer buffer;
    buffer.open(QIODevice::ReadOnly);

    const auto doesntSupportAnimation =
            [&buffer](const QByteArray &format) {
                return !QImageReader(&buffer, format).supportsOption(QImageIOHandler::Animation);
            };

    list.removeIf(doesntSupportAnimation);
    return list;
}

QVMovie::CacheMode QVMovie::cacheMode() const
{
    Q_D(const QVMovie);
    return d->cacheMode;
}

void QVMovie::setCacheMode(CacheMode cacheMode)
{
    Q_D(QVMovie);
    d->cacheMode = cacheMode;
}

QT_END_NAMESPACE
