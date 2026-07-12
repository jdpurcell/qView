#ifndef QVIMAGECORE_H
#define QVIMAGECORE_H

#include "qvnamespace.h"
#include "qvfileenumerator.h"
#include "qvimageloader.h"
#include "qvmovie.h"
#include <optional>
#include <QObject>
#include <QPixmap>
#include <QFileInfo>
#include <QTimer>
#include <QElapsedTimer>
#include <QColorSpace>

class QVImageCore : public QObject
{
    Q_OBJECT

public:
    using ErrorData = QVImageLoader::ErrorData;
    using ReadData = QVImageLoader::Result;

    struct FileDetails
    {
        QFileInfo fileInfo;
        QVFileEnumerator::CompatibleFileList folderFileInfoList;
        int loadedIndexInFolder = -1;
        bool isPixmapLoaded = false;
        bool isMovieLoaded = false;
        QSize baseImageSize;
        QSize loadedPixmapSize;
        QColorSpace targetColorSpace;
        QElapsedTimer timeSinceLoaded;
        std::optional<ErrorData> errorData;

        void updateLoadedIndexInFolder();
    };

    struct GoToFileResult
    {
        bool reachedEnd = false;
    };

    explicit QVImageCore(QObject *parent = nullptr);

    void loadFile(const QString &fileName, bool isReloading = false, const QString &baseDir = "", bool debouncePreloading = false);
    void closeImage(const bool stayInDir = false);
    GoToFileResult goToFile(const Qv::GoToFileMode mode, const int index = 0);

    Qv::SortMode getSortMode() const { return fileEnumerator.getSortMode(); }
    void setSortMode(const Qv::SortMode mode) { fileEnumerator.setSortMode(mode); }
    bool getSortDescending() const { return fileEnumerator.getSortDescending(); }
    void setSortDescending(const bool descending) { fileEnumerator.setSortDescending(descending); }

    void settingsUpdated();

    void jumpToNextFrame();
    void jumpToPreviousFrame();
    void setPaused(bool desiredState);
    void setSpeed(int desiredSpeed);

    QPixmap scaleExpensively(const QSizeF desiredSize);

    const QPixmap& getLoadedPixmap() const { return loadedPixmap; }
    const QVMovie& getLoadedMovie() const { return loadedMovie; }
    const FileDetails& getCurrentFileDetails() const { return currentFileDetails; }
    bool hasFileOrPendingLoad() const { return fileOrLoadPending; }

signals:
    void animatedFrameChanged(QRect rect);

    void fileChanging();

    void fileChanged();

    void sortParametersChanged();

protected:
    void loadPixmap(const ReadData &readData);
    void loadEmptyPixmap();
    void updateFolderInfo(QString dirPath = QString());
    QList<QVImageLoader::DesiredImage> getDesiredImages(bool includePreloads = true) const;
    void refreshDesiredImages(bool includePreloads = true);
    QColorSpace getTargetColorSpace() const;
    QColorSpace detectDisplayColorSpace() const;
    static void handleColorSpaceConversion(QImage &image, const QColorSpace &targetColorSpace);

private:
    QVFileEnumerator fileEnumerator {this};
    QVImageLoader imageLoader {this};
    QTimer preloadDebounceTimer {this};

    QPixmap loadedPixmap;
    QVMovie loadedMovie;

    FileDetails currentFileDetails;

    Qv::PreloadMode preloadingMode {Qv::PreloadMode::Adjacent};
    Qv::ColorSpaceConversion colorSpaceConversion {Qv::ColorSpaceConversion::AutoDetect};

    int largestDimension {1920};

    quint64 pendingLoadRequestId = 0;
    bool loadInProgress {false};
    bool pendingLoadDebouncesPreloading {false};
    bool fileOrLoadPending {false};
};

#endif // QVIMAGECORE_H
