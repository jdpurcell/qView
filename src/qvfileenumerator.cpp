#include "qvfileenumerator.h"
#include "qvapplication.h"

QVFileEnumerator::QVFileEnumerator(QObject *parent)
    : QObject{parent}
{
    collator.setNumericMode(true);
    loadSettings();
}

QList<QVFileEnumerator::CompatibleFile> QVFileEnumerator::getCompatibleFiles(const QString &dirPath) const
{
    QList<CompatibleFile> fileList;

    const QMimeDatabase mimeDb;
    const auto &extensions = qvApp->getFileExtensionSet();
    const auto &disabledExtensions = qvApp->getDisabledFileExtensions();
    const auto &mimeTypes = qvApp->getMimeTypeNameSet();
    const QMimeDatabase::MatchMode mimeMatchMode = allowMimeContentDetection ? QMimeDatabase::MatchDefault : QMimeDatabase::MatchExtension;

    QDir::Filters filters = QDir::Files;
    if (!skipHiddenFiles)
        filters |= QDir::Hidden;

    const QFileInfoList candidateFiles = QDir(dirPath).entryInfoList(filters, QDir::Unsorted);
    for (const QFileInfo &fileInfo : candidateFiles)
    {
        const QString absoluteFilePath = fileInfo.absoluteFilePath();
        const QString fileName = fileInfo.fileName();
        const QString suffix = fileInfo.suffix().toLower();
        bool matched = !suffix.isEmpty() && extensions.contains("." + suffix);
        QString mimeType;

        if (!matched || sortMode == Qv::SortMode::Type)
        {
            mimeType = mimeDb.mimeTypeForFile(absoluteFilePath, mimeMatchMode).name();
            matched |= mimeTypes.contains(mimeType) && (suffix.isEmpty() || !disabledExtensions.contains("." + suffix));
        }

        // ignore macOS ._ metadata files
        if (fileName.startsWith("._"))
        {
            matched = false;
        }

        if (matched)
        {
            qint64 numericSortKey = 0;
            QString stringSortKey;
            switch (sortMode)
            {
            case Qv::SortMode::DateModified:
                numericSortKey = getFileTimeSortKey(fileInfo, QFileDevice::FileModificationTime);
                break;
            case Qv::SortMode::DateCreated:
                numericSortKey = getFileTimeSortKey(fileInfo, QFileDevice::FileBirthTime);
                break;
            case Qv::SortMode::Size:
                numericSortKey = fileInfo.size();
                break;
            case Qv::SortMode::Type:
                stringSortKey = mimeType;
                break;
            case Qv::SortMode::Random:
                numericSortKey = getRandomSortKey(absoluteFilePath);
                break;
            default:
                stringSortKey = fileName;
                break;
            };
            fileList.append({
                absoluteFilePath,
                numericSortKey,
                stringSortKey
            });
        }
    }

    sortCompatibleFiles(fileList);

    return fileList;
}

void QVFileEnumerator::sortCompatibleFiles(QList<CompatibleFile> &fileList) const
{
    std::sort(
        fileList.begin(),
        fileList.end(),
        [&](const CompatibleFile &file1, const CompatibleFile &file2) {
            int result =
                file1.numericSortKey < file2.numericSortKey ? -1 :
                file1.numericSortKey > file2.numericSortKey ? 1 :
                collator.compare(file1.stringSortKey, file2.stringSortKey);
            if (result == 0)
                result = collator.compare(file1.absoluteFilePath, file2.absoluteFilePath);
            return sortDescending ? (result > 0) : (result < 0);
        }
    );
}

qint64 QVFileEnumerator::getFileTimeSortKey(const QFileInfo &fileInfo, const QFileDevice::FileTime type) const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    return fileInfo.fileTime(type, QTimeZone::UTC).toMSecsSinceEpoch();
#else
    return fileInfo.fileTime(type).toMSecsSinceEpoch();
#endif
}

qint64 QVFileEnumerator::getRandomSortKey(const QString &filePath) const
{
    const QString seed = QString::number(baseRandomSortSeed, 16) + filePath;
    const QByteArray hash = QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Md5);
    return static_cast<qint64>(hash.toHex().left(16).toULongLong(nullptr, 16));
}

void QVFileEnumerator::loadSettings()
{
    const auto &settingsManager = qvApp->getSettingsManager();

    //loop folders
    isLoopFoldersEnabled = settingsManager.getBoolean("loopfoldersenabled");

    //sort mode
    sortMode = settingsManager.getEnum<Qv::SortMode>("sortmode");

    //sort ascending
    sortDescending = settingsManager.getBoolean("sortdescending");

    //allow mime content detection
    allowMimeContentDetection = settingsManager.getBoolean("allowmimecontentdetection");

    //skip hidden files
    skipHiddenFiles = settingsManager.getBoolean("skiphidden");
}
