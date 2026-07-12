#ifndef QVIMAGELOADER_H
#define QVIMAGELOADER_H

#include <optional>
#include <memory>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QObject>

class QVImageLoader : public QObject
{
    Q_OBJECT

public:
    struct ErrorData
    {
        int errorNum;
        QString errorString;
    };

    struct Result
    {
        QImage image;
        QString absoluteFilePath;
        qint64 fileSize = 0;
        QDateTime lastModified;
        bool isMultiFrameImage = false;
        QSize intrinsicSize;
        std::optional<ErrorData> errorData;
    };

    struct DesiredImage
    {
        QString absoluteFilePath;
        int priority = 0;
    };

    explicit QVImageLoader(QObject *parent = nullptr);
    ~QVImageLoader() override;

    void setLargestDimension(int value);

    quint64 requestImage(const QString &absoluteFilePath, bool forceReload = false);
    void setDesiredImages(const QList<DesiredImage> &desiredImages);
    void clear();

signals:
    void imageReady(quint64 requestId, const QVImageLoader::Result &result);
    void loadStarted(const QString &absoluteFilePath, int priority);

private:
    struct FileIdentity
    {
        qint64 fileSize = 0;
        QDateTime lastModified;

        bool operator==(const FileIdentity &other) const;
        bool operator!=(const FileIdentity &other) const { return !(*this == other); }
    };

    enum class State
    {
        Queued,
        Loading,
        Cached
    };

    struct Entry
    {
        int priority = 0;
        bool desired = false;
        bool reloadAfterFinish = false;
        State state = State::Queued;
        FileIdentity expectedIdentity;
        FileIdentity startedIdentity;
        quint64 generation = 0;
        std::optional<Result> result;
    };

    struct PendingRequest
    {
        quint64 id;
        QString absoluteFilePath;
    };

    static QString normalizePath(const QString &path);
    static FileIdentity getFileIdentity(const QString &absoluteFilePath);
    static FileIdentity getFileIdentity(const Result &result);
    static Result readFile(const QString &absoluteFilePath, int largestDimension);

    bool isWanted(const QString &absoluteFilePath, const Entry &entry) const;
    void queueCachedDelivery(quint64 requestId, const QString &absoluteFilePath);
    void deliverResult(quint64 requestId, const QString &absoluteFilePath);
    void startReadyJobs();
    void startJob(const QString &absoluteFilePath);
    void jobFinished(const QString &absoluteFilePath, quint64 generation, Result result);

    QHash<QString, Entry> entries;
    std::optional<PendingRequest> pendingRequest;
    std::shared_ptr<int> lifetimeToken = std::make_shared<int>(0);

    quint64 nextRequestId = 0;
    int largestDimension = 1920;
};

Q_DECLARE_METATYPE(QVImageLoader::Result)

#endif // QVIMAGELOADER_H
