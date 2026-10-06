#include <radar/contracts.h>
#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QTableWidget>
#include <QtCore/QTimer>
#include <QtCore/QDir>
#include <QtCore/QtMath>
#include <memory>
#include <algorithm>

namespace {
radar::Dataset data() {
    radar::Dataset d;d.frameCount=7203;d.sequence=QStringLiteral("Oxford 保存数据测试");d.root=QDir::tempPath();d.firstTimestamp=1000000;d.lastTimestamp=721300000;
    for(int i=0;i<50;++i) {radar::Frame f;f.index=i;for(int j=0;j<7;++j) {f.poses[j].north=i*2+j;f.poses[j].east=i+j;}d.overview.append(f);}return d;
}
radar::Frame frame(int index) {
    radar::Frame f;f.index=index;f.radarTimestamp=1000000+index*100000;f.stereoTimestamp=f.radarTimestamp+2500;f.voModelAccepted=true;
    f.poses[0]={10,20,3,359};for(int i=1;i<7;++i) f.poses[i]={13,24,5,1};
    f.stereoImage=QImage(320,120,QImage::Format_RGB32);f.stereoImage.fill(QColor("#2764ba"));f.radarImage=QImage(100,100,QImage::Format_RGB32);f.radarImage.fill(QColor("#ba6432"));return f;
}
template<class T> T *child(radar::Frontend *w,const char *name) {return w->findChild<T*>(QString::fromLatin1(name));}
}

class FrontendTest:public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {radar::registerTypes();}
    void controlsAndCompleteRange() {
        std::unique_ptr<radar::Frontend> w(radar_create_frontend(nullptr));w->setDataset(data());w->show();QTest::qWait(50);
        QSignalSpy seek(w.get(),&radar::Frontend::seekRequested),toggle(w.get(),&radar::Frontend::toggleRequested),step(w.get(),&radar::Frontend::stepRequested),speed(w.get(),&radar::Frontend::speedRequested);
        auto *slider=child<QSlider>(w.get(),"frameSlider");auto *spin=child<QSpinBox>(w.get(),"frameSpin");QVERIFY(slider);QVERIFY(spin);QCOMPARE(slider->maximum(),7202);QCOMPARE(spin->maximum(),7202);
        QTest::mouseClick(child<QPushButton>(w.get(),"playButton"),Qt::LeftButton);QCOMPARE(toggle.size(),1);
        QTest::mouseClick(child<QPushButton>(w.get(),"previousButton"),Qt::LeftButton);QTest::mouseClick(child<QPushButton>(w.get(),"nextButton"),Qt::LeftButton);QCOMPARE(step.size(),2);QCOMPARE(step[0][0].toInt(),-1);QCOMPARE(step[1][0].toInt(),1);
        slider->setFocus();QTest::keyClick(slider,Qt::Key_End);QCOMPARE(slider->value(),7202);QCOMPARE(spin->value(),7202);QCOMPARE(seek.last()[0].toInt(),7202);
        spin->setFocus();spin->selectAll();QTest::keyClicks(spin,"1234");QTest::keyClick(spin,Qt::Key_Return);QCOMPARE(spin->value(),1234);QCOMPARE(slider->value(),1234);QCOMPARE(seek.last()[0].toInt(),1234);
        auto *combo=child<QComboBox>(w.get(),"speedCombo");combo->setFocus();QTest::keyClick(combo,Qt::Key_End);QCOMPARE(combo->currentData().toDouble(),16.);QCOMPARE(speed.last()[0].toDouble(),16.);
        const int requests=seek.size(),speeds=speed.size();w->setPlaybackState(true,7199,.25);QCOMPARE(seek.size(),requests);QCOMPARE(speed.size(),speeds);QCOMPARE(spin->value(),7199);QCOMPARE(combo->currentData().toDouble(),.25);QCOMPARE(child<QPushButton>(w.get(),"playButton")->text(),QStringLiteral("暂停"));
    }
    void exportInclusiveRange() {
        std::unique_ptr<radar::Frontend> w(radar_create_frontend(nullptr));w->setDataset(data());w->show();QSignalSpy exported(w.get(),&radar::Frontend::exportRequested);
        bool dialogFound=false;int defaultFirst=-1,defaultLast=-1;QTimer::singleShot(0,w.get(),[&]{auto *dialog=child<QDialog>(w.get(),"exportDialog");if(!dialog)return;dialogFound=true;auto *first=dialog->findChild<QSpinBox*>("exportFirst");auto *last=dialog->findChild<QSpinBox*>("exportLast");defaultFirst=first->value();defaultLast=last->value();dialog->findChild<QLineEdit*>("exportPath")->setText("C:/saved-results.csv");auto *buttons=dialog->findChild<QDialogButtonBox*>("exportButtons");QTest::mouseClick(buttons->button(QDialogButtonBox::Save),Qt::LeftButton);});
        QTest::mouseClick(child<QPushButton>(w.get(),"exportButton"),Qt::LeftButton);QVERIFY(dialogFound);QCOMPARE(defaultFirst,0);QCOMPARE(defaultLast,7202);QCOMPARE(exported.size(),1);QCOMPARE(exported[0][0].toString(),QString("C:/saved-results.csv"));QCOMPARE(exported[0][1].toInt(),0);QCOMPARE(exported[0][2].toInt(),7202);
        QTimer::singleShot(0,w.get(),[&]{auto *dialog=child<QDialog>(w.get(),"exportDialog");if(!dialog)return;dialog->findChild<QSpinBox*>("exportFirst")->setValue(18);dialog->findChild<QSpinBox*>("exportLast")->setValue(42);auto *buttons=dialog->findChild<QDialogButtonBox*>("exportButtons");QTest::mouseClick(buttons->button(QDialogButtonBox::Save),Qt::LeftButton);});
        QTest::mouseClick(child<QPushButton>(w.get(),"exportButton"),Qt::LeftButton);QCOMPARE(exported.size(),2);QCOMPARE(exported[1][1].toInt(),18);QCOMPARE(exported[1][2].toInt(),42);
    }
    void valuesImagesAndNativeRendering() {
        std::unique_ptr<radar::Frontend> w(radar_create_frontend(nullptr));w->setDataset(data());QSignalSpy seek(w.get(),&radar::Frontend::seekRequested);w->showFrame(frame(123));w->resize(1480,980);w->show();QTest::qWait(30);
        QCOMPARE(seek.size(),0);auto *table=child<QTableWidget>(w.get(),"poseTable");QVERIFY(table);QCOMPARE(table->rowCount(),7);QCOMPARE(table->item(0,1)->text(),QString("10.00"));QCOMPARE(table->item(1,5)->text(),QString("5.00"));QCOMPARE(table->item(1,6)->text(),QString("2.00"));
        QVERIFY(table->item(0,0)->text().contains(QStringLiteral("参考")));QVERIFY(child<QLabel>(w.get(),"voAcceptedLabel")->property("accepted").toBool());
        const QRect lastRow=table->visualItemRect(table->item(6,0));QVERIFY(lastRow.height()>0);QVERIFY2(table->viewport()->rect().contains(lastRow),"All seven pose rows must fit in the polished viewport");QCOMPARE(table->verticalScrollBar()->maximum(),0);QVERIFY(!table->verticalScrollBar()->isVisible());
        auto *camera=child<QWidget>(w.get(),"cameraImage");auto *radar=child<QWidget>(w.get(),"radarImage");QVERIFY(camera->property("hasImage").toBool());QCOMPARE(camera->property("imageSize").toSize(),QSize(320,120));QVERIFY(radar->property("hasImage").toBool());
        const auto cameraGrab=camera->grab().toImage();QVERIFY(!cameraGrab.isNull());QCOMPARE(cameraGrab.pixelColor(cameraGrab.width()/2,cameraGrab.height()/2),QColor("#2764ba"));QCOMPARE(cameraGrab.pixelColor(1,1),QColor("#080e16"));
        const QString timestamp=child<QLabel>(w.get(),"timestampLabel")->text();QVERIFY(timestamp.contains("123"));QVERIFY(timestamp.contains("2.500 ms"));QVERIFY(timestamp.contains("12.300 s"));
        auto *follow=child<QCheckBox>(w.get(),"followCheck");QTest::mouseClick(follow,Qt::LeftButton);QVERIFY(child<QWidget>(w.get(),"trajectoryPanel")->property("followCurrent").toBool());
        const auto grabbed=w->grab();QVERIFY(!grabbed.isNull());QVERIFY(grabbed.width()>=1020);QVERIFY(grabbed.height()>=720);
        const auto *license=child<QLabel>(w.get(),"licenseLabel");QVERIFY(license->text().contains("CC BY-NC-SA 4.0"));QVERIFY(license->text().contains("creativecommons.org"));QVERIFY(child<QLabel>(w.get(),"recordingNotice")->text().contains(QStringLiteral("不执行在线模型")));
        w->showStatus(QString(10000,'a'));QVERIFY(child<QLabel>(w.get(),"statusLabel")->text().size()<=600);
    }
    void boundedHistoriesResetOnBackwardSeekAndDataset() {
        std::unique_ptr<radar::Frontend> w(radar_create_frontend(nullptr));w->setDataset(data());auto *track=child<QWidget>(w.get(),"trajectoryPanel");auto *errors=child<QWidget>(w.get(),"errorPanel");
        for(int i=0;i<1105;++i)w->showFrame(frame(i));QCOMPARE(track->property("historyCount").toInt(),1000);QCOMPARE(errors->property("historyCount").toInt(),400);
        w->showFrame(frame(10));QCOMPARE(track->property("historyCount").toInt(),1);QCOMPARE(errors->property("historyCount").toInt(),1);w->showFrame(frame(10));QCOMPARE(track->property("historyCount").toInt(),1);
        w->setDataset(data());QCOMPARE(track->property("historyCount").toInt(),0);QCOMPARE(errors->property("historyCount").toInt(),0);QVERIFY(!child<QWidget>(w.get(),"cameraImage")->property("hasImage").toBool());QCOMPARE(child<QTableWidget>(w.get(),"poseTable")->item(1,1)->text(),QStringLiteral("—"));
        radar::Dataset empty;w->setDataset(empty);QVERIFY(!child<QPushButton>(w.get(),"playButton")->isEnabled());QVERIFY(!child<QPushButton>(w.get(),"exportButton")->isEnabled());
    }
    void sparseHistoryUsesActualFrameCoordinates() {
        std::unique_ptr<radar::Frontend> w(radar_create_frontend(nullptr));w->setDataset(data());w->resize(1480,980);w->show();QTest::qWait(30);
        for(int index:{0,100,400}) {radar::Frame f;f.index=index;f.poses[1].north=index==100?100:0;w->showFrame(f);}
        auto *chart=child<QWidget>(w.get(),"errorPanel");QVERIFY(chart);const auto image=chart->grab().toImage();const qreal ratio=image.devicePixelRatio();
        // The sparse middle sample is one quarter of the actual 0..400 frame range.
        // A sample-ordinal axis would put this peak at the halfway point instead.
        const double left=chart->width()*.47,span=chart->width()*.53-16;
        auto hasTealNear=[&](double fraction){const QPoint center(qRound((left+span*fraction)*ratio),qRound(33*ratio));const int radius=qCeil(3*ratio);for(int y=center.y()-radius;y<=center.y()+radius;++y)for(int x=center.x()-radius;x<=center.x()+radius;++x) {if(!image.rect().contains(x,y))continue;const QColor c=image.pixelColor(x,y);if(qAbs(c.red()-57)<18&&qAbs(c.green()-213)<18&&qAbs(c.blue()-194)<18)return true;}return false;};
        QVERIFY2(hasTealNear(.25),"Peak at frame100 must appear at25% of actual frame range");QVERIFY2(!hasTealNear(.5),"Sparse frame100 must not be shown as the ordinal midpoint");
    }
    void openTrajectoryPathsNeverFillTheirInterior() {
        std::unique_ptr<radar::Frontend> w(radar_create_frontend(nullptr));radar::Dataset d;d.frameCount=3;
        for(int i=0;i<3;++i) {radar::Frame f;f.index=i;for(auto &pose:f.poses) {pose.east=i==0?0:100;pose.north=i==2?100:0;}d.overview.append(f);}
        w->setDataset(d);w->showFrame(d.overview.last());for(int i=2;i<7;++i)child<QCheckBox>(w.get(),qPrintable(QStringLiteral("methodCheck%1").arg(i)))->setChecked(false);w->resize(1480,980);w->show();QTest::qWait(30);
        auto *chart=child<QWidget>(w.get(),"trajectoryPanel");const auto image=chart->grab().toImage();const qreal ratio=image.devicePixelRatio();const QRectF plot=QRectF(chart->rect()).adjusted(42,18,-18,-32);const double scale=std::min(plot.width()/110.,plot.height()/110.);
        const QPointF inside=plot.center()+QPointF(25*scale,25*scale);const QColor c=image.pixelColor(qRound(inside.x()*ratio),qRound(inside.y()*ratio));
        QVERIFY2(c.red()<80&&c.green()<80&&c.blue()<80,"An open trajectory may draw strokes, but must not fill the triangle between its endpoints");
    }
};
QTEST_MAIN(FrontendTest)
#include "frontend_test.moc"
