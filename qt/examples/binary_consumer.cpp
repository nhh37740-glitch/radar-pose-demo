// The consumer links the delivered public contract and loads three module DLLs.
// No implementation source of the modules is required or compiled here.
#include <radar/contracts.h>
#include <QtWidgets/QApplication>
#include <QtCore/QLibrary>
#include <QtCore/QDir>
#include <QtCore/QTemporaryDir>
#include <QtCore/QFileInfo>
#include <memory>
#include <vector>
int main(int argc,char **argv){
    qputenv("QT_QPA_PLATFORM","offscreen");qputenv("QT_QPA_FONTDIR",(qEnvironmentVariable("SystemRoot","C:/Windows")+"/Fonts").toUtf8());
    QApplication app(argc,argv);radar::registerTypes();if(argc!=3)return 2;
    const QString runtime=QString::fromLocal8Bit(argv[1]),data=QString::fromLocal8Bit(argv[2]);
    std::vector<std::unique_ptr<QLibrary>> libraries;
    auto load=[&](const char *name,const char *symbol){auto library=std::make_unique<QLibrary>(QDir(runtime).filePath(QString("radar_%1.dll").arg(name)));auto factory=library->resolve(symbol);if(!factory)qCritical("%s",qPrintable(library->errorString()));libraries.push_back(std::move(library));return factory;};
    auto readerFactory=reinterpret_cast<radar::DataReader*(*)(QObject*)>(load("data_reader","radar_create_reader"));
    auto playbackFactory=reinterpret_cast<radar::Playback*(*)(QObject*)>(load("playback","radar_create_playback"));
    auto frontendFactory=reinterpret_cast<radar::Frontend*(*)(QWidget*)>(load("frontend","radar_create_frontend"));
    if(!readerFactory||!playbackFactory||!frontendFactory)return 3;
    std::unique_ptr<radar::DataReader> reader(readerFactory(nullptr));std::unique_ptr<radar::Playback> playback(playbackFactory(nullptr));std::unique_ptr<radar::Frontend> view(frontendFactory(nullptr));
    radar::Dataset dataset;int shown=0,last=-1,exported=0,errors=0;
    QObject::connect(reader.get(),&radar::DataReader::error,&app,[&](quint64,QString error){++errors;qCritical("%s",qPrintable(error));});
    QObject::connect(playback.get(),&radar::Playback::error,&app,[&](QString){++errors;});
    QObject::connect(reader.get(),&radar::DataReader::opened,&app,[&](radar::Dataset value){dataset=value;view->setDataset(value);playback->setDataset(value);});
    QObject::connect(playback.get(),&radar::Playback::requestFrame,reader.get(),&radar::DataReader::readFrame);
    QObject::connect(reader.get(),&radar::DataReader::frameReady,playback.get(),&radar::Playback::acceptFrame);
    QObject::connect(playback.get(),&radar::Playback::showFrame,&app,[&](radar::Frame frame){if(frame.radarImage.isNull()||frame.stereoImage.isNull())++errors;view->showFrame(frame);++shown;last=frame.index;});
    QObject::connect(playback.get(),&radar::Playback::stateChanged,view.get(),&radar::Frontend::setPlaybackState);
    QObject::connect(reader.get(),&radar::DataReader::exported,&app,[&](quint64,QString,int rows){exported=rows;});
    reader->open(data);if(dataset.frameCount<2)return 4;playback->seek(dataset.frameCount-1);
    QTemporaryDir temporary(QDir::current().filePath("radar-consumer-XXXXXX"));if(!temporary.isValid())return 5;
    reader->exportCsv(100,temporary.filePath("sample.csv"),dataset.frameCount-2,dataset.frameCount-1);
    view->resize(1400,940);view->show();app.processEvents();const bool rendered=!view->grab().isNull();
    playback->shutdown();reader->shutdown();
    if(errors||shown!=2||last!=dataset.frameCount-1||exported!=2||!rendered)return 6;
    qInfo("All three business DLL factories replayed real paired frames and exported records through delivered interfaces.");return 0;
}
