#ifndef QVIMAGECORE_H
#define QVIMAGECORE_H

#include "qvnamespace.h"
#include "qvfileenumerator.h"
#include <optional>
#include <QObject>
#include <QImageReader>
#include <QPixmap>
#include <QMovie>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTimer>
#include <QCache>
#include <QElapsedTimer>
#include <QColorSpace>
#include "qvrandom.h"

class QVImageCore : public QObject
{
    Q_OBJECT

public:
    struct ErrorData
    {
        int errorNum;
        QString errorString;
    };

    struct FileDetails
    {
        QFileInfo fileInfo;
        QVFileEnumerator::CompatibleFileList folderFileInfoList;
        int loadedIndexInFolder = -1;
        bool isLoadRequested = false;
        bool isPixmapLoaded = false;
        bool isMovieLoaded = false;
        QSize baseImageSize;
        QSize loadedPixmapSize;
        QColorSpace targetColorSpace;
        QElapsedTimer timeSinceLoaded;
        std::optional<ErrorData> errorData;

        void updateLoadedIndexInFolder();
    };

    struct ReadData
    {
        QPixmap pixmap;
        QString absoluteFilePath;
        qint64 fileSize = 0;
        bool isMultiFrameImage = false;
        QSize intrinsicSize;
        QColorSpace targetColorSpace;
        std::optional<ErrorData> errorData;
    };

    struct GoToFileResult
    {
        bool reachedEnd = false;
    };

    explicit QVImageCore(QObject *parent = nullptr);

    void loadFile(const QString &fileName, const bool isReloading = false, const QString &baseDir = "");
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
    const QMovie& getLoadedMovie() const { return loadedMovie; }
    const FileDetails& getCurrentFileDetails() const { return currentFileDetails; }

signals:
    void animatedFrameChanged(QRect rect);

    void fileChanging();

    void fileChanged();

    void sortParametersChanged();

protected:
    ReadData readFile(const QString &fileName, const QColorSpace &targetColorSpace);
    void loadPixmap(const ReadData &readData);
    void loadEmptyPixmap();
    void updateFolderInfo(QString dirPath = QString());
    void requestCaching();
    void requestCachingFile(const QString &filePath, const QColorSpace &targetColorSpace);
    void addToCache(const ReadData &readImageAndFileInfo);
    static QString getPixmapCacheKey(const QString &absoluteFilePath, const qint64 &fileSize, const QColorSpace &targetColorSpace);
    QColorSpace getTargetColorSpace() const;
    QColorSpace detectDisplayColorSpace() const;
    static void handleColorSpaceConversion(QImage &image, const QColorSpace &targetColorSpace);

private:
    QVFileEnumerator fileEnumerator {this};

    QPixmap loadedPixmap;
    QMovie loadedMovie;

    FileDetails currentFileDetails;

    QFutureWatcher<ReadData> loadFutureWatcher;

    Qv::PreloadMode preloadingMode {Qv::PreloadMode::Adjacent};
    Qv::ColorSpaceConversion colorSpaceConversion {Qv::ColorSpaceConversion::AutoDetect};

    static QCache<QString, ReadData> pixmapCache;

    QStringList lastFilesPreloaded;
    QSet<QString> preloadsInProgress;
    QString waitingOnPreloadPath;

    int largestDimension {1920};

    bool waitingOnLoad {false};

    QVRandom randomGenerator;
};

#endif // QVIMAGECORE_H
