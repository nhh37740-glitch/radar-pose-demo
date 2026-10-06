#include <radar/contracts.h>
#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include <QtCore/QTemporaryDir>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QThread>
#include <QtGui/QImageWriter>
#include <atomic>
#include <memory>

namespace {
QJsonArray row(int index) {
    QJsonArray values; values.append(index); values.append(1547557600000000.0+index*250000.0);
    for (int method=0;method<7;++method) {
        values.append(index*0.5+method*0.1); values.append(index*0.25-method*0.2);
        values.append(-method*0.3); values.append(0.001*index+method*0.01);
    }
    values.append(index%2); values.append(1547557600000050.0+index*250000.0);
    return values;
}
bool writeJson(const QString &path,const QJsonObject &value) {
    QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes=QJsonDocument(value).toJson(QJsonDocument::Compact);
    return file.write(bytes)==bytes.size();
}
QJsonObject readJson(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return QJsonDocument::fromJson(file.readAll()).object(); }
QByteArray readBytes(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }

class Fixture {
public:
    QTemporaryDir temporary;
    QString root() const { return temporary.path(); }
    QString path(const QString &relative) const { return root()+QLatin1Char('/')+relative; }
    bool create(int count=242,bool images=true) {
        if (!temporary.isValid()) return false;
        for (const auto &directory:{QStringLiteral("chunks"),QStringLiteral("radar"),QStringLiteral("stereo")}) if (!QDir(root()).mkpath(directory)) return false;
        QJsonArray pages;
        for (int start=0;start<count;start+=240) {
            QJsonArray rows; for (int index=start;index<qMin(start+240,count);++index) rows.append(row(index));
            const QString name=QStringLiteral("chunks/page-%1.json").arg(start/240,5,10,QLatin1Char('0'));
            if (!writeJson(path(name),{{"startFrame",start},{"frames",rows}})) return false;
            pages.append(QJsonObject{{"startFrame",start},{"count",rows.size()},{"file",name}});
        }
        QJsonArray overview; for (int index=0;index<count;index+=10) overview.append(row(index));
        if ((count-1)%10) overview.append(row(count-1));
        QJsonObject metadata{{"sequence","2019-01-15-13-06-37"},{"sampleCount",count},
            {"firstRadarTimestamp",row(0)[1]},{"lastRadarTimestamp",row(count-1)[1]},
            {"recordingMode","saved-per-frame-estimates"},{"liveInference",false},{"dataLicense","CC BY-NC-SA 4.0"}};
        if (!writeJson(path("manifest.json"),{{"formatVersion",1},{"metadata",metadata},{"pageSize",240},{"pages",pages},
            {"globalBounds",QJsonObject{{"minNorth",-10},{"maxNorth",count*1.0+10},{"minEast",-10},{"maxEast",count*1.0+10}}},{"overviewRows",overview}})) return false;
        for (int index=0;images && index<count;++index) {
            QImage radar(12,8,QImage::Format_RGB32); radar.fill(QColor(210,30,20));
            QImage stereo(10,6,QImage::Format_RGB32); stereo.fill(QColor(20,40,210));
            if (!radar.save(path(QStringLiteral("radar/%1.jpg").arg(static_cast<qint64>(row(index)[1].toDouble()))),"JPEG") ||
                !stereo.save(path(QStringLiteral("stereo/%1.jpg").arg(static_cast<qint64>(row(index)[31].toDouble()))),"JPEG")) return false;
        }
        return true;
    }
    void alterRow(int pageNumber,int offset,int field,const QJsonValue &value) {
        const auto file=path(QStringLiteral("chunks/page-%1.json").arg(pageNumber,5,10,QLatin1Char('0')));
        auto page=readJson(file); auto rows=page["frames"].toArray(); auto values=rows[offset].toArray();
        values[field]=value; rows[offset]=values; page["frames"]=rows; writeJson(file,page);
    }
};
}

class DataReaderTest:public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { radar::registerTypes(); QVERIFY(QImageWriter::supportedImageFormats().contains("jpeg")); }

    void opensAndSeeksAcrossPages() {
        Fixture fixture; QVERIFY(fixture.create()); std::unique_ptr<radar::DataReader> reader(radar_create_reader(nullptr));
        QSignalSpy opened(reader.get(),&radar::DataReader::opened),ready(reader.get(),&radar::DataReader::frameReady),errors(reader.get(),&radar::DataReader::error);
        reader->open(fixture.root()); QCOMPARE(opened.count(),1); QCOMPARE(errors.count(),0);
        const auto dataset=qvariant_cast<radar::Dataset>(opened.first()[0]); QCOMPARE(dataset.frameCount,242);
        QCOMPARE(dataset.root,QDir(fixture.root()).canonicalPath()); QCOMPARE(dataset.sequence,QStringLiteral("2019-01-15-13-06-37"));
        QVERIFY(!dataset.liveInference); QCOMPARE(dataset.recordingMode,QStringLiteral("saved-per-frame-estimates"));
        QCOMPARE(dataset.overview.first().index,0); QCOMPARE(dataset.overview.last().index,241);
        for (int index:{239,240,241,0,240}) {
            reader->readFrame(1000+index,index); QCOMPARE(errors.count(),0); QCOMPARE(ready.count(),1);
            const auto values=ready.takeFirst(); QCOMPARE(values[0].toULongLong(),static_cast<quint64>(1000+index));
            const auto frame=qvariant_cast<radar::Frame>(values[1]); QCOMPARE(frame.index,index);
            QCOMPARE(frame.radarTimestamp,static_cast<qint64>(row(index)[1].toDouble())); QCOMPARE(frame.stereoTimestamp,static_cast<qint64>(row(index)[31].toDouble()));
            QCOMPARE(frame.voModelAccepted,index%2==1);
            for (int method=0;method<7;++method) { QCOMPARE(frame.poses[method].north,index*0.5+method*0.1); QCOMPARE(frame.poses[method].east,index*0.25-method*0.2); QCOMPARE(frame.poses[method].down,-method*0.3); QCOMPARE(frame.poses[method].yaw,0.001*index+method*0.01); }
            QCOMPARE(frame.radarImage.size(),QSize(12,8)); QCOMPARE(frame.stereoImage.size(),QSize(10,6));
            QVERIFY(frame.radarImage.pixelColor(2,2).red()>180); QVERIFY(frame.stereoImage.pixelColor(2,2).blue()>180);
        }
        reader->shutdown(); reader->readFrame(2000,0); QCOMPARE(errors.count(),1); QCOMPARE(errors.first()[0].toULongLong(),quint64(2000)); QCOMPARE(ready.count(),0);
    }

    void manifestFailures_data() {
        QTest::addColumn<QString>("kind");
        for (const auto &kind:{"version","pageSize","provenance","license","live","count","timestamp","bounds","overview","pageStart","pageCount","traversal","missingPage","malformed"}) QTest::newRow(kind)<<QString::fromLatin1(kind);
    }
    void manifestFailures() {
        QFETCH(QString,kind); Fixture fixture; QVERIFY(fixture.create(3)); std::unique_ptr<radar::DataReader> reader(radar_create_reader(nullptr));
        QSignalSpy opened(reader.get(),&radar::DataReader::opened),ready(reader.get(),&radar::DataReader::frameReady),errors(reader.get(),&radar::DataReader::error);
        reader->open(fixture.root()); QCOMPARE(opened.count(),1);
        auto manifest=readJson(fixture.path("manifest.json")); auto metadata=manifest["metadata"].toObject(); auto pages=manifest["pages"].toArray(); auto page=pages[0].toObject();
        if(kind=="version") manifest["formatVersion"]=2;
        if(kind=="pageSize") manifest["pageSize"]=241;
        if(kind=="provenance") metadata["recordingMode"]="live";
        if(kind=="license") metadata["dataLicense"]="unknown";
        if(kind=="live") metadata["liveInference"]=true;
        if(kind=="count") metadata["sampleCount"]=3.5;
        if(kind=="timestamp") metadata["lastRadarTimestamp"]=metadata["firstRadarTimestamp"];
        if(kind=="bounds") manifest["globalBounds"]=QJsonObject{{"minNorth",1},{"maxNorth",0},{"minEast",0},{"maxEast",1}};
        if(kind=="overview") manifest["overviewRows"]=QJsonArray{row(1),row(2)};
        if(kind=="pageStart") page["startFrame"]=1;
        if(kind=="pageCount") page["count"]=2;
        if(kind=="traversal") page["file"]="../page.json";
        pages[0]=page; manifest["pages"]=pages; manifest["metadata"]=metadata; QVERIFY(writeJson(fixture.path("manifest.json"),manifest));
        if(kind=="missingPage") QVERIFY(QFile::remove(fixture.path("chunks/page-00000.json")));
        if(kind=="malformed") { QFile file(fixture.path("manifest.json")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{broken"); }
        reader->open(fixture.root()); QCOMPARE(opened.count(),1); QCOMPARE(errors.count(),1); QVERIFY(!errors.first()[1].toString().isEmpty());
        reader->readFrame(87,0); QCOMPARE(ready.count(),0); QCOMPARE(errors.count(),2); QCOMPARE(errors.last()[0].toULongLong(),quint64(87));
    }

    void readFailures_data() {
        QTest::addColumn<QString>("kind");
        for (const auto &kind:{"negative","tooLarge","missingPage","badPageJSON","wrongStart","wrongCount","badIndex","badPose","badAccepted","missingRadar","missingStereo","wrongImage","corruptJPEG"}) QTest::newRow(kind)<<QString::fromLatin1(kind);
    }
    void readFailures() {
        QFETCH(QString,kind); Fixture fixture; QVERIFY(fixture.create(3)); std::unique_ptr<radar::DataReader> reader(radar_create_reader(nullptr));
        QSignalSpy ready(reader.get(),&radar::DataReader::frameReady),errors(reader.get(),&radar::DataReader::error);
        reader->open(fixture.root()); QCOMPARE(errors.count(),0);
        const QString page=fixture.path("chunks/page-00000.json");
        int index=1;
        if(kind=="negative") index=-1;
        if(kind=="tooLarge") index=3;
        if(kind=="missingPage") QVERIFY(QFile::remove(page));
        if(kind=="badPageJSON") { QFile file(page); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("[]"); }
        if(kind=="wrongStart" || kind=="wrongCount") { auto object=readJson(page); if(kind=="wrongStart") object["startFrame"]=1; else object["frames"]=QJsonArray{row(0)}; QVERIFY(writeJson(page,object)); }
        if(kind=="badIndex") fixture.alterRow(0,2,0,1); // Must validate the entire loaded page, including unrequested rows.
        if(kind=="badPose") fixture.alterRow(0,2,4,"not a number");
        if(kind=="badAccepted") fixture.alterRow(0,2,30,2);
        const QString radarPath=fixture.path(QStringLiteral("radar/%1.jpg").arg(static_cast<qint64>(row(index<0?0:1)[1].toDouble())));
        if(kind=="missingRadar") QVERIFY(QFile::remove(radarPath));
        if(kind=="missingStereo") QVERIFY(QFile::remove(fixture.path(QStringLiteral("stereo/%1.jpg").arg(static_cast<qint64>(row(1)[31].toDouble())))));
        if(kind=="wrongImage") { QImage image(4,4,QImage::Format_RGB32); image.fill(Qt::green); QVERIFY(image.save(radarPath,"PNG")); }
        if(kind=="corruptJPEG") { QFile file(radarPath); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QByteArray::fromHex("ffd8ff")+"broken JPEG"); }
        reader->readFrame(123,index); QCOMPARE(ready.count(),0); QCOMPARE(errors.count(),1); QCOMPARE(errors.first()[0].toULongLong(),quint64(123)); QVERIFY(!errors.first()[1].toString().isEmpty());
    }

    void inclusiveCsvDoesNotNeedImagesAndProtectsDataset() {
        Fixture fixture; QVERIFY(fixture.create()); std::unique_ptr<radar::DataReader> reader(radar_create_reader(nullptr));
        QSignalSpy exported(reader.get(),&radar::DataReader::exported),errors(reader.get(),&radar::DataReader::error);
        reader->open(fixture.root()); QCOMPARE(errors.count(),0);
        for (const auto &file:QDir(fixture.path("radar")).entryList(QDir::Files)) QVERIFY(QFile::remove(fixture.path("radar/")+file));
        const QString csv=fixture.path("result.csv"); reader->exportCsv(500,csv,239,241); QCOMPARE(errors.count(),0); QCOMPARE(exported.count(),1);
        QCOMPARE(exported.first()[0].toULongLong(),quint64(500)); QCOMPARE(exported.first()[2].toInt(),3);
        const auto bytes=readBytes(csv); const auto lines=bytes.trimmed().split('\n'); QCOMPARE(lines.size(),4);
        const auto columns=lines[0].split(','); QCOMPARE(columns.size(),46); QVERIFY(columns[4].contains("north")); QVERIFY(columns[8].contains("planar_error"));
        for(int offset=0;offset<3;++offset) { const auto fields=lines[offset+1].split(','); QCOMPARE(fields.size(),46); QCOMPARE(fields[0].toInt(),239+offset); QCOMPARE(fields[1].toLongLong(),static_cast<qint64>(row(239+offset)[1].toDouble())); QCOMPARE(fields[4].toDouble(),(239+offset)*0.5); QCOMPARE(fields[8].toDouble(),0.0); }
        reader->exportCsv(501,csv,239,241); QCOMPARE(readBytes(csv),bytes); QCOMPARE(exported.count(),2);
        for(const auto &owned:{fixture.path("manifest.json"),fixture.path("chunks/page-00000.json"),fixture.path("radar/1547557600000000.jpg"),fixture.path("stereo/1547557600000050.jpg")}) {
            const auto before=readBytes(owned); reader->exportCsv(502,owned,0,1); QCOMPARE(readBytes(owned),before);
        }
        QCOMPARE(errors.count(),4); QCOMPARE(exported.count(),2);
        reader->exportCsv(503,csv,2,1); QCOMPARE(readBytes(csv),bytes); QCOMPARE(errors.count(),5);
    }

    void failedExportPreservesExistingOutput() {
        Fixture fixture; QVERIFY(fixture.create()); std::unique_ptr<radar::DataReader> reader(radar_create_reader(nullptr));
        QSignalSpy exported(reader.get(),&radar::DataReader::exported),errors(reader.get(),&radar::DataReader::error);
        reader->open(fixture.root()); QCOMPARE(errors.count(),0);
        const QString destination=fixture.path("previous.csv"); { QFile file(destination); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("previous export\n"),qint64(16)); }
        fixture.alterRow(1,1,3,"invalid"); reader->exportCsv(900,destination,0,241);
        QCOMPARE(exported.count(),0); QCOMPARE(errors.count(),1); QCOMPARE(errors.first()[0].toULongLong(),quint64(900)); QCOMPARE(readBytes(destination),QByteArray("previous export\n"));
    }

    void cacheEvictionAndReopen() {
        Fixture fixture; QVERIFY(fixture.create(20)); std::unique_ptr<radar::DataReader> reader(radar_create_reader(nullptr));
        QSignalSpy ready(reader.get(),&radar::DataReader::frameReady),errors(reader.get(),&radar::DataReader::error);
        reader->open(fixture.root());
        for(int index=0;index<20;++index) reader->readFrame(index,index);
        QCOMPARE(ready.count(),20); QCOMPARE(errors.count(),0);
        QVERIFY(QFile::remove(fixture.path("radar/1547557600000000.jpg"))); reader->readFrame(100,0); QCOMPARE(errors.count(),1); QCOMPARE(ready.count(),20);
        reader->open(fixture.root()); reader->readFrame(101,19); QCOMPARE(ready.count(),21);
    }

    void posePageCacheIsBounded() {
        Fixture fixture; QVERIFY(fixture.create(961,false)); std::unique_ptr<radar::DataReader> reader(radar_create_reader(nullptr));
        QSignalSpy exported(reader.get(),&radar::DataReader::exported),errors(reader.get(),&radar::DataReader::error);
        reader->open(fixture.root()); QCOMPARE(errors.count(),0);
        for(int index:{0,240,480,720,960}) reader->exportCsv(index,fixture.path("single.csv"),index,index);
        QCOMPARE(exported.count(),5); QCOMPARE(errors.count(),0);
        fixture.alterRow(0,0,4,"invalid"); reader->exportCsv(1000,fixture.path("single.csv"),0,0);
        QCOMPARE(exported.count(),5); QCOMPARE(errors.count(),1); QCOMPARE(errors.first()[0].toULongLong(),quint64(1000));
    }

    void queuedSlotsRunOnFileThread() {
        Fixture fixture; QVERIFY(fixture.create(3)); QThread worker;
        auto *reader=radar_create_reader(nullptr); reader->moveToThread(&worker);
        connect(&worker,&QThread::finished,reader,&QObject::deleteLater);
        QSignalSpy opened(reader,&radar::DataReader::opened),ready(reader,&radar::DataReader::frameReady),exported(reader,&radar::DataReader::exported),errors(reader,&radar::DataReader::error);
        std::atomic<bool> openOnWorker{false},readOnWorker{false},exportOnWorker{false};
        connect(reader,&radar::DataReader::opened,reader,[&](radar::Dataset){openOnWorker=QThread::currentThread()==&worker;},Qt::DirectConnection);
        connect(reader,&radar::DataReader::frameReady,reader,[&](quint64,radar::Frame){readOnWorker=QThread::currentThread()==&worker;},Qt::DirectConnection);
        connect(reader,&radar::DataReader::exported,reader,[&](quint64,QString,int){exportOnWorker=QThread::currentThread()==&worker;},Qt::DirectConnection);
        worker.start();
        const bool openQueued=QMetaObject::invokeMethod(reader,"open",Qt::QueuedConnection,Q_ARG(QString,fixture.root()));
        const bool openedResult=openQueued && QTest::qWaitFor([&]{return opened.count()>0;},5000);
        const bool readQueued=QMetaObject::invokeMethod(reader,"readFrame",Qt::QueuedConnection,Q_ARG(quint64,quint64(701)),Q_ARG(int,2));
        const bool readResult=readQueued && QTest::qWaitFor([&]{return ready.count()>0;},5000);
        const bool exportQueued=QMetaObject::invokeMethod(reader,"exportCsv",Qt::QueuedConnection,Q_ARG(quint64,quint64(702)),Q_ARG(QString,fixture.path("worker.csv")),Q_ARG(int,0),Q_ARG(int,2));
        const bool exportResult=exportQueued && QTest::qWaitFor([&]{return exported.count()>0;},5000);
        QMetaObject::invokeMethod(reader,"shutdown",Qt::BlockingQueuedConnection); worker.quit(); const bool stopped=worker.wait(5000);
        QVERIFY(stopped); QVERIFY(openedResult); QVERIFY(readResult); QVERIFY(exportResult); QVERIFY(openOnWorker); QVERIFY(readOnWorker); QVERIFY(exportOnWorker); QCOMPARE(errors.count(),0);
        QCOMPARE(qvariant_cast<radar::Frame>(ready.first()[1]).index,2); QCOMPARE(exported.first()[2].toInt(),3);
    }
};
QTEST_GUILESS_MAIN(DataReaderTest)
#include "data_reader_test.moc"
