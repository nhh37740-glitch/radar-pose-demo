#include <radar/contracts.h>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtGui/QDesktopServices>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtCore/QSignalBlocker>
#include <QtCore/QThread>
#include <QtCore/QUrl>
#include <QtCore/QDir>
#include <algorithm>
#include <cmath>

namespace {
const std::array<QColor,7> colors{{QColor("#f3f6fa"),QColor("#39d5c2"),QColor("#619eff"),QColor("#e9bd58"),QColor("#c18aff"),QColor("#ee839f"),QColor("#eb9157")}};
constexpr double pi=3.14159265358979323846;
QString number(double v) { return std::isfinite(v)?QString::number(v,'f',2):QStringLiteral("—"); }
void assertGui() { Q_ASSERT(QThread::currentThread()==qApp->thread()); }

class ImagePanel final:public QWidget {
public:
    explicit ImagePanel(QWidget *parent=nullptr):QWidget(parent) { setMinimumSize(230,150); setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding); }
    void setImage(const QImage &image) { image_=image; setProperty("hasImage",!image.isNull()); setProperty("imageSize",image.size()); update(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.fillRect(rect(),QColor("#080e16"));
        if(image_.isNull()) { p.setPen(QColor("#7d8fa6")); p.drawText(rect(),Qt::AlignCenter,QStringLiteral("等待保存的图像")); return; }
        const QSize size=image_.size().scaled(this->size(),Qt::KeepAspectRatio);
        const QRect target(QPoint((width()-size.width())/2,(height()-size.height())/2),size);
        p.setRenderHint(QPainter::SmoothPixmapTransform); p.drawImage(target,image_);
    }
private: QImage image_;
};

class TrajectoryPanel final:public QWidget {
public:
    explicit TrajectoryPanel(QWidget *parent=nullptr):QWidget(parent) { setMinimumSize(350,160); enabled_.fill(true); }
    void dataset(const radar::Dataset &d) { overview_=d.overview; for(auto &f:overview_) {f.radarImage=QImage();f.stereoImage=QImage();} bounds_=d.globalBounds; history_.clear(); current_={}; hasCurrent_=false; setProperty("historyCount",0); update(); }
    void frame(const radar::Frame &f) {
        if(hasCurrent_ && f.index<current_.index) history_.clear();
        if(history_.isEmpty()||history_.last().index!=f.index) history_.append(f); else history_.last()=f;
        if(history_.size()>1000) history_.remove(0,history_.size()-1000);
        // Histories retain poses only. Decoded camera/radar frames are never accumulated.
        history_.last().radarImage=QImage(); history_.last().stereoImage=QImage();
        current_=f; current_.radarImage=QImage(); current_.stereoImage=QImage(); hasCurrent_=true;
        setProperty("historyCount",history_.size()); update();
    }
    void method(int i,bool on) { enabled_[i]=on; update(); }
    void follow(bool on) { follow_=on; setProperty("followCurrent",on); update(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.fillRect(rect(),QColor("#0c1420"));
        const QRectF plot=QRectF(rect()).adjusted(42,18,-18,-32);
        double minE=0,maxE=1,minN=0,maxN=1;
        if(follow_&&hasCurrent_) { minE=current_.poses[0].east-64;maxE=minE+128;minN=current_.poses[0].north-64;maxN=minN+128; }
        else {
            bool first=true;
            for(const auto &f:overview_) for(int method=0;method<7;++method) {
                if(!enabled_[method]) continue;
                const auto &pose=f.poses[method];
                if(!std::isfinite(pose.east)||!std::isfinite(pose.north)) continue;
                if(first) {minE=maxE=pose.east;minN=maxN=pose.north;first=false;}
                else {minE=std::min(minE,pose.east);maxE=std::max(maxE,pose.east);minN=std::min(minN,pose.north);maxN=std::max(maxN,pose.north);}
            }
            if(first&&!bounds_.isEmpty()) { minE=bounds_.left();maxE=bounds_.right();minN=bounds_.top();maxN=bounds_.bottom(); }
            if(first&&hasCurrent_) { minE=current_.poses[0].east-64;maxE=minE+128;minN=current_.poses[0].north-64;maxN=minN+128; }
            const double pad=std::max({maxE-minE,maxN-minN,1.0})*.05;
            minE-=pad;maxE+=pad;minN-=pad;maxN+=pad;
        }
        // A metre has the same screen length on both axes.
        const double scale=std::min(plot.width()/std::max(maxE-minE,1.0),plot.height()/std::max(maxN-minN,1.0));
        const double centerE=(minE+maxE)/2,centerN=(minN+maxN)/2;
        const auto point=[&](const radar::Pose &v){return QPointF(plot.center().x()+(v.east-centerE)*scale,plot.center().y()-(v.north-centerN)*scale);};
        p.setPen(QPen(QColor("#203044"),1));
        for(int i=0;i<=4;++i) { const double x=plot.left()+plot.width()*i/4,y=plot.top()+plot.height()*i/4;p.drawLine(QPointF(x,plot.top()),QPointF(x,plot.bottom()));p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y)); }
        p.setPen(QColor("#9eafc5"));p.drawText(QRectF(0,0,width(),18),Qt::AlignCenter,QStringLiteral("北 N ↑"));
        p.drawText(QRectF(0,height()-27,width(),22),Qt::AlignCenter,QStringLiteral("东 E →   ·   %1 × %2 m").arg(number(plot.width()/scale),number(plot.height()/scale)));
        p.save(); p.setClipRect(plot);
        const auto &pathFrames=follow_?history_:overview_;
        for(int method=0;method<7;++method) if(enabled_[method]) {
            QPainterPath path;bool started=false;
            for(const auto &f:pathFrames) { const auto &v=f.poses[method]; if(!std::isfinite(v.east)||!std::isfinite(v.north)) {started=false;continue;} if(!started) {path.moveTo(point(v));started=true;} else path.lineTo(point(v)); }
            QColor c=colors[method];c.setAlpha(method==0?190:125);p.setPen(QPen(c,method==0?2.0:1.3));p.setBrush(Qt::NoBrush);p.drawPath(path);
            QColor pastColor=colors[method];pastColor.setAlpha(90);p.setPen(Qt::NoPen);p.setBrush(pastColor);
            const int markerStride=std::max(1,static_cast<int>(history_.size()/120));
            for(int h=0;h<history_.size();h+=markerStride) {const auto &pose=history_[h].poses[method];if(std::isfinite(pose.east)&&std::isfinite(pose.north))p.drawEllipse(point(pose),1.7,1.7);}
            if(hasCurrent_) {
                const auto &v=current_.poses[method]; if(!std::isfinite(v.east)||!std::isfinite(v.north)||!std::isfinite(v.yaw)) continue;
                const QPointF pos=point(v);const double a=v.yaw*pi/180;
                const QPointF direction(std::sin(a),-std::cos(a)),side(std::cos(a),std::sin(a));
                QPolygonF triangle;triangle<<pos+direction*10<<pos-direction*5+side*4<<pos-direction*5-side*4;
                p.setPen(QPen(colors[method],1));p.setBrush(colors[method]);p.drawPolygon(triangle);
            }
        }
        p.restore();
        if(!hasCurrent_&&overview_.isEmpty()) {p.setPen(QColor("#7d8fa6"));p.drawText(plot,Qt::AlignCenter,QStringLiteral("打开数据包后显示七种方法轨迹"));}
    }
private:
    QVector<radar::Frame> overview_,history_;QRectF bounds_;radar::Frame current_;std::array<bool,7> enabled_;bool follow_=false,hasCurrent_=false;
};

class ErrorPanel final:public QWidget {
public:
    explicit ErrorPanel(QWidget *parent=nullptr):QWidget(parent) {setMinimumHeight(110);}
    void reset() {frames_.clear();setProperty("historyCount",0);update();}
    void frame(const radar::Frame &f) {
        if(!frames_.isEmpty()&&f.index<frames_.last().index) frames_.clear();
        radar::Frame poses=f;poses.radarImage=QImage();poses.stereoImage=QImage();
        if(!frames_.isEmpty()&&f.index==frames_.last().index) frames_.last()=poses; else frames_.append(poses);
        if(frames_.size()>400) frames_.remove(0,frames_.size()-400);setProperty("historyCount",frames_.size());update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#0c1420"));
        p.setPen(QColor("#9eafc5"));p.drawText(QRectF(10,5,width()-20,20),QStringLiteral("平面误差 / m · 当前对比与已回放历史"));
        if(frames_.isEmpty()) {p.drawText(rect(),Qt::AlignCenter,QStringLiteral("等待帧数据"));return;}
        const QRectF bars(30,33,width()*.42-35,height()-61),lines(width()*.47,33,width()*.53-16,height()-61);
        double maxError=1;
        for(const auto &f:frames_) for(int i=1;i<7;++i) {double e=radar::planarError(f,i);if(std::isfinite(e))maxError=std::max(maxError,e);}
        p.setPen(QColor("#9eafc5"));p.drawText(QRectF(width()*.55,5,width()*.45-12,20),Qt::AlignRight|Qt::AlignVCenter,QStringLiteral("纵轴上限 %1 m").arg(number(maxError)));
        for(int i=1;i<7;++i) {
            const double error=radar::planarError(frames_.last(),i); const double col=bars.width()/6;
            if(std::isfinite(error)) {double h=bars.height()*error/maxError;p.fillRect(QRectF(bars.left()+(i-1)*col+4,bars.bottom()-h,col-8,h),colors[i]);}
            p.setPen(colors[i]);p.drawText(QRectF(bars.left()+(i-1)*col,bars.bottom()+3,col,16),Qt::AlignCenter,QString::number(i));
            QPainterPath path;bool started=false;
            for(int j=0;j<frames_.size();++j) {
                double e=radar::planarError(frames_[j],i);if(!std::isfinite(e)) {started=false;continue;}
                const double fraction=double(frames_[j].index-frames_.first().index)/std::max(1,frames_.last().index-frames_.first().index);
                QPointF pos(lines.left()+lines.width()*fraction,lines.bottom()-lines.height()*e/maxError);
                if(!started) {path.moveTo(pos);started=true;} else path.lineTo(pos);
            }
            p.setPen(QPen(colors[i],1.5));p.setBrush(Qt::NoBrush);p.drawPath(path);
            if(frames_.size()==1&&std::isfinite(error)) {p.setBrush(colors[i]);p.drawEllipse(QPointF(lines.left(),lines.bottom()-lines.height()*error/maxError),2.5,2.5);}
        }
        p.setPen(QColor("#7d8fa6"));p.drawText(QRectF(lines.left(),lines.bottom()+3,lines.width(),16),Qt::AlignCenter,QStringLiteral("帧 %1 — %2（最多 400 个已显示样本）").arg(frames_.first().index).arg(frames_.last().index));
    }
private: QVector<radar::Frame> frames_;
};

class Dashboard final:public radar::Frontend {
public:
    explicit Dashboard(QWidget *parent):Frontend(parent) {
        setObjectName("radarDashboard");setWindowTitle(QStringLiteral("雷达定位 · 保存结果回放"));resize(1400,940);setMinimumSize(1020,720);
        setStyleSheet(QStringLiteral("QWidget { background: #101a29; color: #dce6f2; font-family: 'Microsoft YaHei UI', 'Microsoft YaHei', 'Segoe UI'; font-size: 12px; } QGroupBox { border: 1px solid #2b3c52; border-radius: 8px; margin-top: 19px; padding: 8px; } QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; color: #b4c5da; } QPushButton { background: #24384e; border: 1px solid #385169; padding: 7px 12px; border-radius: 5px; } QPushButton:hover { background: #34516e; } QPushButton:disabled { color:#5f728b; background:#182536; } QSpinBox, QComboBox, QLineEdit { background:#0c1420; border:1px solid #385169; border-radius:4px; padding:5px; } QSlider::groove:horizontal { height:5px; background:#2b3c52; border-radius:2px; } QSlider::handle:horizontal { background:#39d5c2; width:14px; margin:-5px 0; border-radius:7px; } QTableWidget {background:#0c1420; gridline-color:#203044; border:0;} QHeaderView::section {background:#1b2a3e; border:0; padding:6px;} QCheckBox {spacing:6px;} QLabel#title {font-size:22px; font-weight:600;} QLabel#recordingNotice {color:#39d5c2;}"));
        auto *root=new QVBoxLayout(this);root->setContentsMargins(18,14,18,12);root->setSpacing(10);
        auto *head=new QHBoxLayout;auto *title=new QLabel(QStringLiteral("雷达定位 / 多方法对比"));title->setObjectName("title");head->addWidget(title);head->addStretch();
        auto *notice=new QLabel(QStringLiteral("已保存结果回放 · 不执行在线模型"));notice->setObjectName("recordingNotice");head->addWidget(notice);root->addLayout(head);
        datasetLabel_=new QLabel(QStringLiteral("尚未打开数据包 · 选择已准备好的数据包"));datasetLabel_->setObjectName("datasetLabel");root->addWidget(datasetLabel_);
        auto *controls=new QHBoxLayout;
        auto button=[&](const QString &text,const char *name){auto *b=new QPushButton(text);b->setObjectName(name);controls->addWidget(b);return b;};
        auto *open=button(QStringLiteral("打开数据包"),"openButton");prev_=button(QStringLiteral("◀ 上一帧"),"previousButton");play_=button(QStringLiteral("播放"),"playButton");next_=button(QStringLiteral("下一帧 ▶"),"nextButton");
        controls->addSpacing(8);controls->addWidget(new QLabel(QStringLiteral("速度")));speed_=new QComboBox;speed_->setObjectName("speedCombo");for(double s:{.25,.5,1.,2.,4.,8.,16.})speed_->addItem(QString::number(s)+QStringLiteral(" ×"),s);speed_->setCurrentIndex(2);controls->addWidget(speed_);controls->addStretch();export_=button(QStringLiteral("导出 CSV…"),"exportButton");root->addLayout(controls);
        auto *timeline=new QHBoxLayout;slider_=new QSlider(Qt::Horizontal);slider_->setObjectName("frameSlider");slider_->setRange(0,0);slider_->setTracking(false);timeline->addWidget(slider_,1);timeline->addWidget(new QLabel(QStringLiteral("帧")));spin_=new QSpinBox;spin_->setObjectName("frameSpin");spin_->setRange(0,0);spin_->setMinimumWidth(88);timeline->addWidget(spin_);count_=new QLabel(QStringLiteral("/ —"));count_->setObjectName("frameCountLabel");timeline->addWidget(count_);root->addLayout(timeline);
        timestamp_=new QLabel(QStringLiteral("雷达时间戳 —   |   相机时间戳 —   |   已用时间 —   |   同步差 —"));timestamp_->setObjectName("timestampLabel");root->addWidget(timestamp_);
        auto *split=new QSplitter(Qt::Horizontal);split->setObjectName("contentSplitter");root->addWidget(split,1);
        auto *left=new QWidget;auto *leftLayout=new QVBoxLayout(left);leftLayout->setContentsMargins(0,0,0,0);leftLayout->setSpacing(10);
        auto imageGroup=[&](const QString &title,const char *name){auto *g=new QGroupBox(title);auto *layout=new QVBoxLayout(g);auto *image=new ImagePanel;image->setObjectName(name);layout->addWidget(image);leftLayout->addWidget(g,1);return image;};
        camera_=imageGroup(QStringLiteral("相机 / 保存的真实图像"),"cameraImage");radar_=imageGroup(QStringLiteral("雷达 / 保存的真实扫描"),"radarImage");split->addWidget(left);
        auto *right=new QWidget;auto *rightLayout=new QVBoxLayout(right);rightLayout->setContentsMargins(0,0,0,0);rightLayout->setSpacing(8);
        auto *trackGroup=new QGroupBox(QStringLiteral("轨迹 · 当前朝向标记"));auto *trackLayout=new QVBoxLayout(trackGroup);auto *trackTop=new QHBoxLayout;trackTop->addWidget(new QLabel(QStringLiteral("全局概览包含完整保存轨迹")));trackTop->addStretch();auto *follow=new QCheckBox(QStringLiteral("跟随参考位置 · 128 m"));follow->setObjectName("followCheck");trackTop->addWidget(follow);trackLayout->addLayout(trackTop);
        auto *legend=new QGridLayout;const auto names=radar::methodNames();
        for(int i=0;i<7;++i) {auto *c=new QCheckBox(QStringLiteral("%1 %2").arg(i).arg(names.value(i)));c->setObjectName(QStringLiteral("methodCheck%1").arg(i));c->setChecked(true);c->setStyleSheet(QStringLiteral("color:%1;").arg(colors[i].name()));legend->addWidget(c,i/4,i%4);connect(c,&QCheckBox::toggled,this,[this,i](bool checked){trajectory_->method(i,checked);});}
        trackLayout->addLayout(legend);trajectory_=new TrajectoryPanel;trajectory_->setObjectName("trajectoryPanel");trackLayout->addWidget(trajectory_,1);rightLayout->addWidget(trackGroup,1);connect(follow,&QCheckBox::toggled,this,[this](bool on){trajectory_->follow(on);});
        auto *poseGroup=new QGroupBox(QStringLiteral("七种方法 · 位置 / 姿态 / 相对参考位置误差"));auto *poseLayout=new QVBoxLayout(poseGroup);table_=new QTableWidget(7,7);table_->setObjectName("poseTable");table_->setHorizontalHeaderLabels({QStringLiteral("方法"),QStringLiteral("北 / m"),QStringLiteral("东 / m"),QStringLiteral("下 / m"),QStringLiteral("航向 / °"),QStringLiteral("平面误差 / m"),QStringLiteral("航向误差 / °")});table_->verticalHeader()->hide();table_->verticalHeader()->setDefaultSectionSize(26);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSelectionMode(QAbstractItemView::NoSelection);table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);table_->setMinimumHeight(215);table_->setMaximumHeight(245);
        for(int row=0;row<7;++row)for(int col=0;col<7;++col) {auto *item=new QTableWidgetItem(col==0?QStringLiteral("%1 %2%3").arg(row).arg(names.value(row)).arg(row==0?QStringLiteral(" · 参考"):QString()):QStringLiteral("—"));item->setForeground(colors[row]);if(col)item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);table_->setItem(row,col,item);}
        poseGroup->setObjectName("poseGroup");poseGroup->setStyleSheet(QStringLiteral("QGroupBox#poseGroup { padding:4px; }"));poseGroup->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);poseLayout->setContentsMargins(6,0,6,0);poseLayout->setSpacing(4);table_->verticalHeader()->setDefaultSectionSize(24);fitPoseTable();
        poseLayout->addWidget(table_);vo_=new QLabel(QStringLiteral("图像主导估计：等待帧数据"));vo_->setObjectName("voAcceptedLabel");poseLayout->addWidget(vo_);rightLayout->addWidget(poseGroup);
        errors_=new ErrorPanel;errors_->setObjectName("errorPanel");errors_->setFixedHeight(110);rightLayout->addWidget(errors_);split->addWidget(right);split->setStretchFactor(0,4);split->setStretchFactor(1,6);split->setSizes({510,850});
        status_=new QLabel(QStringLiteral("就绪 · 打开数据包开始回放"));status_->setObjectName("statusLabel");status_->setWordWrap(true);root->addWidget(status_);
        auto *license=new QLabel(QStringLiteral("Oxford Radar RobotCar · 数据许可 CC BY-NC-SA 4.0 · <a href=\"https://oxford-robotics-institute.github.io/radar-robotcar-dataset/\">数据集与署名</a> · <a href=\"https://creativecommons.org/licenses/by-nc-sa/4.0/\">许可条款</a>"));license->setObjectName("licenseLabel");license->setTextInteractionFlags(Qt::TextBrowserInteraction);license->setOpenExternalLinks(true);root->addWidget(license);
        connect(open,&QPushButton::clicked,this,[this]{const auto dir=QFileDialog::getExistingDirectory(this,QStringLiteral("打开保存结果数据包"),dataset_.root);if(!dir.isEmpty())emit openRequested(dir);});
        connect(play_,&QPushButton::clicked,this,&radar::Frontend::toggleRequested);connect(prev_,&QPushButton::clicked,this,[this]{emit stepRequested(-1);});connect(next_,&QPushButton::clicked,this,[this]{emit stepRequested(1);});
        connect(slider_,&QSlider::valueChanged,this,[this](int index){QSignalBlocker block(spin_);spin_->setValue(index);emit seekRequested(index);});connect(spin_,qOverload<int>(&QSpinBox::valueChanged),this,[this](int index){QSignalBlocker block(slider_);slider_->setValue(index);emit seekRequested(index);});
        connect(speed_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){emit speedRequested(speed_->itemData(index).toDouble());});connect(export_,&QPushButton::clicked,this,[this]{exportDialog();});enabled(false);
    }
    void setDataset(radar::Dataset dataset) override {
        assertGui();dataset_=std::move(dataset);const int last=std::max(0,dataset_.frameCount-1);{QSignalBlocker a(slider_),b(spin_);slider_->setRange(0,last);spin_->setRange(0,last);slider_->setValue(0);spin_->setValue(0);}
        count_->setText(QStringLiteral("/ %1 · 共 %2 帧").arg(last).arg(dataset_.frameCount));datasetLabel_->setText(QStringLiteral("%1 · %2 帧 · 保存结果回放").arg(dataset_.sequence).arg(dataset_.frameCount));datasetLabel_->setToolTip(dataset_.root);trajectory_->dataset(dataset_);errors_->reset();camera_->setImage({});radar_->setImage({});
        for(int row=0;row<7;++row)for(int col=1;col<7;++col)table_->item(row,col)->setText(QStringLiteral("—"));vo_->setText(QStringLiteral("图像主导估计：等待帧数据"));vo_->setProperty("accepted",false);timestamp_->setText(QStringLiteral("雷达时间戳 —   |   相机时间戳 —   |   已用时间 —   |   同步差 —"));play_->setText(QStringLiteral("播放"));enabled(dataset_.frameCount>0);showStatus(QStringLiteral("数据包已加载 · %1 帧").arg(dataset_.frameCount));
    }
    void showFrame(radar::Frame frame) override {
        assertGui();if(frame.index<0||frame.index>=dataset_.frameCount)return;
        {QSignalBlocker a(slider_),b(spin_);slider_->setValue(frame.index);spin_->setValue(frame.index);}
        camera_->setImage(frame.stereoImage);radar_->setImage(frame.radarImage);trajectory_->frame(frame);errors_->frame(frame);
        timestamp_->setText(QStringLiteral("帧 %1   |   雷达 %2 µs   |   相机 %3 µs   |   已用 %4 s   |   同步差（相机 − 雷达）%5 ms").arg(frame.index).arg(frame.radarTimestamp).arg(frame.stereoTimestamp).arg(QString::number((frame.radarTimestamp-dataset_.firstTimestamp)/1e6,'f',3)).arg(QString::number((frame.stereoTimestamp-frame.radarTimestamp)/1000.,'f',3)));
        for(int i=0;i<7;++i) {const auto &pose=frame.poses[i];const QStringList values{number(pose.north),number(pose.east),number(pose.down),number(pose.yaw),i==0?QStringLiteral("参考"):number(radar::planarError(frame,i)),i==0?QStringLiteral("参考"):number(radar::yawError(frame,i))};for(int j=0;j<6;++j)table_->item(i,j+1)->setText(values[j]);}
        vo_->setText(frame.voModelAccepted?QStringLiteral("图像主导估计：已采用模型结果"):QStringLiteral("图像主导估计：未采用模型结果"));vo_->setProperty("accepted",frame.voModelAccepted);
    }
    void setPlaybackState(bool playing,int index,double speed) override {
        assertGui();play_->setText(playing?QStringLiteral("暂停"):QStringLiteral("播放"));play_->setProperty("playing",playing);
        QSignalBlocker a(slider_),b(spin_),c(speed_);slider_->setValue(index);spin_->setValue(index);for(int i=0;i<speed_->count();++i)if(qFuzzyCompare(speed_->itemData(i).toDouble(),speed))speed_->setCurrentIndex(i);
    }
    void showStatus(QString detail) override {assertGui();detail.replace('\n',' ');status_->setText(detail.left(600));status_->setToolTip(detail.left(1200));}
    void exportCompleted(QString path,int rows) override {showStatus(QStringLiteral("CSV 导出完成 · %1 行 · %2").arg(rows).arg(path));}
protected:
    void showEvent(QShowEvent *event) override {QWidget::showEvent(event);fitPoseTable();}
private:
    void fitPoseTable() {
        table_->ensurePolished();table_->horizontalHeader()->ensurePolished();table_->verticalHeader()->ensurePolished();
        int rows=0;for(int row=0;row<table_->rowCount();++row)rows+=table_->rowHeight(row);
        const int header=std::max(table_->horizontalHeader()->height(),table_->horizontalHeader()->sizeHint().height());
        table_->setFixedHeight(header+rows+2*table_->frameWidth()+2);
    }
    void enabled(bool on) {const std::array<QWidget*,7> widgets{{prev_,play_,next_,export_,slider_,spin_,speed_}};for(QWidget *w:widgets)w->setEnabled(on);}
    void exportDialog() {
        QDialog dialog(this);dialog.setObjectName("exportDialog");dialog.setWindowTitle(QStringLiteral("导出 CSV · 包含起止帧"));auto *layout=new QVBoxLayout(&dialog);layout->addWidget(new QLabel(QStringLiteral("七种方法的保存姿态；起止帧均包含在导出结果中。")));
        auto *path=new QLineEdit(QDir(dataset_.root).filePath("radar-replay.csv"));path->setObjectName("exportPath");auto *pathRow=new QHBoxLayout;pathRow->addWidget(path,1);auto *browse=new QPushButton(QStringLiteral("选择文件…"));browse->setObjectName("exportBrowse");pathRow->addWidget(browse);layout->addLayout(pathRow);connect(browse,&QPushButton::clicked,&dialog,[this,path]{const QString file=QFileDialog::getSaveFileName(this,QStringLiteral("导出保存结果 CSV"),path->text(),QStringLiteral("CSV (*.csv)"));if(!file.isEmpty())path->setText(file);});
        auto *range=new QHBoxLayout;auto *first=new QSpinBox;auto *last=new QSpinBox;first->setObjectName("exportFirst");last->setObjectName("exportLast");first->setRange(0,dataset_.frameCount-1);last->setRange(0,dataset_.frameCount-1);last->setValue(dataset_.frameCount-1);range->addWidget(new QLabel(QStringLiteral("起始帧")));range->addWidget(first);range->addWidget(new QLabel(QStringLiteral("结束帧")));range->addWidget(last);layout->addLayout(range);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);buttons->setObjectName("exportButtons");buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("导出"));buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(first,qOverload<int>(&QSpinBox::valueChanged),&dialog,[last](int value){last->setMinimum(value);});connect(last,qOverload<int>(&QSpinBox::valueChanged),&dialog,[first](int value){first->setMaximum(value);});
        dialog.resize(560,170);if(dialog.exec()==QDialog::Accepted&&!path->text().trimmed().isEmpty())emit exportRequested(path->text().trimmed(),first->value(),last->value());
    }
    radar::Dataset dataset_;QLabel *datasetLabel_=nullptr,*count_=nullptr,*timestamp_=nullptr,*vo_=nullptr,*status_=nullptr;QPushButton *prev_=nullptr,*play_=nullptr,*next_=nullptr,*export_=nullptr;QSlider *slider_=nullptr;QSpinBox *spin_=nullptr;QComboBox *speed_=nullptr;ImagePanel *camera_=nullptr,*radar_=nullptr;TrajectoryPanel *trajectory_=nullptr;ErrorPanel *errors_=nullptr;QTableWidget *table_=nullptr;
};
}
extern "C" RADAR_FRONTEND_API radar::Frontend *radar_create_frontend(QWidget *parent) {assertGui();return new Dashboard(parent);}
