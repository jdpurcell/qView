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

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#include <QColorSpace>
#else
typedef QString QColorSpace;
#endif

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
        QList<QVFileEnumerator::CompatibleFile> folderFileInfoList;
        int loadedIndexInFolder = -1;
        bool isLoadRequested = false;
        bool isPixmapLoaded = false;
        bool isMovieLoaded = false;
        QSize baseImageSize;
        QSize loadedPixmapSize;
        QElapsedTimer timeSinceLoaded;
        std::optional<ErrorData> errorData;

        void updateLoadedIndexInFolder();
    };

    struct ReadData
    {
        QPixmap pixmap;
        QString absoluteFilePath;
        qint64 fileSize;
        QSize imageSize;
        QColorSpace targetColorSpace;
        std::optional<ErrorData> errorData;
    };

    struct GoToFileResult
    {
        bool reachedEnd = false;
    };

    explicit QVImageCore(QObject *parent = nullptr);

    void loadFile(const QString &fileName, bool isReloading = false);
    ReadData readFile(const QString &fileName, const QColorSpace &targetColorSpace);
    void loadPixmap(const ReadData &readData);
    void closeImage();
    GoToFileResult goToFile(const Qv::GoToFileMode mode, const int index = 0);
    void updateFolderInfo(QString dirPath = QString());
    void requestCaching();
    void requestCachingFile(const QString &filePath, const QColorSpace &targetColorSpace);
    void addToCache(const ReadData &readImageAndFileInfo);
    static QString getPixmapCacheKey(const QString &absoluteFilePath, const qint64 &fileSize, const QColorSpace &targetColorSpace);
    QColorSpace getTargetColorSpace() const;
    QColorSpace detectDisplayColorSpace() const;
    static bool removeTinyDataTagsFromIccProfile(QByteArray &profile);

    void settingsUpdated();

    void jumpToNextFrame();
    void setPaused(bool desiredState);
    void setSpeed(int desiredSpeed);

    QPixmap scaleExpensively(const QSizeF desiredSize);

    //returned const reference is read-only
    const QPixmap& getLoadedPixmap() const {return loadedPixmap; }
    const QMovie& getLoadedMovie() const {return loadedMovie; }
    const FileDetails& getCurrentFileDetails() const {return currentFileDetails; }

signals:
    void animatedFrameChanged(QRect rect);

    void fileChanged();

protected:
    void loadEmptyPixmap();
    FileDetails getEmptyFileDetails();

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

    int largestDimension {0};

    bool waitingOnLoad {false};
};

#endif // QVIMAGECORE_H
