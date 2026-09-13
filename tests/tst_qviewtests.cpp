#include <QtTest>
#include <QFile>
#include <QBuffer>
#include <QDataStream>
#include <QImageReader>
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QThreadPool>

#include "qvapplication.h"
#include "qvimageloader.h"

class ImageLoaderTests : public QObject
{
    Q_OBJECT

private slots:
    void testAnimationStillPlays();
    void testMultiFrameImages();
    void testImageLoaderFrameCache();
    void testImageLoaderFrameRequestsInFlight();
    void testMultiFrameCore();
    void testMultiFrameReplacement();
    void testSampleTiff();
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
    void testMultiFrameView();
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

// Uncompressed RGB TIFF pages with differing sizes and embedded ICC profiles.
static QString createTestTiff(const QTemporaryDir &dir, const int pageCount = 2)
{
    QFile file(dir.filePath("pages.tif"));
    if (!file.open(QIODevice::WriteOnly))
        return {};
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("II", 2);
    out << quint16(42) << quint32(8);
    const QByteArray profile = QColorSpace(QColorSpace::DisplayP3).iccProfile();
    for (int page = 0; page < pageCount; ++page)
    {
        const quint32 width = page == 0 ? 16 : 32;
        const quint32 height = page == 0 ? 12 : 24;
        const quint32 bitsOffset = file.pos() + 2 + 11 * 12 + 4;
        const quint32 profileOffset = bitsOffset + 6;
        const quint32 pixelsOffset = profileOffset + profile.size();
        out << quint16(11);
        const auto tag = [&out](quint16 id, quint16 type, quint32 count, quint32 value) {
            out << id << type << count << value;
        };
        tag(256, 4, 1, width);
        tag(257, 4, 1, height);
        tag(258, 3, 3, bitsOffset);
        tag(259, 3, 1, 1);
        tag(262, 3, 1, 2);
        tag(273, 4, 1, pixelsOffset);
        tag(277, 3, 1, 3);
        tag(278, 4, 1, height);
        tag(279, 4, 1, width * height * 3);
        tag(284, 3, 1, 1);
        tag(34675, 7, profile.size(), profileOffset);
        out << quint32(page + 1 < pageCount ? pixelsOffset + width * height * 3 : 0);
        out << quint16(8) << quint16(8) << quint16(8);
        out.writeRawData(profile.constData(), profile.size());
        for (quint32 pixel = 0; pixel < width * height; ++pixel)
            out << quint8(page == 0 ? 180 : 60) << quint8(100) << quint8(80);
    }
    return file.fileName();
}

static QString createTestIcon(const QTemporaryDir &dir)
{
    QList<QByteArray> images;
    for (int size : {16, 32})
    {
        QImage image(size, size, QImage::Format_ARGB32);
        image.fill(size == 16 ? Qt::red : Qt::blue);
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG"))
            return {};
        images.append(data);
    }
    QFile file(dir.filePath("images.ico"));
    if (!file.open(QIODevice::WriteOnly))
        return {};
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out << quint16(0) << quint16(1) << quint16(2);
    quint32 offset = 6 + 2 * 16;
    for (int i = 0; i < images.size(); ++i)
    {
        out << quint8(i == 0 ? 16 : 32) << quint8(i == 0 ? 16 : 32)
            << quint8(0) << quint8(0) << quint16(1) << quint16(32)
            << quint32(images[i].size()) << offset;
        offset += images[i].size();
    }
    for (const auto &data : images)
        out.writeRawData(data.constData(), data.size());
    return file.fileName();
}

void ImageLoaderTests::testAnimationStillPlays()
{
    QTemporaryDir dir;
    QFile file(dir.filePath("animation.gif"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    // Two one-pixel frames, a global black/white palette, and a repeating animation.
    const QByteArray gif = QByteArray::fromHex(
        "47494638396101000100800000000000ffffff"
        "21ff0b4e45545343415045322e300301000000"
        "21f90400640000002c0000000001000100000202440100"
        "21f90400640000002c00000000010001000002024c01003b");
    QCOMPARE(file.write(gif), gif.size());
    file.close();
    QVImageCore core;
    QSignalSpy files(&core, &QVImageCore::fileChanged);
    core.loadFile(file.fileName());
    QTRY_COMPARE(files.size(), 1);
    QVERIFY(core.getCurrentFileDetails().isMovieLoaded);
    QVERIFY(!core.getCurrentFileDetails().isMultiFrameImage);
    QCOMPARE(core.getLoadedMovie().state(), QVMovie::Running);
    core.jumpToNextFrame();
    QCOMPARE(core.getLoadedMovie().state(), QVMovie::Paused);
    QCOMPARE(core.getLoadedMovie().currentFrameNumber(), 1);
    core.jumpToPreviousFrame();
    QCOMPARE(core.getLoadedMovie().currentFrameNumber(), 0);
}

void ImageLoaderTests::testMultiFrameImages()
{
    QTemporaryDir dir;
    const QString tiff = createTestTiff(dir);
    const QString icon = createTestIcon(dir);
    QVERIFY(!tiff.isEmpty());
    QVERIFY(!icon.isEmpty());
    QVImageLoader loader;
    QSignalSpy ready(&loader, &QVImageLoader::imageReady);
    loader.requestImage(tiff);
    QTRY_COMPARE(ready.size(), 1);
    auto result = qvariant_cast<QVImageLoader::Result>(ready.takeFirst().at(1));
    QVERIFY(!result.errorData.has_value());
    QVERIFY(result.isMultiFrameImage);
    QCOMPARE(result.frameCount, 2);
    QCOMPARE(result.frameNumber, 0);
    QCOMPARE(result.image.size(), QSize(16, 12));

    loader.requestImage(tiff, false, 1);
    QTRY_COMPARE(ready.size(), 1);
    result = qvariant_cast<QVImageLoader::Result>(ready.takeFirst().at(1));
    QVERIFY(!result.errorData.has_value());
    QCOMPARE(result.frameNumber, 1);
    QCOMPARE(result.image.size(), QSize(32, 24));

    loader.requestImage(icon);
    QTRY_COMPARE(ready.size(), 1);
    result = qvariant_cast<QVImageLoader::Result>(ready.takeFirst().at(1));
    QVERIFY(!result.errorData.has_value());
    QVERIFY(result.isMultiFrameImage);
    QCOMPARE(result.frameNumber, 1);
    QCOMPARE(result.image.size(), QSize(32, 32));
    loader.requestImage(icon, false, 0);
    QTRY_COMPARE(ready.size(), 1);
    result = qvariant_cast<QVImageLoader::Result>(ready.takeFirst().at(1));
    QVERIFY(!result.errorData.has_value());
    QCOMPARE(result.image.size(), QSize(16, 16));

    loader.requestImage(dir.filePath("missing.tif"), false, 99);
    QTRY_COMPARE(ready.size(), 1);
    result = qvariant_cast<QVImageLoader::Result>(ready.takeFirst().at(1));
    QVERIFY(result.errorData.has_value());
    QCOMPARE(result.frameNumber, 0);
}

void ImageLoaderTests::testImageLoaderFrameCache()
{
    QTemporaryDir dir;
    for (const QString &path : {createTestTiff(dir), createTestIcon(dir)})
    {
        const bool isIcon = path.endsWith(".ico");
        const int defaultFrame = isIcon ? 1 : 0;
        const int otherFrame = isIcon ? 0 : 1;
        QVImageLoader loader;
        QSignalSpy ready(&loader, &QVImageLoader::imageReady);
        QSignalSpy started(&loader, &QVImageLoader::loadStarted);
        const QList<QVImageLoader::DesiredImage> desired {{path, 0}};
        loader.requestImage(path);
        loader.setDesiredImages(desired);
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(qvariant_cast<QVImageLoader::Result>(ready.takeFirst().at(1)).frameNumber, defaultFrame);
        QCOMPARE(started.size(), 1);

        loader.requestImage(path, false, otherFrame);
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(qvariant_cast<QVImageLoader::Result>(ready.takeFirst().at(1)).frameNumber, otherFrame);
        QCOMPARE(started.size(), 2);

        // Preload maintenance retains the selected page, and a matching request hits the cache.
        loader.setDesiredImages(desired);
        loader.requestImage(path, false, otherFrame);
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(qvariant_cast<QVImageLoader::Result>(ready.takeFirst().at(1)).frameNumber, otherFrame);
        QCOMPARE(started.size(), 2);

        loader.requestImage(path);
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(qvariant_cast<QVImageLoader::Result>(ready.takeFirst().at(1)).frameNumber, defaultFrame);
        QCOMPARE(started.size(), 3);

        // Default selection and an explicit frame remain distinct even when they decode the same image.
        loader.requestImage(path, false, defaultFrame);
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(qvariant_cast<QVImageLoader::Result>(ready.takeFirst().at(1)).frameNumber, defaultFrame);
        QCOMPARE(started.size(), 4);
    }
}

void ImageLoaderTests::testImageLoaderFrameRequestsInFlight()
{
    QTemporaryDir dir;
    const QString path = createTestTiff(dir);
    QVImageLoader loader;
    QSignalSpy ready(&loader, &QVImageLoader::imageReady);
    QSignalSpy started(&loader, &QVImageLoader::loadStarted);

    // A foreground request for a different page must not accept the default preload's result.
    loader.setDesiredImages({{path, 1}});
    const quint64 requestId = loader.requestImage(path, false, 1);
    loader.setDesiredImages({{path, 0}});
    QCOMPARE(started.size(), 1);
    QTRY_COMPARE(ready.size(), 1);
    QCOMPARE(ready.at(0).at(0).toULongLong(), requestId);
    auto result = qvariant_cast<QVImageLoader::Result>(ready.takeFirst().at(1));
    QVERIFY(!result.errorData.has_value());
    QCOMPARE(result.frameNumber, 1);
    QCOMPARE(result.image.size(), QSize(32, 24));
    QCOMPARE(started.size(), 2);

    // A queued cached delivery must also be superseded when its frame selection changes.
    loader.requestImage(path, false, 1);
    const quint64 defaultRequest = loader.requestImage(path);
    QTRY_COMPARE(ready.size(), 1);
    QCOMPARE(ready.at(0).at(0).toULongLong(), defaultRequest);
    QCOMPARE(qvariant_cast<QVImageLoader::Result>(ready.takeFirst().at(1)).frameNumber, 0);
    QCOMPARE(started.size(), 3);

    // If requests switch back to the in-flight selection, that job can be used directly.
    loader.requestImage(path, false, 1);
    loader.requestImage(path);
    const quint64 latestRequest = loader.requestImage(path, false, 1);
    QTRY_COMPARE(ready.size(), 1);
    QCOMPARE(ready.at(0).at(0).toULongLong(), latestRequest);
    QCOMPARE(qvariant_cast<QVImageLoader::Result>(ready.takeFirst().at(1)).frameNumber, 1);
    QCOMPARE(started.size(), 4);

    // A fresh explicit request decodes the requested page in one job.
    loader.clear();
    loader.requestImage(path, false, 1);
    QTRY_COMPARE(ready.size(), 1);
    QCOMPARE(qvariant_cast<QVImageLoader::Result>(ready.takeFirst().at(1)).frameNumber, 1);
    QCOMPARE(started.size(), 5);
}

void ImageLoaderTests::testMultiFrameCore()
{
    QTemporaryDir dir;
    const QString path = createTestTiff(dir);
    QVImageCore core;
    QSignalSpy files(&core, &QVImageCore::fileChanged);
    core.loadFile(path);
    QTRY_COMPARE(files.size(), 1);
    QVERIFY(core.getCurrentFileDetails().isMultiFrameImage);
    QVERIFY(!core.getCurrentFileDetails().isMovieLoaded);
    QCOMPARE(core.getLoadedMovie().state(), QVMovie::NotRunning);
    core.jumpToPreviousFrame();
    QTRY_COMPARE(files.size(), 2);
    QCOMPARE(core.getCurrentFileDetails().frameNumber, 1);
    QCOMPARE(core.getCurrentFileDetails().baseImageSize, QSize(32, 24));
    QCOMPARE(core.getCurrentFileDetails().loadedPixmapSize, QSize(32, 24));
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QVERIFY(reader.jumpToImage(1));
    QImage expected = reader.read();
    QVERIFY(expected.colorSpace().isValid());
    if (core.getCurrentFileDetails().targetColorSpace.isValid())
        expected.convertToColorSpace(core.getCurrentFileDetails().targetColorSpace);
    QCOMPARE(core.getLoadedPixmap().toImage().pixelColor(0, 0), expected.pixelColor(0, 0));
    core.jumpToNextFrame();
    QTRY_COMPARE(files.size(), 3);
    QCOMPARE(core.getCurrentFileDetails().frameNumber, 0);
    QCOMPARE(core.getCurrentFileDetails().baseImageSize, QSize(16, 12));

    // Late frame results must not replace a newly requested file or reopen a closed image.
    core.jumpToNextFrame();
    core.loadFile(createTestImage(dir, "single", Qt::green));
    QTRY_COMPARE(files.size(), 4);
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    QCOMPARE(files.size(), 4);
    QVERIFY(!core.getCurrentFileDetails().isMultiFrameImage);
    core.loadFile(path);
    QTRY_COMPARE(files.size(), 5);
    core.jumpToNextFrame();
    core.closeImage();
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    QCOMPARE(files.size(), 6);
    QVERIFY(!core.getCurrentFileDetails().isPixmapLoaded);

    core.loadFile(path);
    QTRY_COMPARE(files.size(), 7);
    QVERIFY(QFile::remove(path));
    core.jumpToNextFrame();
    QTRY_COMPARE(files.size(), 8);
    QVERIFY(core.getCurrentFileDetails().errorData.has_value());
    QVERIFY(!core.getCurrentFileDetails().isPixmapLoaded);
    QVERIFY(core.getLoadedPixmap().isNull());
    // Page failures have the same limitations as file failures: reopen to recover.
    QVERIFY(!core.getCurrentFileDetails().isMultiFrameImage);
    QCOMPARE(core.getCurrentFileDetails().baseImageSize, QSize());
    core.jumpToNextFrame();
    QCOMPARE(files.size(), 8);

    QCOMPARE(createTestTiff(dir), path);
    core.loadFile(path);
    QTRY_COMPARE(files.size(), 9);
    QVERIFY(!core.getCurrentFileDetails().errorData.has_value());
    QVERIFY(core.getCurrentFileDetails().isPixmapLoaded);
    QCOMPARE(core.getCurrentFileDetails().frameNumber, 0);
    QCOMPARE(core.getCurrentFileDetails().baseImageSize, QSize(16, 12));
}

void ImageLoaderTests::testMultiFrameReplacement()
{
    QTemporaryDir dir;
    const QString path = createTestTiff(dir);
    QVImageCore core;
    QSignalSpy files(&core, &QVImageCore::fileChanged);
    core.loadFile(path);
    QTRY_COMPARE(files.size(), 1);

    QCOMPARE(createTestTiff(dir, 3), path);
    core.jumpToNextFrame();
    QTRY_COMPARE(files.size(), 2);
    QCOMPARE(core.getCurrentFileDetails().frameCount, 3);
    core.jumpToNextFrame();
    QTRY_COMPARE(files.size(), 3);
    QCOMPARE(core.getCurrentFileDetails().frameNumber, 2);

    QCOMPARE(createTestTiff(dir, 1), path);
    core.jumpToNextFrame();
    QTRY_COMPARE(files.size(), 4);
    QVERIFY(!core.getCurrentFileDetails().isMultiFrameImage);
    QVERIFY(!core.getCurrentFileDetails().isMovieLoaded);
    QCOMPARE(core.getCurrentFileDetails().frameCount, 1);
    QCOMPARE(core.getCurrentFileDetails().baseImageSize, QSize(16, 12));
}

void ImageLoaderTests::testSampleTiff()
{
    const QString path = qEnvironmentVariable("QVIEW_TEST_TIFF");
    if (path.isEmpty())
        QSKIP("Set QVIEW_TEST_TIFF to validate an external multipage TIFF.");
    QVImageCore core;
    QSignalSpy files(&core, &QVImageCore::fileChanged);
    core.loadFile(path);
    QTRY_COMPARE_WITH_TIMEOUT(files.size(), 1, 30000);
    QVERIFY(core.getCurrentFileDetails().isMultiFrameImage);
    QCOMPARE(core.getCurrentFileDetails().frameNumber, 0);
    const int count = core.getCurrentFileDetails().frameCount;
    QVERIFY(count > 1);
    for (int i = 0; i < count; ++i)
    {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        if (i > 0)
            QVERIFY(reader.jumpToImage(i));
        QImage expected = reader.read();
        QVERIFY(!expected.isNull());
        if (!expected.colorSpace().isValid())
            expected.setColorSpace(QColorSpace::SRgb);
        if (core.getCurrentFileDetails().targetColorSpace.isValid())
            expected.convertToColorSpace(core.getCurrentFileDetails().targetColorSpace);
        QCOMPARE(core.getLoadedPixmap().toImage(), QPixmap::fromImage(expected).toImage());
        core.jumpToNextFrame();
        QTRY_COMPARE_WITH_TIMEOUT(files.size(), i + 2, 30000);
    }
    QCOMPARE(core.getCurrentFileDetails().frameNumber, 0);
    QVERIFY(!core.getCurrentFileDetails().isMovieLoaded);
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

void ActionManagerTests::testMultiFrameView()
{
    QTemporaryDir dir;
    MainWindow window;
    window.show();
    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    QSignalSpy files(view, &QVGraphicsView::fileChanged);
    const QString path = createTestTiff(dir);
    view->loadFile(path);
    QTRY_COMPARE(files.size(), 1);
    const auto initialZoomMode = view->getCalculatedZoomMode();
    for (const auto &key : {"nextframe", "previousframe"})
    {
        const auto actions = qvApp->getActionManager().getAllClonesOfAction(key, &window);
        QVERIFY(!actions.isEmpty());
        for (auto *action : actions)
            QVERIFY(action->isEnabled());
    }
    for (auto *action : qvApp->getActionManager().getAllClonesOfAction("pause", &window))
        QVERIFY(!action->isEnabled());

    view->setNavigationResetsZoom(true);
    view->zoomAbsolute(2.0);
    QVERIFY(!view->getCalculatedZoomMode().has_value());
    view->jumpToNextFrame();
    QTRY_COMPARE(files.size(), 2);
    QVERIFY(view->getCalculatedZoomMode() == initialZoomMode);
    view->setNavigationResetsZoom(false);
    view->zoomAbsolute(3.0);
    view->jumpToPreviousFrame();
    QTRY_COMPARE(files.size(), 3);
    QCOMPARE(view->getZoomLevel(), 3.0);
    QVERIFY(!view->getCalculatedZoomMode().has_value());
    view->setCalculatedZoomMode(Qv::CalculatedZoomMode::ZoomToFit);
    view->jumpToNextFrame();
    QTRY_COMPARE(files.size(), 4);
    const qreal fittedZoom = view->getZoomLevel();
    view->recalculateZoom();
    QCOMPARE(view->getZoomLevel(), fittedZoom);

    QVERIFY(QFile::remove(path));
    view->jumpToNextFrame();
    QTRY_COMPARE(files.size(), 5);
    QVERIFY(view->getCurrentFileDetails().errorData.has_value());
    QVERIFY(!view->getCurrentFileDetails().isPixmapLoaded);
    for (auto *action : qvApp->getActionManager().getAllClonesOfAction("nextframe", &window))
        QVERIFY(!action->isEnabled());
    for (auto *action : qvApp->getActionManager().getAllClonesOfAction("zoomin", &window))
        QVERIFY(!action->isEnabled());

    QCOMPARE(createTestTiff(dir), path);
    view->loadFile(path);
    QTRY_COMPARE(files.size(), 6);
    QVERIFY(!view->getCurrentFileDetails().errorData.has_value());
    QVERIFY(view->getCurrentFileDetails().isPixmapLoaded);
    for (auto *action : qvApp->getActionManager().getAllClonesOfAction("zoomin", &window))
        QVERIFY(action->isEnabled());
    window.close();
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
    QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDir.path());
    QCoreApplication::setOrganizationName("qViewTests");
    QCoreApplication::setApplicationName("qViewTests");
    QSettings().setValue("options/colorspaceconversion", static_cast<int>(Qv::ColorSpaceConversion::SRgb));
    QSettings().setValue("options/updatenotifications", false);
    QVApplication app(argc, argv);
    qRegisterMetaType<QVImageLoader::Result>();

    ImageLoaderTests imageLoaderTests;
    ActionManagerTests actionManagerTests;
    int result = QTest::qExec(&imageLoaderTests, argc, argv);
    result |= QTest::qExec(&actionManagerTests, argc, argv);
    return result;
}

#include "tst_qviewtests.moc"
