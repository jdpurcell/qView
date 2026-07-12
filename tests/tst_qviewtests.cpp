#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThreadPool>

#include "qvapplication.h"
#include "qvimageloader.h"

class ImageLoaderTests : public QObject
{
    Q_OBJECT

private slots:
    void testImageLoaderPriorities();
    void testImageLoaderCacheAndAttachment();
    void testImageLoaderRetainedDuringDelivery();
    void testImageLoaderForegroundRequestPreservesCache();
    void testImageLoaderSupersededForegroundDiscarded();
    void testImageLoaderDisabledRetention();
    void testImageLoaderCachedErrorRetry();
    void testImageLoaderDestructionDuringLoad();
};

class ActionManagerTests : public QObject
{
    Q_OBJECT

private slots:
    void testClonedActionsUntracked();
};

static QString createTestImage(const QTemporaryDir &dir, const QString &name, const QColor color)
{
    const QString path = dir.filePath(name + ".png");
    QImage image(32, 32, QImage::Format_RGB32);
    image.fill(color);
    if (!image.save(path))
        return {};
    return path;
}

void ImageLoaderTests::testImageLoaderPriorities()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString target = createTestImage(dir, "target", Qt::red);
    const QString adjacentBefore = createTestImage(dir, "adjacent-before", Qt::green);
    const QString adjacentAfter = createTestImage(dir, "adjacent-after", Qt::blue);
    const QString extendedBefore = createTestImage(dir, "extended-before", Qt::cyan);
    const QString extendedAfter = createTestImage(dir, "extended-after", Qt::magenta);
    QVERIFY(!target.isEmpty());
    QVERIFY(!adjacentBefore.isEmpty());
    QVERIFY(!adjacentAfter.isEmpty());
    QVERIFY(!extendedBefore.isEmpty());
    QVERIFY(!extendedAfter.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);
    QVERIFY(startedSpy.isValid());
    QVERIFY(readySpy.isValid());

    const quint64 requestId = loader.requestImage(target);
    loader.setDesiredImages({
        {target, 0},
        {adjacentBefore, 1},
        {adjacentAfter, 1},
        {extendedBefore, 2},
        {extendedAfter, 2}
    });

    QCOMPARE(startedSpy.size(), 1);
    QCOMPARE(startedSpy.at(0).at(0).toString(), target);
    QCOMPARE(startedSpy.at(0).at(1).toInt(), 0);

    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    QCOMPARE(readySpy.at(0).at(0).toULongLong(), requestId);
    const auto result = qvariant_cast<QVImageLoader::Result>(readySpy.at(0).at(1));
    QCOMPARE(result.absoluteFilePath, target);
    QVERIFY(!result.image.isNull());

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.size(), 5, 5000);
    const QList<int> expectedPriorities {0, 1, 1, 2, 2};
    for (int i = 0; i < expectedPriorities.size(); ++i)
        QCOMPARE(startedSpy.at(i).at(1).toInt(), expectedPriorities.at(i));
}

void ImageLoaderTests::testImageLoaderCacheAndAttachment()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = createTestImage(dir, "image", Qt::yellow);
    QVERIFY(!path.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);
    const QList<QVImageLoader::DesiredImage> desiredImages {{path, 0}};

    loader.requestImage(path);
    loader.setDesiredImages(desiredImages);
    const quint64 attachedRequestId = loader.requestImage(path);
    QCOMPARE(startedSpy.size(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    QCOMPARE(readySpy.at(0).at(0).toULongLong(), attachedRequestId);

    loader.requestImage(path);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
    QCOMPARE(startedSpy.size(), 1);

    loader.setDesiredImages({});
    loader.requestImage(path);
    loader.setDesiredImages(desiredImages);
    QCOMPARE(startedSpy.size(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 3, 5000);
}

void ImageLoaderTests::testImageLoaderRetainedDuringDelivery()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = createTestImage(dir, "image", Qt::yellow);
    QVERIFY(!path.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);
    connect(&loader, &QVImageLoader::imageReady, this,
        [&loader, path](quint64, const QVImageLoader::Result &) {
            loader.setDesiredImages({{path, 0}});
        });

    loader.requestImage(path);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);

    loader.requestImage(path);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
    QCOMPARE(startedSpy.size(), 1);
}

void ImageLoaderTests::testImageLoaderForegroundRequestPreservesCache()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cachedPath = createTestImage(dir, "cached", Qt::green);
    const QString foregroundPath = createTestImage(dir, "foreground", Qt::blue);
    QVERIFY(!cachedPath.isEmpty());
    QVERIFY(!foregroundPath.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);

    loader.requestImage(cachedPath);
    loader.setDesiredImages({{cachedPath, 0}});
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);

    loader.requestImage(foregroundPath);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
    QCOMPARE(startedSpy.size(), 2);

    loader.requestImage(cachedPath);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 3, 5000);
    QCOMPARE(startedSpy.size(), 2);
}

void ImageLoaderTests::testImageLoaderSupersededForegroundDiscarded()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = createTestImage(dir, "first", Qt::red);
    const QString secondPath = createTestImage(dir, "second", Qt::blue);
    QVERIFY(!firstPath.isEmpty());
    QVERIFY(!secondPath.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);

    loader.requestImage(firstPath);
    const quint64 secondRequestId = loader.requestImage(secondPath);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    QCOMPARE(readySpy.at(0).at(0).toULongLong(), secondRequestId);

    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    loader.requestImage(firstPath);
    QCOMPARE(startedSpy.size(), 3);
}

void ImageLoaderTests::testImageLoaderDisabledRetention()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = createTestImage(dir, "image", Qt::black);
    QVERIFY(!path.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);

    loader.requestImage(path);
    loader.setDesiredImages({});
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    QCOMPARE(startedSpy.size(), 1);

    loader.requestImage(path);
    loader.setDesiredImages({});
    QCOMPARE(startedSpy.size(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
}

void ImageLoaderTests::testImageLoaderCachedErrorRetry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.filePath("invalid.png");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("not an image"), 12);
    file.close();

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);
    const QList<QVImageLoader::DesiredImage> desiredImages {{path, 0}};

    loader.requestImage(path);
    loader.setDesiredImages(desiredImages);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    const auto firstResult = qvariant_cast<QVImageLoader::Result>(readySpy.at(0).at(1));
    QVERIFY(firstResult.errorData.has_value());

    loader.requestImage(path);
    QCOMPARE(startedSpy.size(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
    const auto secondResult = qvariant_cast<QVImageLoader::Result>(readySpy.at(1).at(1));
    QVERIFY(secondResult.errorData.has_value());
}

void ImageLoaderTests::testImageLoaderDestructionDuringLoad()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString target = createTestImage(dir, "target", Qt::red);
    const QString queuedPreload = createTestImage(dir, "queued-preload", Qt::blue);
    QVERIFY(!target.isEmpty());
    QVERIFY(!queuedPreload.isEmpty());

    QStringList startedPaths;
    auto *loader = new QVImageLoader;
    connect(loader, &QVImageLoader::loadStarted, this,
        [&startedPaths](const QString &path, int) { startedPaths.append(path); });

    loader->requestImage(target);
    loader->setDesiredImages({
        {target, 0},
        {queuedPreload, 1}
    });
    QCOMPARE(startedPaths, QStringList {target});

    delete loader;
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();

    QCOMPARE(startedPaths, QStringList {target});
}

void ActionManagerTests::testClonedActionsUntracked()
{
    // Get initial counts of certain actions
    int fullscreenCount = qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length();
    int openCount = qvApp->getActionManager().getAllInstancesOfAction("open").length();
    qDebug() << fullscreenCount;

    // Have window clone actions
    MainWindow window;
    window.show();
    // Make sure they were cloned
    QVERIFY(qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length() != fullscreenCount);
    QVERIFY(qvApp->getActionManager().getAllInstancesOfAction("open").length() != openCount);
    // Untrack them
    window.close();

    // Make sure the count has not changed from the initial
    QCOMPARE(qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length(), fullscreenCount);
    QCOMPARE(qvApp->getActionManager().getAllInstancesOfAction("open").length(), openCount);
}

int main(int argc, char *argv[])
{
    QVApplication app(argc, argv);
    qRegisterMetaType<QVImageLoader::Result>();

    ImageLoaderTests imageLoaderTests;
    ActionManagerTests actionManagerTests;
    int result = QTest::qExec(&imageLoaderTests, argc, argv);
    result |= QTest::qExec(&actionManagerTests, argc, argv);
    return result;
}

#include "tst_qviewtests.moc"
