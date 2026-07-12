#include "qvimageloader.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QThreadPool>

QVImageLoader::QVImageLoader(QObject *parent) : QObject(parent)
{
}

QVImageLoader::~QVImageLoader()
{
    lifetimeToken.reset();
}

void QVImageLoader::setLargestDimension(const int value)
{
    largestDimension = value;
}

quint64 QVImageLoader::requestImage(const QString &absoluteFilePath, const bool forceReload)
{
    const QString normalizedPath = normalizePath(absoluteFilePath);
    const FileIdentity identity = getFileIdentity(normalizedPath);

    auto targetEntryIt = entries.find(normalizedPath);
    if (targetEntryIt == entries.end())
    {
        Entry entry;
        entry.priority = 0;
        entry.expectedIdentity = identity;
        targetEntryIt = entries.insert(normalizedPath, std::move(entry));
    }
    else
    {
        targetEntryIt->priority = 0;
        targetEntryIt->expectedIdentity = identity;

        if (targetEntryIt->state == State::Cached &&
            getFileIdentity(targetEntryIt->result.value()) != identity)
        {
            targetEntryIt->state = State::Queued;
            targetEntryIt->result.reset();
        }
        else if (targetEntryIt->state == State::Loading &&
                 targetEntryIt->startedIdentity != identity)
        {
            targetEntryIt->reloadAfterFinish = true;
        }
    }

    Entry &targetEntry = targetEntryIt.value();
    const bool retryCachedError =
        targetEntry.state == State::Cached &&
        targetEntry.result.has_value() &&
        targetEntry.result->errorData.has_value();
    if (forceReload || retryCachedError)
    {
        if (targetEntry.state == State::Loading)
        {
            targetEntry.reloadAfterFinish = true;
        }
        else
        {
            targetEntry.state = State::Queued;
            targetEntry.result.reset();
        }
    }

    const quint64 requestId = ++nextRequestId;
    pendingRequest = PendingRequest {requestId, normalizedPath};

    if (targetEntry.state == State::Cached)
        queueCachedDelivery(requestId, normalizedPath);

    startReadyJobs();
    return requestId;
}

void QVImageLoader::clear()
{
    pendingRequest.reset();

    for (auto it = entries.begin(); it != entries.end();)
    {
        if (it->state == State::Loading)
        {
            it->desired = false;
            it->reloadAfterFinish = false;
            ++it;
        }
        else
        {
            it = entries.erase(it);
        }
    }
}

bool QVImageLoader::FileIdentity::operator==(const FileIdentity &other) const
{
    return fileSize == other.fileSize && lastModified == other.lastModified;
}

QString QVImageLoader::normalizePath(const QString &path)
{
    return QFileInfo(path).absoluteFilePath();
}

QVImageLoader::FileIdentity QVImageLoader::getFileIdentity(const QString &absoluteFilePath)
{
    const QFileInfo fileInfo(absoluteFilePath);
    return {fileInfo.size(), fileInfo.lastModified()};
}

QVImageLoader::FileIdentity QVImageLoader::getFileIdentity(const Result &result)
{
    return {result.fileSize, result.lastModified};
}

QVImageLoader::Result QVImageLoader::readFile(const QString &absoluteFilePath, const int largestDimension)
{
    QImageReader imageReader(absoluteFilePath);
    imageReader.setAutoTransform(true);

    bool isMultiFrameImage = false;
    QSize intrinsicSize;
    QImage image;
    if ((imageReader.format() == "svg" || imageReader.format() == "svgz") && !imageReader.size().isEmpty())
    {
        intrinsicSize = imageReader.size();
        imageReader.setScaledSize(intrinsicSize.scaled(largestDimension, largestDimension, Qt::KeepAspectRatio));
        image = imageReader.read();
    }
    else
    {
        isMultiFrameImage = !imageReader.supportsOption(QImageIOHandler::Animation) && imageReader.imageCount() > 1;
        image = imageReader.read();
    }

    // Handle cases like icons containing multiple resolutions
    if (isMultiFrameImage)
    {
        qsizetype bestSize = image.sizeInBytes();
        while (imageReader.jumpToNextImage())
        {
            QImage candidateImage = imageReader.read();
            if (!candidateImage.isNull() && candidateImage.sizeInBytes() > bestSize)
            {
                bestSize = candidateImage.sizeInBytes();
                image = std::move(candidateImage);
            }
        }
    }

    const QFileInfo fileInfo(absoluteFilePath);

    Result result {
        std::move(image),
        fileInfo.absoluteFilePath(),
        fileInfo.size(),
        fileInfo.lastModified(),
        isMultiFrameImage,
        intrinsicSize,
        {}
    };

    if (result.image.isNull())
        result.errorData = ErrorData {imageReader.error(), imageReader.errorString()};

    return result;
}

bool QVImageLoader::isWanted(const QString &absoluteFilePath, const Entry &entry) const
{
    return entry.desired ||
        (pendingRequest.has_value() && pendingRequest->absoluteFilePath == absoluteFilePath);
}

void QVImageLoader::setDesiredImages(const QList<DesiredImage> &desiredImages)
{
    struct DesiredEntry
    {
        int priority;
        FileIdentity identity;
    };

    QHash<QString, DesiredEntry> desiredEntries;
    for (const DesiredImage &desiredImage : desiredImages)
    {
        const QString absoluteFilePath = normalizePath(desiredImage.absoluteFilePath);
        const FileIdentity identity = getFileIdentity(absoluteFilePath);
        auto desiredIt = desiredEntries.find(absoluteFilePath);
        if (desiredIt == desiredEntries.end())
        {
            desiredEntries.insert(absoluteFilePath, {desiredImage.priority, identity});
        }
        else
        {
            desiredIt->priority = qMin(desiredIt->priority, desiredImage.priority);
            desiredIt->identity = identity;
        }
    }

    for (auto it = entries.begin(); it != entries.end();)
    {
        const auto desiredIt = desiredEntries.constFind(it.key());
        if (desiredIt == desiredEntries.constEnd())
        {
            it->desired = false;
            const bool wanted = isWanted(it.key(), it.value());
            if (!wanted)
                it->reloadAfterFinish = false;

            if (it->state == State::Loading || wanted)
                ++it;
            else
                it = entries.erase(it);
            continue;
        }

        it->desired = true;
        it->priority = desiredIt->priority;
        it->expectedIdentity = desiredIt->identity;

        if (it->state == State::Cached && getFileIdentity(it->result.value()) != it->expectedIdentity)
        {
            it->state = State::Queued;
            it->result.reset();
        }
        else if (it->state == State::Loading && it->startedIdentity != it->expectedIdentity)
        {
            it->reloadAfterFinish = true;
        }

        desiredEntries.remove(it.key());
        ++it;
    }

    for (auto it = desiredEntries.constBegin(); it != desiredEntries.constEnd(); ++it)
    {
        Entry entry;
        entry.desired = true;
        entry.priority = it->priority;
        entry.expectedIdentity = it->identity;
        entries.insert(it.key(), std::move(entry));
    }

    startReadyJobs();
}

void QVImageLoader::queueCachedDelivery(const quint64 requestId, const QString &absoluteFilePath)
{
    QMetaObject::invokeMethod(
        this,
        [this, requestId, absoluteFilePath]() {
            deliverResult(requestId, absoluteFilePath);
        },
        Qt::QueuedConnection
    );
}

void QVImageLoader::deliverResult(const quint64 requestId, const QString &absoluteFilePath)
{
    if (!pendingRequest.has_value() ||
        pendingRequest->id != requestId ||
        pendingRequest->absoluteFilePath != absoluteFilePath)
    {
        return;
    }

    const auto entryIt = entries.constFind(absoluteFilePath);
    if (entryIt == entries.constEnd() || entryIt->state != State::Cached || !entryIt->result.has_value())
        return;

    const Result result = entryIt->result.value();
    pendingRequest.reset();
    emit imageReady(requestId, result);

    const auto currentEntryIt = entries.find(absoluteFilePath);
    if (currentEntryIt != entries.end() &&
        currentEntryIt->state == State::Cached &&
        !currentEntryIt->desired)
    {
        entries.erase(currentEntryIt);
    }
}

void QVImageLoader::startReadyJobs()
{
    std::optional<int> nextPriority;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
    {
        if (!isWanted(it.key(), it.value()) || it->state != State::Queued)
            continue;
        if (!nextPriority.has_value() || it->priority < nextPriority.value())
            nextPriority = it->priority;
    }

    if (!nextPriority.has_value())
        return;

    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
    {
        if (isWanted(it.key(), it.value()) &&
            it->state == State::Loading &&
            it->priority < nextPriority.value())
        {
            return;
        }
    }

    QStringList pathsToStart;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
    {
        if (isWanted(it.key(), it.value()) &&
            it->state == State::Queued &&
            it->priority == nextPriority.value())
        {
            pathsToStart.append(it.key());
        }
    }

    for (const QString &absoluteFilePath : std::as_const(pathsToStart))
        startJob(absoluteFilePath);
}

void QVImageLoader::startJob(const QString &absoluteFilePath)
{
    auto entryIt = entries.find(absoluteFilePath);
    if (entryIt == entries.end() ||
        !isWanted(absoluteFilePath, entryIt.value()) ||
        entryIt->state != State::Queued)
    {
        return;
    }

    entryIt->state = State::Loading;
    entryIt->startedIdentity = entryIt->expectedIdentity;
    entryIt->reloadAfterFinish = false;
    const quint64 generation = ++entryIt->generation;
    const int priority = entryIt->priority;
    const int targetLargestDimension = largestDimension;
    emit loadStarted(absoluteFilePath, priority);

    QVImageLoader *loader = this;
    const std::weak_ptr<int> weakLifetime = lifetimeToken;
    QObject *dispatchContext = QCoreApplication::instance();
    QThreadPool::globalInstance()->start(
        [
            loader,
            weakLifetime,
            dispatchContext,
            absoluteFilePath,
            generation,
            targetLargestDimension
        ]() {
            Result result = readFile(absoluteFilePath, targetLargestDimension);
            QMetaObject::invokeMethod(
                dispatchContext,
                [
                    loader,
                    weakLifetime,
                    absoluteFilePath,
                    generation,
                    result = std::move(result)
                ]() mutable {
                    if (!weakLifetime.lock())
                        return;
                    loader->jobFinished(absoluteFilePath, generation, std::move(result));
                },
                Qt::QueuedConnection
            );
        },
        -priority
    );
}

void QVImageLoader::jobFinished(const QString &absoluteFilePath, const quint64 generation, Result result)
{
    auto entryIt = entries.find(absoluteFilePath);
    if (entryIt == entries.end() || entryIt->state != State::Loading || entryIt->generation != generation)
        return;

    if (!isWanted(absoluteFilePath, entryIt.value()))
    {
        entries.erase(entryIt);
        startReadyJobs();
        return;
    }

    const FileIdentity currentIdentity = getFileIdentity(absoluteFilePath);
    if (entryIt->reloadAfterFinish || getFileIdentity(result) != currentIdentity)
    {
        entryIt->state = State::Queued;
        entryIt->reloadAfterFinish = false;
        entryIt->expectedIdentity = currentIdentity;
        entryIt->result.reset();
        startReadyJobs();
        return;
    }

    entryIt->expectedIdentity = currentIdentity;
    entryIt->state = State::Cached;
    entryIt->result = std::move(result);

    if (pendingRequest.has_value() && pendingRequest->absoluteFilePath == absoluteFilePath)
        deliverResult(pendingRequest->id, absoluteFilePath);

    startReadyJobs();
}
