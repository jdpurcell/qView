#include "qvfileenumerator.h"
#include "qvapplication.h"
#include <random>

QVFileEnumerator::QVFileEnumerator(QObject *parent)
    : QObject{parent}
{
    loadSettings();
}

QList<QVFileEnumerator::CompatibleFile> QVFileEnumerator::getCompatibleFiles(const QString &dirPath)
{
    QList<CompatibleFile> fileList;

    QMimeDatabase mimeDb;
    const auto &extensions = qvApp->getFileExtensionSet();
    const auto &disabledExtensions = qvApp->getDisabledFileExtensions();
    const auto &mimeTypes = qvApp->getMimeTypeNameSet();

    QMimeDatabase::MatchMode mimeMatchMode = allowMimeContentDetection ? QMimeDatabase::MatchDefault : QMimeDatabase::MatchExtension;

    QDir::Filters filters = QDir::Files;
    if (!skipHiddenFiles)
        filters |= QDir::Hidden;

    const QFileInfoList currentFolder = QDir(dirPath).entryInfoList(filters, QDir::Unsorted);
    for (const QFileInfo &fileInfo : currentFolder)
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
            fileList.append({
                absoluteFilePath,
                fileName,
                sortMode == Qv::SortMode::DateModified ? fileInfo.lastModified().toMSecsSinceEpoch() : 0,
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
                sortMode == Qv::SortMode::DateCreated ? fileInfo.birthTime().toMSecsSinceEpoch() : 0,
#else
                sortMode == Qv::SortMode::DateCreated ? fileInfo.created().toMSecsSinceEpoch() : 0,
#endif
                sortMode == Qv::SortMode::Size ? fileInfo.size() : 0,
                sortMode == Qv::SortMode::Type ? mimeType : QString()
            });
        }
    }

    sortCompatibleFiles(fileList);

    return fileList;
}

void QVFileEnumerator::sortCompatibleFiles(QList<CompatibleFile> &fileList)
{
    if (sortMode == Qv::SortMode::Name)
    {
        QCollator collator;
        collator.setNumericMode(true);
        std::sort(fileList.begin(),
                  fileList.end(),
                  [&collator, this](const CompatibleFile &file1, const CompatibleFile &file2)
        {
            if (sortDescending)
                return collator.compare(file1.fileName, file2.fileName) > 0;
            else
                return collator.compare(file1.fileName, file2.fileName) < 0;
        });
    }
    else if (sortMode == Qv::SortMode::DateModified)
    {
        std::sort(fileList.begin(),
                  fileList.end(),
                  [this](const CompatibleFile &file1, const CompatibleFile &file2)
        {
            if (sortDescending)
                return file1.lastModified < file2.lastModified;
            else
                return file1.lastModified > file2.lastModified;
        });
    }
    else if (sortMode == Qv::SortMode::DateCreated)
    {
        std::sort(fileList.begin(),
                  fileList.end(),
                  [this](const CompatibleFile &file1, const CompatibleFile &file2)
        {
            if (sortDescending)
                return file1.lastCreated < file2.lastCreated;
            else
                return file1.lastCreated > file2.lastCreated;
        });

    }
    else if (sortMode == Qv::SortMode::Size)
    {
        std::sort(fileList.begin(),
                  fileList.end(),
                  [this](const CompatibleFile &file1, const CompatibleFile &file2)
        {
            if (sortDescending)
                return file1.size < file2.size;
            else
                return file1.size > file2.size;
        });
    }
    else if (sortMode == Qv::SortMode::Type)
    {
        QCollator collator;
        std::sort(fileList.begin(),
                  fileList.end(),
                  [&collator, this](const CompatibleFile &file1, const CompatibleFile &file2)
        {
            if (sortDescending)
                return collator.compare(file1.mimeType, file2.mimeType) > 0;
            else
                return collator.compare(file1.mimeType, file2.mimeType) < 0;
        });
    }
    else if (sortMode == Qv::SortMode::Random)
    {
        unsigned randomSortSeed = getRandomSortSeed(QFileInfo(fileList.value(0).absoluteFilePath).path(), fileList.count());
        std::shuffle(fileList.begin(), fileList.end(), std::default_random_engine(randomSortSeed));
    }
}

unsigned QVFileEnumerator::getRandomSortSeed(const QString &dirPath, const int fileCount)
{
    QString seed = QString::number(baseRandomSortSeed, 16) + dirPath + QString::number(fileCount, 16);
    QByteArray hash = QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Md5);
    return hash.toHex().left(8).toUInt(nullptr, 16);
}

void QVFileEnumerator::loadSettings()
{
    auto &settingsManager = qvApp->getSettingsManager();

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
