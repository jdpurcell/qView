#ifndef QVFILEENUMERATOR_H
#define QVFILEENUMERATOR_H

#include "qvnamespace.h"
#include <QObject>

class QVFileEnumerator : public QObject
{
    Q_OBJECT
public:
    struct CompatibleFile
    {
        QString absoluteFilePath;
        QString fileName;

        // Only populated if needed for sorting
        qint64 lastModified;
        qint64 lastCreated;
        qint64 size;
        QString mimeType;
    };

    explicit QVFileEnumerator(QObject *parent = nullptr);

    QList<CompatibleFile> getCompatibleFiles(const QString &dirPath);
    bool getIsLoopFoldersEnabled() const { return isLoopFoldersEnabled; }
    void loadSettings();

protected:
    void sortCompatibleFiles(QList<CompatibleFile> &fileList);
    unsigned getRandomSortSeed(const QString &dirPath, const int fileCount);

private:
    const quint32 baseRandomSortSeed {static_cast<quint32>(std::chrono::system_clock::now().time_since_epoch().count())};

    bool isLoopFoldersEnabled {true};
    Qv::SortMode sortMode {Qv::SortMode::Name};
    bool sortDescending {false};
    bool allowMimeContentDetection {false};
    bool skipHiddenFiles {false};
};

#endif // QVFILEENUMERATOR_H
