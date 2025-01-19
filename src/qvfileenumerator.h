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
        qint64 numericSortKey;
        QString stringSortKey;
    };

    explicit QVFileEnumerator(QObject *parent = nullptr);

    QList<CompatibleFile> getCompatibleFiles(const QString &dirPath) const;
    bool getIsLoopFoldersEnabled() const { return isLoopFoldersEnabled; }
    void loadSettings();

protected:
    void sortCompatibleFiles(QList<CompatibleFile> &fileList) const;
    qint64 getRandomSortKey(const QString &filePath, const int fileCount) const;

private:
    const quint32 baseRandomSortSeed {static_cast<quint32>(std::chrono::system_clock::now().time_since_epoch().count())};

    bool isLoopFoldersEnabled {true};
    Qv::SortMode sortMode {Qv::SortMode::Name};
    bool sortDescending {false};
    bool allowMimeContentDetection {false};
    bool skipHiddenFiles {false};
};

#endif // QVFILEENUMERATOR_H
