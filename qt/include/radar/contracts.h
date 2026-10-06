#pragma once
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QStringList>
#include <QtCore/QRectF>
#include <QtGui/QImage>
#include <QtWidgets/QWidget>
#include <array>

#ifdef RADAR_CONTRACTS_BUILD
#define RADAR_API Q_DECL_EXPORT
#else
#define RADAR_API Q_DECL_IMPORT
#endif
#ifdef RADAR_READER_BUILD
#define RADAR_READER_API Q_DECL_EXPORT
#else
#define RADAR_READER_API Q_DECL_IMPORT
#endif
#ifdef RADAR_PLAYBACK_BUILD
#define RADAR_PLAYBACK_API Q_DECL_EXPORT
#else
#define RADAR_PLAYBACK_API Q_DECL_IMPORT
#endif
#ifdef RADAR_FRONTEND_BUILD
#define RADAR_FRONTEND_API Q_DECL_EXPORT
#else
#define RADAR_FRONTEND_API Q_DECL_IMPORT
#endif

namespace radar {
struct Pose { double north=0, east=0, down=0, yaw=0; };
struct Frame {
    int index=-1;
    qint64 radarTimestamp=0, stereoTimestamp=0;
    std::array<Pose,7> poses{};
    bool voModelAccepted=false;
    QImage radarImage, stereoImage;
};
struct Dataset {
    QString root, sequence, recordingMode, dataLicense;
    int frameCount=0;
    qint64 firstTimestamp=0, lastTimestamp=0;
    bool liveInference=false;
    QRectF globalBounds;
    QVector<Frame> overview;
    QJsonObject metadata;
};
RADAR_API void registerTypes();
RADAR_API bool parseFrame(const QJsonArray &row,Frame &frame,QString &error);
RADAR_API QJsonArray frameToJson(const Frame &frame);
RADAR_API QStringList methodNames();
RADAR_API double planarError(const Frame &frame,int method);
RADAR_API double yawError(const Frame &frame,int method);

class RADAR_API DataReader:public QObject {
    Q_OBJECT
public: explicit DataReader(QObject *parent=nullptr):QObject(parent){}
public slots:
    virtual void open(const QString &root)=0;
    virtual void readFrame(quint64 requestId,int index)=0;
    virtual void exportCsv(quint64 requestId,const QString &path,int first,int last)=0;
    virtual void shutdown()=0;
signals:
    void opened(radar::Dataset dataset);
    void frameReady(quint64 requestId,radar::Frame frame);
    void exported(quint64 requestId,QString path,int rows);
    void error(quint64 requestId,QString detail);
};
class RADAR_API Playback:public QObject {
    Q_OBJECT
public: explicit Playback(QObject *parent=nullptr):QObject(parent){}
public slots:
    virtual void setDataset(radar::Dataset dataset)=0;
    virtual void play()=0;
    virtual void pause()=0;
    virtual void toggle()=0;
    virtual void seek(int index)=0;
    virtual void step(int delta)=0;
    virtual void setSpeed(double speed)=0;
    virtual void acceptFrame(quint64 requestId,radar::Frame frame)=0;
    virtual void acceptError(quint64 requestId,QString detail)=0;
    virtual void shutdown()=0;
signals:
    void requestFrame(quint64 requestId,int index);
    void showFrame(radar::Frame frame);
    void stateChanged(bool playing,int index,double speed);
    void error(QString detail);
    void finished();
};
class RADAR_API Frontend:public QWidget {
    Q_OBJECT
public: explicit Frontend(QWidget *parent=nullptr):QWidget(parent){}
public slots:
    virtual void setDataset(radar::Dataset dataset)=0;
    virtual void showFrame(radar::Frame frame)=0;
    virtual void setPlaybackState(bool playing,int index,double speed)=0;
    virtual void showStatus(QString detail)=0;
    virtual void exportCompleted(QString path,int rows)=0;
signals:
    void openRequested(QString root);
    void seekRequested(int index);
    void toggleRequested();
    void stepRequested(int delta);
    void speedRequested(double speed);
    void exportRequested(QString path,int first,int last);
};
}
Q_DECLARE_METATYPE(radar::Frame)
Q_DECLARE_METATYPE(radar::Dataset)
extern "C" RADAR_READER_API radar::DataReader *radar_create_reader(QObject *parent);
extern "C" RADAR_PLAYBACK_API radar::Playback *radar_create_playback(QObject *parent);
extern "C" RADAR_FRONTEND_API radar::Frontend *radar_create_frontend(QWidget *parent);
