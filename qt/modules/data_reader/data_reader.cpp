#include <radar/contracts.h>
#include <QtCore/QCache>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QSaveFile>
#include <QtGui/QImageReader>
#include <cmath>
#include <limits>

namespace {
struct Page { int start=0, count=0; QString file; };

bool integer(const QJsonValue &value, qint64 &result) {
    if (!value.isDouble()) return false;
    const double number=value.toDouble();
    // JSON integer timestamps must be exactly representable, never rounded.
    if (!std::isfinite(number) || std::floor(number)!=number ||
        std::abs(number)>9007199254740991.0) return false;
    result=static_cast<qint64>(number);
    return true;
}
bool matchesInteger(const QJsonValue &value,qint64 expected) {
    qint64 number=0;
    return integer(value,number) && number==expected;
}
bool inside(const QString &path,const QString &root) {
#ifdef Q_OS_WIN
    constexpr auto sensitivity=Qt::CaseInsensitive;
#else
    constexpr auto sensitivity=Qt::CaseSensitive;
#endif
    return path.compare(root,sensitivity)==0 || path.startsWith(root+QLatin1Char('/'),sensitivity);
}
bool jsonObject(const QString &path,QJsonObject &object,QString &error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error=QStringLiteral("Cannot read %1: %2").arg(path,file.errorString()); return false; }
    QJsonParseError parseError;
    const auto document=QJsonDocument::fromJson(file.readAll(),&parseError);
    if (file.error()!=QFileDevice::NoError || parseError.error!=QJsonParseError::NoError || !document.isObject()) {
        error=QStringLiteral("Invalid JSON object in %1: %2").arg(path,parseError.errorString()); return false;
    }
    object=document.object(); return true;
}

class Reader final:public radar::DataReader {
public:
    explicit Reader(QObject *parent):DataReader(parent),m_pages(4),m_images(16) {}

    void open(const QString &root) override {
        shutdown();
        QString detail;
        const QFileInfo directory(root);
        const QString canonical=QDir::fromNativeSeparators(directory.canonicalFilePath());
        if (!directory.isDir() || canonical.isEmpty()) { emit error(0,QStringLiteral("Dataset directory does not exist: %1").arg(root)); return; }
        m_dataset.root=canonical;
        auto fail=[&](const QString &message) { shutdown(); emit error(0,message); };
        QString manifestPath;
        if (!ownedFile(QStringLiteral("manifest.json"),manifestPath,detail)) { fail(detail); return; }
        QJsonObject manifest;
        if (!jsonObject(manifestPath,manifest,detail)) { fail(detail); return; }
        const auto metadata=manifest.value(QStringLiteral("metadata")).toObject();
        qint64 count=0,first=0,last=0;
        if (!matchesInteger(manifest.value(QStringLiteral("formatVersion")),1) ||
            !matchesInteger(manifest.value(QStringLiteral("pageSize")),240) ||
            metadata.value(QStringLiteral("sequence")).toString()!=QStringLiteral("2019-01-15-13-06-37") ||
            metadata.value(QStringLiteral("recordingMode")).toString()!=QStringLiteral("saved-per-frame-estimates") ||
            !metadata.value(QStringLiteral("liveInference")).isBool() || metadata.value(QStringLiteral("liveInference")).toBool() ||
            metadata.value(QStringLiteral("dataLicense")).toString()!=QStringLiteral("CC BY-NC-SA 4.0") ||
            !integer(metadata.value(QStringLiteral("sampleCount")),count) || count<1 || count>std::numeric_limits<int>::max() ||
            !integer(metadata.value(QStringLiteral("firstRadarTimestamp")),first) || first<=0 ||
            !integer(metadata.value(QStringLiteral("lastRadarTimestamp")),last) || last<first || (count>1 && last==first)) {
            fail(QStringLiteral("Invalid dataset version, paging, timestamps or recorded-data provenance")); return;
        }
        m_dataset.frameCount=static_cast<int>(count);
        m_dataset.firstTimestamp=first; m_dataset.lastTimestamp=last;
        m_dataset.metadata=metadata;
        m_dataset.sequence=metadata.value(QStringLiteral("sequence")).toString();
        m_dataset.recordingMode=metadata.value(QStringLiteral("recordingMode")).toString();
        m_dataset.dataLicense=metadata.value(QStringLiteral("dataLicense")).toString();
        const auto bounds=manifest.value(QStringLiteral("globalBounds")).toObject();
        const QStringList boundKeys={QStringLiteral("minNorth"),QStringLiteral("maxNorth"),QStringLiteral("minEast"),QStringLiteral("maxEast")};
        for (const auto &key:boundKeys) {
            if (!bounds.value(key).isDouble() || !std::isfinite(bounds.value(key).toDouble())) { fail(QStringLiteral("Invalid global bounds")); return; }
        }
        const double minNorth=bounds.value(boundKeys[0]).toDouble(),maxNorth=bounds.value(boundKeys[1]).toDouble();
        const double minEast=bounds.value(boundKeys[2]).toDouble(),maxEast=bounds.value(boundKeys[3]).toDouble();
        if (maxNorth<=minNorth || maxEast<=minEast || !std::isfinite(maxNorth-minNorth) || !std::isfinite(maxEast-minEast)) { fail(QStringLiteral("Invalid global bounds extent")); return; }
        // QRectF x is east and y is north, matching the route display contract.
        m_dataset.globalBounds=QRectF(minEast,minNorth,maxEast-minEast,maxNorth-minNorth);
        const auto pages=manifest.value(QStringLiteral("pages")).toArray();
        if (pages.size()!=(count+239)/240) { fail(QStringLiteral("Page count does not cover the dataset")); return; }
        int next=0;
        for (qsizetype i=0;i<pages.size();++i) {
            const auto entry=pages[i].toObject();
            const int expectedCount=qMin(240,m_dataset.frameCount-next);
            const QString expectedName=QStringLiteral("chunks/page-%1.json").arg(i,5,10,QLatin1Char('0'));
            if (!matchesInteger(entry.value(QStringLiteral("startFrame")),next) ||
                !matchesInteger(entry.value(QStringLiteral("count")),expectedCount) || entry.value(QStringLiteral("file")).toString()!=expectedName) {
                fail(QStringLiteral("Invalid contiguous page index at page %1").arg(i)); return;
            }
            QString path;
            if (!ownedFile(expectedName,path,detail)) { fail(detail); return; }
            m_index.append({next,expectedCount,expectedName}); next+=expectedCount;
        }
        const auto overview=manifest.value(QStringLiteral("overviewRows")).toArray();
        if (overview.isEmpty() || overview.size()>count) { fail(QStringLiteral("Missing or oversized route overview")); return; }
        int previous=-1; qint64 previousStamp=0;
        for (const auto &value:overview) {
            radar::Frame frame;
            if (!value.isArray() || !radar::parseFrame(value.toArray(),frame,detail) ||
                !validFrame(frame,detail) || frame.index<=previous || frame.radarTimestamp<=previousStamp) {
                fail(QStringLiteral("Invalid overview row: %1").arg(detail)); return;
            }
            previous=frame.index; previousStamp=frame.radarTimestamp; m_dataset.overview.append(frame);
        }
        if (m_dataset.overview.first().index!=0 || m_dataset.overview.last().index!=m_dataset.frameCount-1 ||
            m_dataset.overview.first().radarTimestamp!=first || m_dataset.overview.last().radarTimestamp!=last) {
            fail(QStringLiteral("Overview endpoints do not match the dataset")); return;
        }
        m_open=true; emit opened(m_dataset);
    }

    void readFrame(quint64 requestId,int index) override {
        QString detail;
        radar::Frame frame;
        if (!pose(index,frame,detail)) { emit error(requestId,detail); return; }
        if (const auto cached=m_images.object(index)) { emit frameReady(requestId,*cached); return; }
        if (!image(QStringLiteral("radar/%1.jpg").arg(frame.radarTimestamp),frame.radarImage,detail) ||
            !image(QStringLiteral("stereo/%1.jpg").arg(frame.stereoTimestamp),frame.stereoImage,detail)) {
            emit error(requestId,detail); return;
        }
        m_images.insert(index,new radar::Frame(frame)); emit frameReady(requestId,frame);
    }

    void exportCsv(quint64 requestId,const QString &path,int first,int last) override {
        if (!m_open || first<0 || last<first || last>=m_dataset.frameCount) {
            emit error(requestId,QStringLiteral("Invalid inclusive CSV export range or no open dataset")); return;
        }
        const QFileInfo target(path);
        const QString parent=QDir::fromNativeSeparators(target.dir().canonicalPath());
        const QString resolved=target.exists()?QDir::fromNativeSeparators(target.canonicalFilePath()):QDir(parent).filePath(target.fileName());
        if (path.isEmpty() || parent.isEmpty() || target.fileName().isEmpty() || target.isDir() ||
            inside(resolved,m_dataset.root+QStringLiteral("/manifest.json")) ||
            inside(resolved,m_dataset.root+QStringLiteral("/chunks")) || inside(resolved,m_dataset.root+QStringLiteral("/radar")) || inside(resolved,m_dataset.root+QStringLiteral("/stereo"))) {
            emit error(requestId,QStringLiteral("CSV destination is invalid or belongs to the dataset")); return;
        }
        QSaveFile output(target.absoluteFilePath()); output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly)) { emit error(requestId,QStringLiteral("Cannot create CSV: %1").arg(output.errorString())); return; }
        QStringList columns={QStringLiteral("index"),QStringLiteral("radar_timestamp"),QStringLiteral("stereo_timestamp"),QStringLiteral("vo_model_accepted")};
        const auto names=radar::methodNames();
        const QStringList fields={QStringLiteral("north"),QStringLiteral("east"),QStringLiteral("down"),QStringLiteral("yaw"),QStringLiteral("planar_error"),QStringLiteral("yaw_error")};
        for (int method=0;method<7;++method) for (const auto &field:fields) columns.append(csv(names.value(method,QStringLiteral("method_%1").arg(method))+QLatin1Char('_')+field));
        auto write=[&](const QStringList &values) { const auto bytes=(values.join(QLatin1Char(','))+QLatin1Char('\n')).toUtf8(); return output.write(bytes)==bytes.size(); };
        if (!write(columns)) { output.cancelWriting(); emit error(requestId,QStringLiteral("Cannot write CSV header")); return; }
        for (int index=first;index<=last;++index) {
            radar::Frame frame; QString detail;
            if (!pose(index,frame,detail)) { output.cancelWriting(); emit error(requestId,detail); return; }
            QStringList values={QString::number(frame.index),QString::number(frame.radarTimestamp),QString::number(frame.stereoTimestamp),frame.voModelAccepted?QStringLiteral("1"):QStringLiteral("0")};
            for (int method=0;method<7;++method) {
                const auto &p=frame.poses[method];
                for (double value:{p.north,p.east,p.down,p.yaw,radar::planarError(frame,method),radar::yawError(frame,method)}) values.append(QString::number(value,'g',17));
            }
            if (!write(values)) { output.cancelWriting(); emit error(requestId,QStringLiteral("Cannot write CSV row %1").arg(index)); return; }
        }
        if (!output.commit()) { emit error(requestId,QStringLiteral("Cannot atomically commit CSV: %1").arg(output.errorString())); return; }
        emit exported(requestId,target.absoluteFilePath(),last-first+1);
    }

    void shutdown() override { m_open=false; m_pages.clear(); m_images.clear(); m_index.clear(); m_dataset=radar::Dataset{}; }

private:
    static QString csv(QString value) { value.replace(QLatin1Char('"'),QStringLiteral("\"\"")); return QLatin1Char('"')+value+QLatin1Char('"'); }
    bool ownedFile(const QString &relative,QString &path,QString &detail) const {
        const QFileInfo info(QDir(m_dataset.root).filePath(relative));
        path=QDir::fromNativeSeparators(info.canonicalFilePath());
        if (!info.isFile() || path.isEmpty() || !inside(path,m_dataset.root)) {
            detail=QStringLiteral("Missing file or path escaping the dataset: %1").arg(relative); return false;
        }
        return true;
    }
    bool validFrame(const radar::Frame &frame,QString &detail) const {
        if (frame.index<0 || frame.index>=m_dataset.frameCount || frame.radarTimestamp<m_dataset.firstTimestamp ||
            frame.radarTimestamp>m_dataset.lastTimestamp || frame.stereoTimestamp<=0 ||
            (frame.index==0 && frame.radarTimestamp!=m_dataset.firstTimestamp) ||
            (frame.index==m_dataset.frameCount-1 && frame.radarTimestamp!=m_dataset.lastTimestamp) ||
            !m_dataset.globalBounds.contains(QPointF(frame.poses[0].east,frame.poses[0].north))) {
            detail=QStringLiteral("Frame index, timestamp or reference position is outside manifest bounds"); return false;
        }
        for (int method=0;method<7;++method) {
            if (!std::isfinite(radar::planarError(frame,method)) || !std::isfinite(radar::yawError(frame,method))) {
                detail=QStringLiteral("Frame values produce a non-finite pose error"); return false;
            }
        }
        return true;
    }
    bool pose(int index,radar::Frame &frame,QString &detail) {
        if (!m_open || index<0 || index>=m_dataset.frameCount) { detail=QStringLiteral("Frame request out of range or no open dataset: %1").arg(index); return false; }
        const int pageNumber=index/240;
        auto page=m_pages.object(pageNumber);
        if (!page) {
            const auto &entry=m_index[pageNumber]; QString path; QJsonObject object;
            if (!ownedFile(entry.file,path,detail) || !jsonObject(path,object,detail)) return false;
            const auto rows=object.value(QStringLiteral("frames")).toArray();
            if (!matchesInteger(object.value(QStringLiteral("startFrame")),entry.start) || rows.size()!=entry.count) { detail=QStringLiteral("Invalid page header or row count: %1").arg(entry.file); return false; }
            QVector<radar::Frame> parsed; parsed.reserve(entry.count);
            qint64 previousRadar=0,previousStereo=0;
            for (qsizetype offset=0;offset<rows.size();++offset) {
                radar::Frame item;
                if (!rows[offset].isArray() || !radar::parseFrame(rows[offset].toArray(),item,detail) ||
                    item.index!=entry.start+offset || !validFrame(item,detail) || item.radarTimestamp<=previousRadar || item.stereoTimestamp<=previousStereo) {
                    detail=QStringLiteral("Invalid frame %1 in %2: %3").arg(entry.start+offset).arg(entry.file,detail); return false;
                }
                for (const auto &overview:m_dataset.overview) if (overview.index==item.index && radar::frameToJson(overview)!=radar::frameToJson(item)) { detail=QStringLiteral("Page disagrees with overview at frame %1").arg(item.index); return false; }
                previousRadar=item.radarTimestamp; previousStereo=item.stereoTimestamp; parsed.append(item);
            }
            m_pages.insert(pageNumber,new QVector<radar::Frame>(std::move(parsed)));
            page=m_pages.object(pageNumber);
        }
        frame=page->at(index-m_index[pageNumber].start); return true;
    }
    bool image(const QString &relative,QImage &result,QString &detail) const {
        QString path;
        if (!ownedFile(relative,path,detail)) return false;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.read(3)!=QByteArray::fromHex("ffd8ff")) { detail=QStringLiteral("Invalid or unreadable JPEG: %1").arg(relative); return false; }
        QImageReader reader(path,"JPEG"); reader.setDecideFormatFromContent(false);
        result=reader.read();
        if (result.isNull()) { detail=QStringLiteral("Cannot decode JPEG %1: %2").arg(relative,reader.errorString()); return false; }
        return true;
    }
    radar::Dataset m_dataset;
    QVector<Page> m_index;
    // LRU bounds: 4 pose pages (960 rows) and 16 decoded radar/stereo pairs.
    QCache<int,QVector<radar::Frame>> m_pages;
    QCache<int,radar::Frame> m_images;
    bool m_open=false;
};
}

extern "C" RADAR_READER_API radar::DataReader *radar_create_reader(QObject *parent) { return new Reader(parent); }
