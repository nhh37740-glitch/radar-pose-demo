#include <radar/contracts.h>
#include <QtWidgets/QApplication>
#include <QtGui/QScreen>
#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QSaveFile>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>

int main(int argc,char **argv){
    for(int i=1;i<argc;++i)if(QByteArray(argv[i])=="--headless")qputenv("QT_QPA_PLATFORM","offscreen");
    qputenv("QT_QPA_FONTDIR",(qEnvironmentVariable("SystemRoot","C:/Windows")+"/Fonts").toUtf8());
    QApplication app(argc,argv);app.setApplicationName("radar-playback");app.setApplicationVersion(QString::fromLatin1(RADAR_APP_VERSION));radar::registerTypes();
    QCommandLineParser cli;cli.setApplicationDescription(QStringLiteral("完整雷达与图像位姿回放：读取已保存结果，不执行在线模型"));cli.addHelpOption();cli.addVersionOption();
    auto option=[&](const char *name,const char *description,const char *value="",const char *initial=""){
        cli.addOption(QCommandLineOption(QStringList{QString::fromLatin1(name)},QString::fromUtf8(description),QString::fromLatin1(value),QString::fromLatin1(initial)));
    };
    option("data","数据包目录（含 manifest.json）","directory");option("headless","不打开可见窗口");
    option("probe-frames","依次跳转这些帧后退出，例如0,239,240,7202","list");option("verify-all","解码并校验所有位姿和配对图像后退出");
    option("report","运行结果 JSON","file");option("screenshot","退出前保存真实界面 PNG","file");
    option("ready-file","第一帧已显示时写入就绪 JSON","file");
    option("export","导出 CSV 后退出（可同时指定回放时长）","file");option("export-first","导出起始帧","number","0");option("export-last","导出结束帧，默认全部","number","-1");
    option("start-frame","打开后跳到此帧","number","0");option("auto-play","打开数据后自动播放");
    option("speed","按录制时间的播放倍速，0.25至16","number","1");option("duration-ms","运行指定毫秒数后退出","number");
    cli.process(app);
    bool ok=true;bool parsed=false;const int startFrame=cli.value("start-frame").toInt(&parsed);ok&=parsed&&startFrame>=0;
    const double speed=cli.value("speed").toDouble(&parsed);ok&=parsed&&std::isfinite(speed)&&speed>=.25&&speed<=16;
    const int first=cli.value("export-first").toInt(&parsed);ok&=parsed&&first>=0;
    const int last=cli.value("export-last").toInt(&parsed);ok&=parsed&&last>=-1;
    int duration=0;if(cli.isSet("duration-ms")){duration=cli.value("duration-ms").toInt(&parsed);ok&=parsed&&duration>0&&duration<=3600000;}
    QVector<int> probes;if(cli.isSet("probe-frames")){
        for(const auto &text:cli.value("probe-frames").split(',')){const int index=text.toInt(&parsed);ok&=parsed&&index>=0;probes.append(index);}
        ok&=!probes.isEmpty();
    }
    if(!ok){qCritical("Invalid frame, duration or speed argument");return 2;}
    std::unique_ptr<radar::Frontend> view(radar_create_frontend(nullptr));
    std::unique_ptr<radar::Playback> playback(radar_create_playback(nullptr));
    auto *reader=radar_create_reader(nullptr);QThread fileThread;fileThread.setObjectName("read-images-and-export-records");reader->moveToThread(&fileThread);
    QObject::connect(&fileThread,&QThread::finished,reader,&QObject::deleteLater);
    radar::Dataset dataset;QJsonArray shown,errors;radar::Frame current;int displayed=0,validated=0,exportedRows=0,requests=0;
    bool guiThreadCorrect=true,playbackThreadCorrect=true;std::atomic_bool readerThreadCorrect{true};
    bool probeDone=probes.isEmpty(),verifyDone=!cli.isSet("verify-all"),exportDone=!cli.isSet("export"),ready=false;
    bool stopping=false,verificationStarted=false;int probePosition=0;int exitResult=0;
    constexpr quint64 verifyBase=1000000000ULL;constexpr quint64 exportRequest=2000000000ULL;
    auto submitFrame=[&](quint64 id,int index){QMetaObject::invokeMethod(reader,"readFrame",Qt::QueuedConnection,Q_ARG(quint64,id),Q_ARG(int,index));};
    auto recordError=[&](const QString &error){errors.append(error);view->showStatus(error);qWarning("%s",qPrintable(error));exitResult=1;};
    std::function<void()> maybeDone;
    auto finish=[&]{if(stopping)return;stopping=true;playback->pause();QTimer::singleShot(100,&app,&QCoreApplication::quit);};
    maybeDone=[&]{if(!duration&&ready&&probeDone&&verifyDone&&exportDone&&(cli.isSet("probe-frames")||cli.isSet("verify-all")||cli.isSet("export")))finish();};
    QObject::connect(reader,&radar::DataReader::opened,reader,[&](radar::Dataset){readerThreadCorrect.store(readerThreadCorrect.load()&&QThread::currentThread()==&fileThread);},Qt::DirectConnection);
    QObject::connect(reader,&radar::DataReader::frameReady,reader,[&](quint64,radar::Frame){readerThreadCorrect.store(readerThreadCorrect.load()&&QThread::currentThread()==&fileThread);},Qt::DirectConnection);
    QObject::connect(playback.get(),&radar::Playback::requestFrame,&app,[&](quint64 id,int index){++requests;playbackThreadCorrect&=QThread::currentThread()==app.thread();submitFrame(id,index);});
    QObject::connect(reader,&radar::DataReader::frameReady,playback.get(),&radar::Playback::acceptFrame,Qt::QueuedConnection);
    QObject::connect(reader,&radar::DataReader::error,playback.get(),&radar::Playback::acceptError,Qt::QueuedConnection);
    QObject::connect(playback.get(),&radar::Playback::stateChanged,view.get(),&radar::Frontend::setPlaybackState);
    QObject::connect(playback.get(),&radar::Playback::error,&app,[&](QString error){recordError(error);if(cli.isSet("headless"))finish();});
    QObject::connect(playback.get(),&radar::Playback::showFrame,&app,[&](radar::Frame frame){
        guiThreadCorrect&=QThread::currentThread()==app.thread();current=frame;++displayed;shown.append(frame.index);view->showFrame(frame);
        if(displayed==1&&cli.isSet("ready-file")){const QString path=cli.value("ready-file");QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(QJsonObject{{"ready",true},{"frame",frame.index}}).toJson())<0||!file.commit()){recordError("Cannot write readiness file");finish();}}
        if(!probes.isEmpty()&&!probeDone&&frame.index==probes[probePosition]){
            if(++probePosition<probes.size())QTimer::singleShot(0,playback.get(),[&]{playback->seek(probes[probePosition]);});else probeDone=true;
        }
        if(cli.isSet("verify-all")&&!verificationStarted){verificationStarted=true;submitFrame(verifyBase,0);}
        maybeDone();
    });
    QObject::connect(reader,&radar::DataReader::frameReady,&app,[&](quint64 id,radar::Frame frame){
        if(id<verifyBase||id>=verifyBase+quint64(dataset.frameCount))return;
        if(frame.index!=validated||id!=verifyBase+quint64(validated)||frame.radarImage.isNull()||frame.stereoImage.isNull()){
            recordError("Full sequence frame verification failed");finish();return;
        }
        ++validated;
        if(validated==dataset.frameCount){verifyDone=true;view->showStatus(QStringLiteral("已解码验证全部 %1 帧及配对图像").arg(validated));maybeDone();}
        else {if(validated%240==0)view->showStatus(QStringLiteral("正在验证完整数据：%1 / %2").arg(validated).arg(dataset.frameCount));submitFrame(verifyBase+quint64(validated),validated);}
    },Qt::QueuedConnection);
    QObject::connect(reader,&radar::DataReader::error,&app,[&](quint64 id,QString error){
        if(id==0||id==exportRequest||id>=verifyBase){recordError(error);if(cli.isSet("headless"))finish();}
        else view->showStatus(error);
    },Qt::QueuedConnection);
    QObject::connect(reader,&radar::DataReader::exported,&app,[&](quint64 id,QString path,int rows){view->exportCompleted(path,rows);if(id==exportRequest){exportedRows=rows;exportDone=true;maybeDone();}},Qt::QueuedConnection);
    QObject::connect(reader,&radar::DataReader::opened,&app,[&](radar::Dataset opened){
        dataset=opened;ready=true;view->setDataset(dataset);playback->setDataset(dataset);playback->setSpeed(speed);
        if(startFrame>=dataset.frameCount){recordError("Start frame exceeds recording range");finish();return;}
        for(int index:probes)if(index>=dataset.frameCount){recordError("Probe frame exceeds recording range");finish();return;}
        if(!probes.isEmpty())playback->seek(probes[0]);else if(startFrame)playback->seek(startFrame);
        if(cli.isSet("export"))QMetaObject::invokeMethod(reader,"exportCsv",Qt::QueuedConnection,Q_ARG(quint64,exportRequest),Q_ARG(QString,cli.value("export")),Q_ARG(int,first),Q_ARG(int,last<0?dataset.frameCount-1:last));
        if(cli.isSet("auto-play"))playback->play();
    },Qt::QueuedConnection);
    QObject::connect(view.get(),&radar::Frontend::openRequested,&app,[&](QString root){playback->pause();QMetaObject::invokeMethod(reader,"open",Qt::QueuedConnection,Q_ARG(QString,root));});
    QObject::connect(view.get(),&radar::Frontend::seekRequested,playback.get(),&radar::Playback::seek);
    QObject::connect(view.get(),&radar::Frontend::toggleRequested,playback.get(),&radar::Playback::toggle);
    QObject::connect(view.get(),&radar::Frontend::stepRequested,playback.get(),&radar::Playback::step);
    QObject::connect(view.get(),&radar::Frontend::speedRequested,playback.get(),&radar::Playback::setSpeed);
    QObject::connect(view.get(),&radar::Frontend::exportRequested,&app,[&](QString path,int begin,int end){QMetaObject::invokeMethod(reader,"exportCsv",Qt::QueuedConnection,Q_ARG(quint64,quint64(2000000001ULL)),Q_ARG(QString,path),Q_ARG(int,begin),Q_ARG(int,end));});
    fileThread.start();QSize initialSize(1480,980);
    if(!cli.isSet("headless"))if(const auto *screen=QGuiApplication::primaryScreen()) {
        const QSize available=screen->availableGeometry().size()-QSize(32,48);
        initialSize=initialSize.boundedTo(available).expandedTo(view->minimumSize());
    }
    view->resize(initialSize);view->show();
    QString root=cli.value("data");if(root.isEmpty()){const QString bundled=QDir(QCoreApplication::applicationDirPath()).filePath("full");if(QFileInfo::exists(bundled+"/manifest.json"))root=bundled;}
    if(!root.isEmpty())QTimer::singleShot(0,&app,[&,root]{QMetaObject::invokeMethod(reader,"open",Qt::QueuedConnection,Q_ARG(QString,root));});
    else {view->showStatus(QStringLiteral("请选择包含 manifest.json 的完整数据包目录"));if(cli.isSet("headless")){recordError("No data pack selected");finish();}}
    if(duration)QTimer::singleShot(duration,&app,[&]{if(!ready||displayed==0)recordError("Recording did not become ready within requested duration");finish();});
    app.exec();
    if(cli.isSet("screenshot")){const QString path=cli.value("screenshot");QDir().mkpath(QFileInfo(path).absolutePath());if(!view->grab().save(path)){recordError("Cannot save screenshot");}}
    playback->shutdown();QMetaObject::invokeMethod(reader,"shutdown",Qt::BlockingQueuedConnection);fileThread.quit();const bool stopped=fileThread.wait(10000);
    if(!stopped){qCritical("File worker did not stop");return 3;}
    QJsonObject report{{"sequence",dataset.sequence},{"frameCount",dataset.frameCount},{"firstTimestamp",double(dataset.firstTimestamp)},{"lastTimestamp",double(dataset.lastTimestamp)},{"liveInference",dataset.liveInference},{"recordingMode",dataset.recordingMode},{"displayedFrames",displayed},{"shownIndices",shown},{"validatedFrames",validated},{"exportedRows",exportedRows},{"requests",requests},{"errors",errors},{"currentIndex",current.index},{"currentPoseRow",radar::frameToJson(current)},{"radarImageWidth",current.radarImage.width()},{"stereoImageWidth",current.stereoImage.width()},{"threads",QJsonObject{{"gui",guiThreadCorrect},{"playback",playbackThreadCorrect},{"fileReader",readerThreadCorrect.load()}}},{"workerStopped",stopped}};
    if(cli.isSet("report")){const QString path=cli.value("report");QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(report).toJson())<0||!file.commit()){qCritical("Cannot write result report");return 4;}}
    qInfo("Replay finished: %d displayed, %d decoded, %d exported",displayed,validated,exportedRows);return exitResult;
}
