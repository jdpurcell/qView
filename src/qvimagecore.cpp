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
#include <QIcon>
#include <QGuiApplication>
#include <QScreen>

static int findFileIndex(const QVFileEnumerator::CompatibleFileList &files, const QFileInfo &fileInfo)
{
    const QString targetPath = fileInfo.absoluteFilePath().normalized(QString::NormalizationForm_D);
    for (int i = 0; i < files.length(); i++)
    {
        // Compare absoluteFilePath first because it's way faster, but double-check with
        // QFileInfo::operator== because it respects file system case sensitivity rules
        const QString candidatePath = files[i].absoluteFilePath.normalized(QString::NormalizationForm_D);
        if (candidatePath.compare(targetPath, Qt::CaseInsensitive) == 0 &&
            QFileInfo(files[i].absoluteFilePath) == fileInfo)
        {
            return i;
        }
    }
    return -1;
}

QVImageCore::QVImageCore(QObject *parent) : QObject(parent)
{
    QImageReader::setAllocationLimit(8192); // 8 GiB

    connect(&loadedMovie, &QVMovie::updated, this, [this]() {
        sourceImage = loadedMovie.currentImage();
        updateLoadedPixmap();
        emit pixmapChanged();
    });

    connect(&imageLoader, &QVImageLoader::imageReady, this,
        [this](const quint64 requestId, const ReadData &readData) {
            if (requestId != pendingLoadRequestId)
                return;

            const bool debouncePreloading = pendingLoadDebouncesPreloading;
            pendingLoadRequestId = 0;
            loadInProgress = false;
            pendingLoadDebouncesPreloading = false;
            loadPixmap(readData);

            // A fileChanged handler may have synchronously requested another image.
            if (loadInProgress ||
                currentFileDetails.fileInfo.absoluteFilePath() != readData.absoluteFilePath)
            {
                return;
            }

            refreshDesiredImages(!debouncePreloading);
            if (debouncePreloading && preloadingMode != Qv::PreloadMode::Disabled)
                preloadDebounceTimer.start();
        });

    preloadDebounceTimer.setSingleShot(true);
    preloadDebounceTimer.setInterval(500);
    connect(&preloadDebounceTimer, &QTimer::timeout, this, [this]() {
        refreshDesiredImages();
    });

    connect(&fileEnumerator, &QVFileEnumerator::sortParametersChanged, this, [this](){
        updateFolderInfo();
        refreshDesiredImages();
        emit sortParametersChanged();
    });

    for (auto const &screen : QGuiApplication::screens())
    {
        const QSize adjustedSize = screen->size() * screen->devicePixelRatio();
        const int largerDimension = qMax(adjustedSize.width(), adjustedSize.height());
        if (largerDimension > largestDimension)
            largestDimension = largerDimension;
    }
    imageLoader.setLargestDimension(largestDimension);

    // Connect to settings signal
    connect(&qvApp->getSettingsManager(), &SettingsManager::settingsUpdated, this, &QVImageCore::settingsUpdated);
    connect(qvApp, &QVApplication::folderListingsInvalidated, this, &QVImageCore::markFolderInfoDirty);
    settingsUpdated();
}

void QVImageCore::loadFile(const QString &fileName, const bool isReloading, const QString &baseDir, const bool debouncePreloading, const std::optional<int> initialFrameNumber)
{
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

    fileOrLoadPending = true;
    preloadDebounceTimer.stop();
    loadInProgress = true;
    pendingLoadDebouncesPreloading = debouncePreloading;
    pendingLoadRequestId = imageLoader.requestImage(absolutePath, isReloading, initialFrameNumber);
    if (debouncePreloading)
        imageLoader.setDesiredImages({{absolutePath, 0}});
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

    if (currentFileDetails.errorData.has_value())
    {
        loadEmptyPixmap();
        return;
    }

    sourceImage = readData.image;
    currentFileDetails.targetColorSpace = getTargetColorSpace();
    updateLoadedPixmap();

    // Set file details
    currentFileDetails.isPixmapLoaded = true;
    currentFileDetails.baseImageSize = readData.intrinsicSize.isValid() ? readData.intrinsicSize : loadedPixmap.size();
    currentFileDetails.loadedPixmapSize = loadedPixmap.size();

    currentFileDetails.isMultiFrameImage = readData.isMultiFrameImage;
    currentFileDetails.frameCount = readData.frameCount;
    currentFileDetails.frameNumber = readData.frameNumber;

    // Animation detection
    loadedMovie.stop();
    loadedMovie.setFileName("");
    loadedMovie.setFormat("");
    loadedMovie.setCacheMode(QVMovie::CacheAll);
    const QString suffix = currentFileDetails.fileInfo.suffix().toLower();
    const bool disableAnimation = suffix == "svg" || suffix == "svgz";
    if (!disableAnimation)
    {
        loadedMovie.setFileName(currentFileDetails.fileInfo.absoluteFilePath());

        // APNG workaround
        if (loadedMovie.format() == "png")
        {
            loadedMovie.setFormat("apng");
            loadedMovie.setFileName(currentFileDetails.fileInfo.absoluteFilePath());
        }

        if (!readData.isMultiFrameImage && loadedMovie.isValid() && loadedMovie.frameCount() != 1)
            loadedMovie.start();
    }

    currentFileDetails.isMovieLoaded = loadedMovie.state() == QVMovie::Running;

    if (!currentFileDetails.isMovieLoaded)
        if (auto device = loadedMovie.device())
            device->close();

    emit fileChanged();
}

void QVImageCore::closeImage(const bool stayInDir)
{
    preloadDebounceTimer.stop();
    imageLoader.clear();
    pendingLoadRequestId = 0;
    loadInProgress = false;
    pendingLoadDebouncesPreloading = false;
    fileOrLoadPending = false;

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
    sourceImage = QImage();
    loadedPixmap = QPixmap();
    loadedMovie.stop();
    loadedMovie.setFileName("");

    emit fileChanged();
}

QVImageCore::GoToFileResult QVImageCore::goToFile(const Qv::GoToFileMode mode, const int index)
{
    GoToFileResult result;
    if (loadInProgress)
        return result;

    bool shouldRetryFolderInfoUpdate = false;

    if (folderInfoDirty)
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
    case Qv::GoToFileMode::JumpBack:
    case Qv::GoToFileMode::JumpForward:
    {
        const int distance = qBound(5, qvApp->getSettingsManager().getInteger("jumpdistance"), 250);
        searchDirection = mode == Qv::GoToFileMode::JumpBack ? -1 : 1;
        const int lastIndex = static_cast<int>(fileList.size()) - 1;
        const bool atBoundary = searchDirection < 0 ? newIndex == 0 : newIndex == lastIndex;
        if (atBoundary)
        {
            if (fileEnumerator.getIsLoopFoldersEnabled())
                newIndex = searchDirection < 0 ? lastIndex : 0;
            else
                result.reachedEnd = true;
        }
        else
            newIndex = qBound(0, newIndex + searchDirection * distance, lastIndex);
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
    case Qv::GoToFileMode::Random:
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

    if (!QFile::exists(nextImageFilePath) || nextImageFilePath == currentFileDetails.fileInfo.absoluteFilePath())
        return result;

    if (shouldRetryFolderInfoUpdate)
        updateFolderInfo();

    const bool debouncePreloading =
        mode == Qv::GoToFileMode::Random ||
        mode == Qv::GoToFileMode::JumpBack ||
        mode == Qv::GoToFileMode::JumpForward;
    loadFile(nextImageFilePath, false, {}, debouncePreloading);

    // Replenish lookahead immediately, while the requested image is loading
    // or being delivered from cache. Keep displayed-file details unchanged.
    if (!debouncePreloading)
    {
        if (shouldRetryFolderInfoUpdate)
            newIndex = findFileIndex(fileList, QFileInfo(nextImageFilePath));

        imageLoader.setDesiredImages(getDesiredImages(nextImageFilePath, newIndex, true));
    }

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
    folderInfoDirty = false;

    // Set current file index variable
    currentFileDetails.updateLoadedIndexInFolder();
}

QList<QVImageLoader::DesiredImage> QVImageCore::getDesiredImages(const QString &targetPath, const int loadedIndex, const bool includePreloads) const
{
    QList<QVImageLoader::DesiredImage> desiredImages {{targetPath, 0}};

    const auto &fileList = currentFileDetails.folderFileInfoList;
    if (!includePreloads || loadedIndex == -1 || preloadingMode == Qv::PreloadMode::Disabled)
        return desiredImages;

    const int preloadDistance = preloadingMode == Qv::PreloadMode::Extended ? 3 : 1;
    const bool loopFolders = fileEnumerator.getIsLoopFoldersEnabled();
    for (int distance = 1; distance <= preloadDistance; ++distance)
    {
        for (const int direction : {-1, 1})
        {
            int index = loadedIndex + (distance * direction);
            if (loopFolders)
                index = (index % fileList.size() + fileList.size()) % fileList.size();
            else if (index < 0 || index >= fileList.size())
                continue;

            desiredImages.append({fileList.at(index).absoluteFilePath, distance});
        }
    }

    return desiredImages;
}

void QVImageCore::refreshDesiredImages(const bool includePreloads)
{
    if (loadInProgress)
    {
        // A disabled setting should purge the old cache without cancelling the
        // foreground load. Enabled modes are reconciled once that load finishes.
        if (preloadingMode == Qv::PreloadMode::Disabled)
            imageLoader.setDesiredImages({});
        return;
    }

    const QString targetFilePath = currentFileDetails.fileInfo.absoluteFilePath();
    if (targetFilePath.isEmpty() || preloadingMode == Qv::PreloadMode::Disabled)
    {
        imageLoader.setDesiredImages({});
        return;
    }

    imageLoader.setDesiredImages(getDesiredImages(targetFilePath, currentFileDetails.loadedIndexInFolder, includePreloads));
}

void QVImageCore::refreshDisplayColorSpace()
{
    if (sourceImage.isNull() || colorSpaceConversion != Qv::ColorSpaceConversion::AutoDetect)
        return;

    const QColorSpace targetColorSpace = getTargetColorSpace();
    if (targetColorSpace == currentFileDetails.targetColorSpace)
        return;

    currentFileDetails.targetColorSpace = targetColorSpace;
    updateLoadedPixmap();
    emit pixmapChanged();
}

void QVImageCore::updateLoadedPixmap()
{
    QImage image = sourceImage;
    handleColorSpaceConversion(image, currentFileDetails.targetColorSpace);
    loadedPixmap = QPixmap::fromImage(std::move(image));
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

void QVImageCore::jumpToImageFrame(const int direction)
{
    if (loadInProgress)
        return;

    const int count = currentFileDetails.frameCount;
    const int frame = (currentFileDetails.frameNumber + direction + count) % count;
    loadFile(currentFileDetails.fileInfo.absoluteFilePath(), false, {}, false, frame);
}

void QVImageCore::jumpToNextFrame()
{
    if (currentFileDetails.isMovieLoaded)
    {
        loadedMovie.setPaused(true);
        loadedMovie.jumpToNextFrame();
    }
    else if (currentFileDetails.isMultiFrameImage)
    {
        jumpToImageFrame(1);
    }
}

void QVImageCore::jumpToPreviousFrame()
{
    if (currentFileDetails.isMovieLoaded)
    {
        loadedMovie.setPaused(true);
        int frameNumber = loadedMovie.currentFrameNumber() - 1;
        if (frameNumber < 0)
            frameNumber = loadedMovie.frameCount() - 1;
        loadedMovie.jumpToFrame(frameNumber);
    }
    else if (currentFileDetails.isMultiFrameImage)
    {
        jumpToImageFrame(-1);
    }
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

QPixmap QVImageCore::scaleExpensively(const QSize size)
{
    if (!currentFileDetails.isPixmapLoaded)
        return QPixmap();

    if (loadedPixmap.size() == size)
        return loadedPixmap;

    return loadedPixmap.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void QVImageCore::settingsUpdated()
{
    auto &settingsManager = qvApp->getSettingsManager();

    //preloading mode
    preloadingMode = settingsManager.getEnum<Qv::PreloadMode>("preloadingmode");

    //update folder info to reflect new settings (e.g. sort order)
    fileEnumerator.loadSettings(false);
    updateFolderInfo();

    //color space conversion
    Qv::ColorSpaceConversion oldColorSpaceConversion = colorSpaceConversion;
    colorSpaceConversion = settingsManager.getEnum<Qv::ColorSpaceConversion>("colorspaceconversion");

    if (colorSpaceConversion != oldColorSpaceConversion && currentFileDetails.isPixmapLoaded && !loadInProgress)
    {
        const auto initialFrameNumber = currentFileDetails.isMultiFrameImage ? std::optional(currentFileDetails.frameNumber) : std::nullopt;
        loadFile(currentFileDetails.fileInfo.absoluteFilePath(), false, {}, false, initialFrameNumber);
    }
    else
    {
        refreshDesiredImages(!preloadDebounceTimer.isActive());
    }
}

void QVImageCore::FileDetails::updateLoadedIndexInFolder()
{
    loadedIndexInFolder = findFileIndex(folderFileInfoList, fileInfo);
}
