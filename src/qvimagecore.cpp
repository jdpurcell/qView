#include "qvimagecore.h"
#include "qvapplication.h"
#include "qvwin32functions.h"
#include "qvcocoafunctions.h"
#include "qvlinuxx11functions.h"
#include <cstring>
#include <QMessageBox>
#include <QDir>
#include <QUrl>
#include <QSettings>
#include <QCollator>
#include <QtConcurrent/QtConcurrentRun>
#include <QIcon>
#include <QGuiApplication>
#include <QScreen>

QCache<QString, QVImageCore::ReadData> QVImageCore::pixmapCache;

QVImageCore::QVImageCore(QObject *parent) : QObject(parent)
{
    QImageReader::setAllocationLimit(8192); // 8 GiB

    connect(&loadedMovie, &QMovie::updated, this, [this](QRect rect){
        QImage movieImage = loadedMovie.currentImage();
        handleColorSpaceConversion(movieImage, currentFileDetails.targetColorSpace);
        loadedPixmap = QPixmap::fromImage(movieImage);
        emit animatedFrameChanged(rect);
    });

    connect(&loadFutureWatcher, &QFutureWatcher<ReadData>::finished, this, [this](){
        loadPixmap(loadFutureWatcher.result());
    });

    connect(&fileEnumerator, &QVFileEnumerator::sortParametersChanged, this, [this](){
        updateFolderInfo();
        emit sortParametersChanged();
    });

    for (auto const &screen : QGuiApplication::screens())
    {
        const QSize adjustedSize = screen->size() * screen->devicePixelRatio();
        const int largerDimension = qMax(adjustedSize.width(), adjustedSize.height());
        if (largerDimension > largestDimension)
            largestDimension = largerDimension;
    }

    // Connect to settings signal
    connect(&qvApp->getSettingsManager(), &SettingsManager::settingsUpdated, this, &QVImageCore::settingsUpdated);
    settingsUpdated();
}

void QVImageCore::loadFile(const QString &fileName, const bool isReloading, const QString &baseDir)
{
    if (waitingOnLoad)
    {
        return;
    }

    QString adjustedFileName = fileName;

    //sanitize file name if necessary
    QUrl fileUrl = QUrl(adjustedFileName);
    if (fileUrl.isLocalFile())
        adjustedFileName = fileUrl.toLocalFile();

#ifdef WIN32_LOADED
    QString longFileName = QVWin32Functions::getLongPath(QDir::toNativeSeparators(QFileInfo(adjustedFileName).absoluteFilePath()));
    if (!longFileName.isEmpty())
        adjustedFileName = longFileName;
#endif

    QFileInfo fileInfo(adjustedFileName);
    QString absolutePath = fileInfo.absoluteFilePath();

    if (fileInfo.isDir())
    {
        updateFolderInfo(absolutePath);
        if (currentFileDetails.folderFileInfoList.isEmpty())
            closeImage(true);
        else
            loadFile(currentFileDetails.folderFileInfoList.at(0).absoluteFilePath);
        return;
    }

    if (!baseDir.isEmpty())
    {
        updateFolderInfo(baseDir);
    }

    // Pause playing movie because it feels better that way
    setPaused(true);

    currentFileDetails.isLoadRequested = true;
    waitingOnLoad = true;

    QColorSpace targetColorSpace = getTargetColorSpace();
    QString cacheKey = getPixmapCacheKey(absolutePath, fileInfo.size(), targetColorSpace);

    //check if cached already before loading the long way
    auto *cachedData = isReloading ? nullptr : QVImageCore::pixmapCache.take(cacheKey);
    if (cachedData != nullptr)
    {
        ReadData readData = *cachedData;
        delete cachedData;
        loadPixmap(readData);
    }
    else if (preloadsInProgress.contains(absolutePath))
    {
        waitingOnPreloadPath = absolutePath;
    }
    else
    {
        loadFutureWatcher.setFuture(QtConcurrent::run(&QVImageCore::readFile, this, absolutePath, targetColorSpace));
    }
}

QVImageCore::ReadData QVImageCore::readFile(const QString &fileName, const QColorSpace &targetColorSpace)
{
    if (qvApp->getIsApplicationQuitting())
        return {};

    QImageReader imageReader;
    imageReader.setAutoTransform(true);

    imageReader.setFileName(fileName);

    bool isMultiFrameImage = false;
    QSize intrinsicSize;
    QImage readImage;
    if ((imageReader.format() == "svg" || imageReader.format() == "svgz") && !imageReader.size().isEmpty())
    {
        intrinsicSize = imageReader.size();
        imageReader.setScaledSize(intrinsicSize.scaled(largestDimension, largestDimension, Qt::KeepAspectRatio));
        readImage = imageReader.read();
    }
    else
    {
        isMultiFrameImage = !imageReader.supportsOption(QImageIOHandler::Animation) && imageReader.imageCount() > 1;
        readImage = imageReader.read();
    }

    // Handle cases like icons containing multiple resolutions
    if (isMultiFrameImage)
    {
        qsizetype bestSize = readImage.sizeInBytes();
        while (imageReader.jumpToNextImage())
        {
            QImage candidateImage = imageReader.read();
            if (!candidateImage.isNull() && candidateImage.sizeInBytes() > bestSize)
            {
                bestSize = candidateImage.sizeInBytes();
                readImage = candidateImage;
            }
        }
    }

    if (qvApp->getIsApplicationQuitting())
        return {};

    handleColorSpaceConversion(readImage, targetColorSpace);

    if (qvApp->getIsApplicationQuitting())
        return {};

    QPixmap readPixmap = QPixmap::fromImage(readImage);
    QFileInfo fileInfo(fileName);

    ReadData readData = {
        readPixmap,
        fileInfo.absoluteFilePath(),
        fileInfo.size(),
        isMultiFrameImage,
        intrinsicSize,
        targetColorSpace,
        {}
    };

    if (readPixmap.isNull())
    {
        readData.errorData = {
            imageReader.error(),
            imageReader.errorString()
        };
    }

    return readData;
}

void QVImageCore::loadPixmap(const ReadData &readData)
{
    emit fileChanging();

    if (readData.errorData.has_value())
    {
        FileDetails emptyDetails;
        emptyDetails.folderFileInfoList = currentFileDetails.folderFileInfoList;
        emptyDetails.loadedIndexInFolder = currentFileDetails.loadedIndexInFolder;
        emptyDetails.errorData = readData.errorData;
        currentFileDetails = emptyDetails;
    }
    else
    {
        currentFileDetails.errorData = {};
    }

    // Do this first so we can keep folder info even when loading errored files
    currentFileDetails.fileInfo = QFileInfo(readData.absoluteFilePath);
    currentFileDetails.updateLoadedIndexInFolder();
    if (currentFileDetails.loadedIndexInFolder == -1)
    {
        // If the current list of files doesn't contain this one, assume we're switching folders now
        updateFolderInfo(currentFileDetails.fileInfo.path());
    }

    // Reset mechanism to avoid stalling while loading
    waitingOnLoad = false;

    if (currentFileDetails.errorData.has_value())
    {
        loadEmptyPixmap();
        return;
    }

    loadedPixmap = readData.pixmap;

    // Set file details
    currentFileDetails.isPixmapLoaded = true;
    currentFileDetails.baseImageSize = readData.intrinsicSize.isValid() ? readData.intrinsicSize : loadedPixmap.size();
    currentFileDetails.loadedPixmapSize = loadedPixmap.size();
    currentFileDetails.targetColorSpace = readData.targetColorSpace;

    addToCache(readData);

    // Animation detection
    loadedMovie.stop();
    loadedMovie.setFormat("");
    loadedMovie.setCacheMode(QMovie::CacheAll);
    loadedMovie.setFileName(currentFileDetails.fileInfo.absoluteFilePath());

    // APNG workaround
    if (loadedMovie.format() == "png")
    {
        loadedMovie.setFormat("apng");
        loadedMovie.setFileName(currentFileDetails.fileInfo.absoluteFilePath());
    }

    if (!readData.isMultiFrameImage && loadedMovie.isValid() && loadedMovie.frameCount() != 1)
        loadedMovie.start();

    currentFileDetails.isMovieLoaded = loadedMovie.state() == QMovie::Running;

    if (!currentFileDetails.isMovieLoaded)
        if (auto device = loadedMovie.device())
            device->close();

    currentFileDetails.timeSinceLoaded.start();

    emit fileChanged();

    requestCaching();
}

void QVImageCore::closeImage(const bool stayInDir)
{
    emit fileChanging();
    FileDetails emptyDetails;
    if (stayInDir)
    {
        emptyDetails.folderFileInfoList = currentFileDetails.folderFileInfoList;
        emptyDetails.loadedIndexInFolder = currentFileDetails.loadedIndexInFolder;
    }
    currentFileDetails = emptyDetails;
    loadEmptyPixmap();
}

void QVImageCore::loadEmptyPixmap()
{
    loadedPixmap = QPixmap();
    loadedMovie.stop();
    loadedMovie.setFileName("");

    emit fileChanged();
}

QVImageCore::GoToFileResult QVImageCore::goToFile(const Qv::GoToFileMode mode, const int index)
{
    GoToFileResult result;
    bool shouldRetryFolderInfoUpdate = false;

    // Update folder info only after a little idle time as an optimization for when
    // the user is rapidly navigating through files.
    if (!currentFileDetails.timeSinceLoaded.isValid() || currentFileDetails.timeSinceLoaded.hasExpired(3000))
    {
        // Make sure the file still exists because if it disappears from the file listing we'll lose
        // track of our index within the folder. Use the static 'exists' method to avoid caching.
        // If we skip updating now, flag it for retry later once we locate a new file.
        if (QFile::exists(currentFileDetails.fileInfo.absoluteFilePath()))
            updateFolderInfo();
        else
            shouldRetryFolderInfoUpdate = true;
    }

    const auto &fileList = currentFileDetails.folderFileInfoList;
    if (fileList.isEmpty())
        return result;

    int newIndex = currentFileDetails.loadedIndexInFolder;
    int searchDirection = 0;

    switch (mode) {
    case Qv::GoToFileMode::Constant:
    {
        newIndex = index;
        break;
    }
    case Qv::GoToFileMode::First:
    {
        newIndex = 0;
        searchDirection = 1;
        break;
    }
    case Qv::GoToFileMode::Previous:
    {
        if (newIndex == 0)
        {
            if (fileEnumerator.getIsLoopFoldersEnabled())
                newIndex = fileList.size()-1;
            else
                result.reachedEnd = true;
        }
        else
            newIndex--;
        searchDirection = -1;
        break;
    }
    case Qv::GoToFileMode::Next:
    {
        if (fileList.size()-1 == newIndex)
        {
            if (fileEnumerator.getIsLoopFoldersEnabled())
                newIndex = 0;
            else
                result.reachedEnd = true;
        }
        else
            newIndex++;
        searchDirection = 1;
        break;
    }
    case Qv::GoToFileMode::Last:
    {
        newIndex = fileList.size()-1;
        searchDirection = -1;
        break;
    }
    case Qv::GoToFileMode::NextRandom:
    {
        if (fileList.size() > 1)
        {
            randomGenerator.ensureParamsUpToDate(fileList.size());
            newIndex = randomGenerator.nextIndex(newIndex);
            searchDirection = 0;
        }
        break;
    }
    case Qv::GoToFileMode::PreviousRandom:
    {
        if (fileList.size() > 1)
        {
            randomGenerator.ensureParamsUpToDate(fileList.size());
            newIndex = randomGenerator.previousIndex(newIndex);
            searchDirection = 0;
        }
        break;
    }
    }

    while (searchDirection == 1 && newIndex < fileList.size()-1 && !QFile::exists(fileList.value(newIndex).absoluteFilePath))
        newIndex++;
    while (searchDirection == -1 && newIndex > 0 && !QFile::exists(fileList.value(newIndex).absoluteFilePath))
        newIndex--;

    const QString nextImageFilePath = fileList.value(newIndex).absoluteFilePath;

    if (!QFile::exists(nextImageFilePath) || nextImageFilePath == currentFileDetails.fileInfo.absoluteFilePath())
        return result;

    if (shouldRetryFolderInfoUpdate)
        updateFolderInfo();

    loadFile(nextImageFilePath);

    return result;
}

void QVImageCore::updateFolderInfo(QString dirPath)
{
    if (dirPath.isEmpty())
    {
        // No directory specified; we are refreshing the currently loaded directory
        dirPath = currentFileDetails.folderFileInfoList.getBaseDir();

        // Return early if there's nothing currently loaded
        if (dirPath.isEmpty())
            return;
    }

    // Get file listing
    currentFileDetails.folderFileInfoList = fileEnumerator.getCompatibleFiles(dirPath);

    // Set current file index variable
    currentFileDetails.updateLoadedIndexInFolder();
}

void QVImageCore::requestCaching()
{
    if (preloadingMode == Qv::PreloadMode::Disabled)
    {
        QVImageCore::pixmapCache.clear();
        return;
    }

    QColorSpace targetColorSpace = getTargetColorSpace();

    int preloadingDistance = preloadingMode == Qv::PreloadMode::Extended ? 4 : 1;

    QStringList filesToPreload;
    for (int i = currentFileDetails.loadedIndexInFolder-preloadingDistance; i <= currentFileDetails.loadedIndexInFolder+preloadingDistance; i++)
    {
        int index = i;

        //keep within index range
        if (fileEnumerator.getIsLoopFoldersEnabled())
        {
            if (index > currentFileDetails.folderFileInfoList.length()-1)
                index = index-(currentFileDetails.folderFileInfoList.length());
            else if (index < 0)
                index = index+(currentFileDetails.folderFileInfoList.length());
        }

        //if still out of range after looping, just cancel the cache for this index
        if (index > currentFileDetails.folderFileInfoList.length()-1 || index < 0 || currentFileDetails.folderFileInfoList.isEmpty())
            continue;

        QString filePath = currentFileDetails.folderFileInfoList[index].absoluteFilePath;
        filesToPreload.append(filePath);

        // Don't try to cache the currently loaded image
        if (index == currentFileDetails.loadedIndexInFolder)
            continue;

        requestCachingFile(filePath, targetColorSpace);
    }
    lastFilesPreloaded = filesToPreload;
}

void QVImageCore::requestCachingFile(const QString &filePath, const QColorSpace &targetColorSpace)
{
    QFileInfo imgFile(filePath);
    QString cacheKey = getPixmapCacheKey(filePath, imgFile.size(), targetColorSpace);

    //check if image is already loaded or requested
    if (QVImageCore::pixmapCache.contains(cacheKey) || lastFilesPreloaded.contains(filePath))
        return;

    //TODO: this is basically pointless since the cache goes by uncompressed size
    if (imgFile.size()/1024 > QVImageCore::pixmapCache.maxCost()/2)
        return;

    preloadsInProgress.insert(filePath);

    auto *cacheFutureWatcher = new QFutureWatcher<ReadData>();
    connect(cacheFutureWatcher, &QFutureWatcher<ReadData>::finished, this, [cacheFutureWatcher, this](){
        const ReadData readData = cacheFutureWatcher->result();
        if (waitingOnPreloadPath == readData.absoluteFilePath)
        {
            loadPixmap(readData);
            waitingOnPreloadPath = QString();
        }
        else
        {
            addToCache(readData);
        }
        preloadsInProgress.remove(readData.absoluteFilePath);
        cacheFutureWatcher->deleteLater();
    });

    cacheFutureWatcher->setFuture(QtConcurrent::run(&QVImageCore::readFile, this, filePath, targetColorSpace));
}

void QVImageCore::addToCache(const ReadData &readData)
{
    if (readData.pixmap.isNull())
        return;

    QString cacheKey = getPixmapCacheKey(readData.absoluteFilePath, readData.fileSize, readData.targetColorSpace);
    qint64 memoryBytes = static_cast<qint64>(readData.pixmap.width()) * readData.pixmap.height() * readData.pixmap.depth() / 8;

    QVImageCore::pixmapCache.insert(cacheKey, new ReadData(readData), qMax(memoryBytes / 1024, 1LL));
}

QString QVImageCore::getPixmapCacheKey(const QString &absoluteFilePath, const qint64 &fileSize, const QColorSpace &targetColorSpace)
{
    QString targetColorSpaceHash = QCryptographicHash::hash(targetColorSpace.iccProfile(), QCryptographicHash::Md5).toHex();
    return absoluteFilePath + "\n" + QString::number(fileSize) + "\n" + targetColorSpaceHash;
}

QColorSpace QVImageCore::getTargetColorSpace() const
{
    return
        colorSpaceConversion == Qv::ColorSpaceConversion::AutoDetect ? detectDisplayColorSpace() :
        colorSpaceConversion == Qv::ColorSpaceConversion::SRgb ? QColorSpace::SRgb :
        colorSpaceConversion == Qv::ColorSpaceConversion::DisplayP3 ? QColorSpace::DisplayP3 :
        QColorSpace();
}

QColorSpace QVImageCore::detectDisplayColorSpace() const
{
    QWindow *window = static_cast<QWidget*>(parent())->window()->windowHandle();

    QByteArray profileData;
#ifdef WIN32_LOADED
    profileData = QVWin32Functions::getIccProfileForWindow(window);
#endif
#ifdef COCOA_LOADED
    profileData = QVCocoaFunctions::getIccProfileForWindow(window);
#endif
#ifdef X11_LOADED
    profileData = QVLinuxX11Functions::getIccProfileForWindow(window);
#endif

    if (!profileData.isEmpty())
    {
        return QColorSpace::fromIccProfile(profileData);
    }

    return {};
}

void QVImageCore::handleColorSpaceConversion(QImage &image, const QColorSpace &targetColorSpace)
{
    // Assume image is sRGB if it doesn't specify
    if (!image.colorSpace().isValid())
        image.setColorSpace(QColorSpace::SRgb);

    // Convert image color space if we have a target that's different
    if (targetColorSpace.isValid() && image.colorSpace() != targetColorSpace)
        image.convertToColorSpace(targetColorSpace);
}

void QVImageCore::jumpToNextFrame()
{
    if (!currentFileDetails.isMovieLoaded)
        return;

    loadedMovie.setPaused(true);
    loadedMovie.jumpToNextFrame();
}

void QVImageCore::jumpToPreviousFrame()
{
    if (!currentFileDetails.isMovieLoaded)
        return;

    loadedMovie.setPaused(true);
    int frameNumber = loadedMovie.currentFrameNumber() - 1;
    if (frameNumber < 0)
        frameNumber = loadedMovie.frameCount() - 1;
    loadedMovie.jumpToFrame(frameNumber);
}

void QVImageCore::setPaused(bool desiredState)
{
    if (currentFileDetails.isMovieLoaded)
        loadedMovie.setPaused(desiredState);
}

void QVImageCore::setSpeed(int desiredSpeed)
{
    if (currentFileDetails.isMovieLoaded)
        loadedMovie.setSpeed(std::clamp(desiredSpeed, 0, 1000));
}

QPixmap QVImageCore::scaleExpensively(const QSizeF desiredSize)
{
    if (!currentFileDetails.isPixmapLoaded)
        return QPixmap();

    // If we are really close to the original size, just return the original
    if (abs(desiredSize.width() - loadedPixmap.width()) < 1 &&
        abs(desiredSize.height() - loadedPixmap.height()) < 1)
    {
        return loadedPixmap;
    }

    QSize size = desiredSize.toSize();
    size.rwidth() = qMax(size.width(), 1);
    size.rheight() = qMax(size.height(), 1);

    return loadedPixmap.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void QVImageCore::settingsUpdated()
{
    auto &settingsManager = qvApp->getSettingsManager();

    //preloading mode
    preloadingMode = settingsManager.getEnum<Qv::PreloadMode>("preloadingmode");
    //cost is in KiB
    switch (preloadingMode) {
    case Qv::PreloadMode::Adjacent:
    {
        QVImageCore::pixmapCache.setMaxCost(307200);
        break;
    }
    case Qv::PreloadMode::Extended:
    {
        QVImageCore::pixmapCache.setMaxCost(921600);
        break;
    }
    default:
        QVImageCore::pixmapCache.setMaxCost(0);
        break;
    }

    //update folder info to reflect new settings (e.g. sort order)
    fileEnumerator.loadSettings(false);
    updateFolderInfo();

    //color space conversion
    Qv::ColorSpaceConversion oldColorSpaceConversion = colorSpaceConversion;
    colorSpaceConversion = settingsManager.getEnum<Qv::ColorSpaceConversion>("colorspaceconversion");

    if (colorSpaceConversion != oldColorSpaceConversion && currentFileDetails.isPixmapLoaded)
        loadFile(currentFileDetails.fileInfo.absoluteFilePath());
}

void QVImageCore::FileDetails::updateLoadedIndexInFolder()
{
    const QString targetPath = fileInfo.absoluteFilePath().normalized(QString::NormalizationForm_D);
    for (int i = 0; i < folderFileInfoList.length(); i++)
    {
        // Compare absoluteFilePath first because it's way faster, but double-check with
        // QFileInfo::operator== because it respects file system case sensitivity rules
        QString candidatePath = folderFileInfoList[i].absoluteFilePath.normalized(QString::NormalizationForm_D);
        if (candidatePath.compare(targetPath, Qt::CaseInsensitive) == 0 &&
            QFileInfo(folderFileInfoList[i].absoluteFilePath) == fileInfo)
        {
            loadedIndexInFolder = i;
            return;
        }
    }
    loadedIndexInFolder = -1;
}
