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

QVImageCore::QVImageCore(QObject *parent) : QObject(parent)
{
    QImageReader::setAllocationLimit(8192); // 8 GiB

    connect(&loadedMovie, &QVMovie::updated, this, [this](QRect rect){
        QImage movieImage = loadedMovie.currentImage();
        handleColorSpaceConversion(movieImage, currentFileDetails.targetColorSpace);
        loadedPixmap = QPixmap::fromImage(std::move(movieImage));
        emit animatedFrameChanged(rect);
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
    settingsUpdated();
}

void QVImageCore::loadFile(const QString &fileName, const bool isReloading, const QString &baseDir, const bool debouncePreloading)
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
    pendingLoadRequestId = imageLoader.requestImage(absolutePath, isReloading);
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

    QImage readImage = readData.image;
    const QColorSpace targetColorSpace = getTargetColorSpace();
    handleColorSpaceConversion(readImage, targetColorSpace);
    loadedPixmap = QPixmap::fromImage(std::move(readImage));

    // Set file details
    currentFileDetails.isPixmapLoaded = true;
    currentFileDetails.baseImageSize = readData.intrinsicSize.isValid() ? readData.intrinsicSize : loadedPixmap.size();
    currentFileDetails.loadedPixmapSize = loadedPixmap.size();
    currentFileDetails.targetColorSpace = targetColorSpace;

    // Animation detection
    loadedMovie.stop();
    loadedMovie.setFormat("");
    loadedMovie.setCacheMode(QVMovie::CacheAll);
    loadedMovie.setFileName(currentFileDetails.fileInfo.absoluteFilePath());

    // APNG workaround
    if (loadedMovie.format() == "png")
    {
        loadedMovie.setFormat("apng");
        loadedMovie.setFileName(currentFileDetails.fileInfo.absoluteFilePath());
    }

    if (!readData.isMultiFrameImage && loadedMovie.isValid() && loadedMovie.frameCount() != 1)
        loadedMovie.start();

    currentFileDetails.isMovieLoaded = loadedMovie.state() == QVMovie::Running;

    if (!currentFileDetails.isMovieLoaded)
        if (auto device = loadedMovie.device())
            device->close();

    currentFileDetails.timeSinceLoaded.start();

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

    loadFile(nextImageFilePath, false, {}, mode == Qv::GoToFileMode::Random);

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

QList<QVImageLoader::DesiredImage> QVImageCore::getDesiredImages(const bool includePreloads) const
{
    const QString absoluteTargetPath = currentFileDetails.fileInfo.absoluteFilePath();
    QList<QVImageLoader::DesiredImage> desiredImages {{absoluteTargetPath, 0}};

    const auto &fileList = currentFileDetails.folderFileInfoList;
    if (!includePreloads || fileList.isEmpty() || preloadingMode == Qv::PreloadMode::Disabled)
        return desiredImages;

    const int loadedIndex = currentFileDetails.loadedIndexInFolder;
    if (loadedIndex == -1)
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

    imageLoader.setDesiredImages(getDesiredImages(includePreloads));
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

    //update folder info to reflect new settings (e.g. sort order)
    fileEnumerator.loadSettings(false);
    updateFolderInfo();

    //color space conversion
    Qv::ColorSpaceConversion oldColorSpaceConversion = colorSpaceConversion;
    colorSpaceConversion = settingsManager.getEnum<Qv::ColorSpaceConversion>("colorspaceconversion");

    if (colorSpaceConversion != oldColorSpaceConversion && currentFileDetails.isPixmapLoaded && !loadInProgress)
        loadFile(currentFileDetails.fileInfo.absoluteFilePath());
    else
        refreshDesiredImages(!preloadDebounceTimer.isActive());
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
