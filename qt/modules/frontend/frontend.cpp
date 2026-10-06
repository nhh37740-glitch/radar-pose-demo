#include <radar/contracts.h>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QAbstractSpinBox>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QStyle>
#include <QtWidgets/QVBoxLayout>
#include <QtGui/QDesktopServices>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QKeyEvent>
#include <QtCore/QSignalBlocker>
#include <QtCore/QScopedValueRollback>
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
    explicit ImagePanel(QWidget *parent=nullptr):QWidget(parent) { setMinimumSize(230,130); setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding); setFocusPolicy(Qt::ClickFocus); }
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
    explicit TrajectoryPanel(QWidget *parent=nullptr):QWidget(parent) { setMinimumSize(350,120); setFocusPolicy(Qt::ClickFocus); enabled_.fill(true); }
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
        p.setPen(QColor("#9eafc5"));p.drawText(QRectF(0,0,width(),18),Qt::AlignCenter,QStringLiteral("北 ↑"));
        p.drawText(QRectF(0,height()-27,width(),22),Qt::AlignCenter,QStringLiteral("东 →   ·   显示范围 %1 × %2 m").arg(number(plot.width()/scale),number(plot.height()/scale)));
        p.save(); p.setClipRect(plot);
        const auto &pathFrames=follow_?history_:overview_;
        for(int method=0;method<7;++method) if(enabled_[method]) {
            QPainterPath path;bool started=false;
            for(const auto &f:pathFrames) { const auto &v=f.poses[method]; if(!std::isfinite(v.east)||!std::isfinite(v.north)) {started=false;continue;} if(!started) {path.moveTo(point(v));started=true;} else path.lineTo(point(v)); }
            QColor c=colors[method];c.setAlpha(method==0?230:180);p.setPen(QPen(c,method==0?2.0:1.5));p.setBrush(Qt::NoBrush);p.drawPath(path);
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
    explicit ErrorPanel(QWidget *parent=nullptr):QWidget(parent) {setMinimumHeight(160);setToolTip(QStringLiteral("位置误差相对参考位置计算。横轴按实际帧号排列，仅保留最近 400 个已回放样本；向前跳转留下的间隔不代表加载了中间帧。"));}
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
        p.setPen(QColor("#b8c8db"));p.drawText(QRectF(12,8,width()*.42-12,18),QStringLiteral("当前位置误差 / m"));p.drawText(QRectF(width()*.47,8,width()*.23,18),QStringLiteral("已回放趋势 / m"));
        if(frames_.isEmpty()) {p.drawText(rect(),Qt::AlignCenter,QStringLiteral("等待帧数据"));return;}
        const QRectF bars(20,38,width()*.42-25,height()-86),lines(width()*.47,38,width()*.53-16,height()-86);
        double maxError=1;
        for(const auto &f:frames_) for(int i=1;i<7;++i) {double e=radar::planarError(f,i);if(std::isfinite(e))maxError=std::max(maxError,e);}
        p.setPen(QColor("#9eafc5"));p.drawText(QRectF(width()*.70,8,width()*.30-12,18),Qt::AlignRight|Qt::AlignVCenter,QStringLiteral("上限 %1 m").arg(number(maxError)));
        p.setPen(QPen(QColor("#203044"),1));for(int line=0;line<=3;++line) {const double y=lines.top()+lines.height()*line/3;p.drawLine(QPointF(lines.left(),y),QPointF(lines.right(),y));}
        const std::array<QString,6> labels{{QStringLiteral("雷达\n主导"),QStringLiteral("多帧\n估计"),QStringLiteral("单帧\n估计"),QStringLiteral("图像\n主导"),QStringLiteral("仅图像"),QStringLiteral("仅雷达")}};
        for(int i=1;i<7;++i) {
            const double error=radar::planarError(frames_.last(),i); const double col=bars.width()/6;
            if(std::isfinite(error)) {double h=bars.height()*error/maxError;p.fillRect(QRectF(bars.left()+(i-1)*col+4,bars.bottom()-h,col-8,h),colors[i]);}
            p.setPen(colors[i]);p.drawText(QRectF(bars.left()+(i-1)*col,bars.bottom()+4,col,32),Qt::AlignCenter,labels[i-1]);
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
        p.setPen(QColor("#8ea3bc"));p.drawText(QRectF(lines.left(),lines.bottom()+7,lines.width(),32),Qt::AlignCenter,QStringLiteral("帧 %1 — %2 · %3 个已回放样本").arg(frames_.first().index).arg(frames_.last().index).arg(frames_.size()));
    }
private: QVector<radar::Frame> frames_;
};

class Dashboard final:public radar::Frontend {
public:
    explicit Dashboard(QWidget *parent):Frontend(parent) {
        setObjectName("radarDashboard");
        setWindowTitle(QStringLiteral("雷达定位 · 保存结果回放"));
        setFocusPolicy(Qt::StrongFocus);
        resize(1480,980);
        setMinimumSize(1020,720);
        setStyleSheet(QString::fromLatin1(R"css(
            QWidget { background:#0b131f; color:#dce6f2; font-family:'Microsoft YaHei UI','Microsoft YaHei','Segoe UI'; font-size:12px; }
            QLabel, QCheckBox { background:transparent; }
            QFrame#sensorCameraTile, QFrame#sensorRadarTile, QFrame#trajectoryCard { background:#111d2c; border:1px solid #26384d; border-radius:10px; }
            QFrame#playbackDock { background:#142234; border:1px solid #30475c; border-radius:10px; }
            QPushButton { background:#1b2b3e; border:1px solid #344b62; padding:7px 12px; border-radius:6px; }
            QPushButton:hover { background:#2a4057; border-color:#506c85; }
            QPushButton:pressed { background:#17283b; }
            QPushButton:disabled { color:#536980; background:#152234; border-color:#27394a; }
            QPushButton#playButton { background:#3bd1b7; color:#062f2a; border:1px solid #55e1c9; font-size:13px; font-weight:600; padding:9px 18px; }
            QPushButton#playButton:hover { background:#63dfc8; }
            QPushButton#playButton:disabled { color:#668e88; background:#21443f; border-color:#315851; }
            QPushButton#syncDetailsButton { background:transparent; color:#91a9c1; border:0; padding:5px; }
            QSpinBox, QComboBox, QLineEdit { background:#0c1725; border:1px solid #385069; border-radius:5px; padding:5px; }
            QComboBox QAbstractItemView { background:#142234; selection-background-color:#29455e; }
            QSlider::groove:horizontal { height:5px; background:#2a4058; border-radius:2px; }
            QSlider::sub-page:horizontal { background:#37baa8; border-radius:2px; }
            QSlider::handle:horizontal { background:#52dcc4; border:2px solid #173e3a; width:14px; margin:-6px 0; border-radius:8px; }
            QSplitter::handle { background:transparent; width:8px; }
            QSplitter::handle:hover { background:#243748; }
            QTabWidget::pane { background:#111d2c; border:1px solid #26384d; border-radius:7px; }
            QTabBar::tab { background:#111b29; color:#96acc4; padding:8px 18px; min-width:105px; border:0; border-bottom:2px solid transparent; }
            QTabBar::tab:selected { background:#1a2b3e; color:#e5f3f1; border-bottom:2px solid #3bd1b7; }
            QTabBar::tab:hover { color:#dcf3ef; }
            QTableWidget { background:#0e1826; alternate-background-color:#132132; gridline-color:#233347; border:0; font-size:11px; }
            QHeaderView::section { background:#1b2a3e; color:#b0c2d7; border:0; padding:6px; font-size:11px; }
            QCheckBox { spacing:5px; }
            QLabel#title { font-size:21px; font-weight:600; color:#eef5fc; }
            QLabel#datasetLabel { color:#91a8c0; font-size:11px; }
            QLabel#recordingNotice { color:#58c5b4; font-size:11px; }
            QLabel#voAcceptedLabel { color:#93b7b1; font-size:11px; }
            QLabel#statusLabel { color:#91a5bc; font-size:10px; }
            QLabel#licenseLabel { color:#7f94ac; font-size:10px; }
        )css"));
        auto *root=new QVBoxLayout(this);
        root->setContentsMargins(14,12,14,10);
        root->setSpacing(8);
        const auto makeButton=[](const QString &text,const char *name) {
            auto *button=new QPushButton(text);button->setObjectName(name);return button;
        };

        auto *header=new QHBoxLayout;
        header->setSpacing(12);
        auto *heading=new QVBoxLayout;
        heading->setSpacing(3);
        auto *title=new QLabel(QStringLiteral("雷达定位 / 多方法对比"));
        title->setObjectName("title");
        heading->addWidget(title);
        datasetLabel_=new QLabel(QStringLiteral("选择已准备好的数据包，开始查看录制结果"));
        datasetLabel_->setObjectName("datasetLabel");
        datasetLabel_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
        heading->addWidget(datasetLabel_);
        header->addLayout(heading,1);
        auto *notice=new QLabel(QStringLiteral("已保存结果回放 · 不执行在线模型"));
        notice->setObjectName("recordingNotice");
        header->addWidget(notice);
        auto *open=makeButton(QStringLiteral("打开数据包"),"openButton");
        export_=makeButton(QStringLiteral("导出 CSV…"),"exportButton");
        header->addWidget(open);
        header->addWidget(export_);
        root->addLayout(header);

        auto *split=new QSplitter(Qt::Horizontal);
        split->setObjectName("contentSplitter");
        split->setChildrenCollapsible(false);
        split->setHandleWidth(8);
        root->addWidget(split,1);
        auto *left=new QWidget;
        auto *leftLayout=new QVBoxLayout(left);
        leftLayout->setContentsMargins(0,0,0,0);
        leftLayout->setSpacing(10);
        const auto imageTile=[&](const QString &title,const QString &tag,const char *tileName,const char *imageName) {
            auto *tile=new QFrame;tile->setObjectName(tileName);
            auto *layout=new QVBoxLayout(tile);layout->setContentsMargins(10,9,10,10);layout->setSpacing(7);
            auto *top=new QHBoxLayout;
            auto *label=new QLabel(title);label->setStyleSheet("font-size:13px;font-weight:600;color:#e4edf7;");
            top->addWidget(label);top->addStretch();
            auto *badge=new QLabel(tag);badge->setStyleSheet("font-size:10px;color:#8199b2;");top->addWidget(badge);
            layout->addLayout(top);
            auto *image=new ImagePanel;image->setObjectName(imageName);layout->addWidget(image,1);
            leftLayout->addWidget(tile,1);return image;
        };
        camera_=imageTile(QStringLiteral("相机视图"),QStringLiteral("保存图像 · 同步回放"),"sensorCameraTile","cameraImage");
        radar_=imageTile(QStringLiteral("雷达视图"),QStringLiteral("保存扫描 · 同步回放"),"sensorRadarTile","radarImage");
        split->addWidget(left);

        auto *right=new QWidget;
        auto *rightLayout=new QVBoxLayout(right);
        rightLayout->setContentsMargins(0,0,0,0);
        rightLayout->setSpacing(8);
        auto *trackCard=new QFrame;trackCard->setObjectName("trajectoryCard");
        auto *trackLayout=new QVBoxLayout(trackCard);trackLayout->setContentsMargins(10,9,10,8);trackLayout->setSpacing(7);
        auto *trackTop=new QHBoxLayout;
        auto *trackTitle=new QLabel(QStringLiteral("轨迹对比"));trackTitle->setStyleSheet("font-size:13px;font-weight:600;color:#e4edf7;");
        trackTop->addWidget(trackTitle);
        auto *trackHint=new QLabel(QStringLiteral("完整路线 · 箭头表示当前朝向"));trackHint->setStyleSheet("font-size:10px;color:#839ab3;");
        trackHint->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
        trackTop->addWidget(trackHint,1);
        auto *follow=new QCheckBox(QStringLiteral("跟随参考位置 · 128 m"));follow->setObjectName("followCheck");trackTop->addWidget(follow);
        trackLayout->addLayout(trackTop);
        auto *legend=new QGridLayout;legend->setHorizontalSpacing(8);legend->setVerticalSpacing(3);
        const auto names=radar::methodNames();
        const QStringList shortNames{QStringLiteral("参考位置"),QStringLiteral("雷达主导"),QStringLiteral("多帧估计"),QStringLiteral("单帧估计"),QStringLiteral("图像主导"),QStringLiteral("仅图像"),QStringLiteral("仅雷达")};
        for(int i=0;i<7;++i) {
            auto *check=new QCheckBox(shortNames[i]);check->setObjectName(QStringLiteral("methodCheck%1").arg(i));check->setChecked(true);
            check->setToolTip(names.value(i));check->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(colors[i].name()));
            legend->addWidget(check,i/4,i%4);
            connect(check,&QCheckBox::toggled,this,[this,i](bool on){trajectory_->method(i,on);});
        }
        trackLayout->addLayout(legend);
        trajectory_=new TrajectoryPanel;trajectory_->setObjectName("trajectoryPanel");trackLayout->addWidget(trajectory_,1);
        rightLayout->addWidget(trackCard,1);
        connect(follow,&QCheckBox::toggled,this,[this](bool on){trajectory_->follow(on);});

        inspector_=new QTabWidget;inspector_->setObjectName("inspectorTabs");inspector_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        posePage_=new QWidget;posePage_->setObjectName("poseGroup");
        auto *poseLayout=new QVBoxLayout(posePage_);poseLayout->setContentsMargins(6,6,6,6);poseLayout->setSpacing(6);
        table_=new QTableWidget(7,7);table_->setObjectName("poseTable");
        table_->setHorizontalHeaderLabels({QStringLiteral("方法"),QStringLiteral("北 / m"),QStringLiteral("东 / m"),QStringLiteral("下 / m"),QStringLiteral("航向 / °"),QStringLiteral("平面误差 / m"),QStringLiteral("航向误差 / °")});
        table_->verticalHeader()->hide();table_->verticalHeader()->setDefaultSectionSize(24);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setAlternatingRowColors(true);
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
        for(int row=0;row<7;++row)for(int col=0;col<7;++col) {
            auto *item=new QTableWidgetItem(col==0?names.value(row):QStringLiteral("—"));item->setForeground(colors[row]);
            if(col)item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);table_->setItem(row,col,item);
        }
        poseLayout->addWidget(table_);
        vo_=new QLabel(QStringLiteral("图像主导估计：等待帧数据"));vo_->setObjectName("voAcceptedLabel");poseLayout->addWidget(vo_);
        inspector_->addTab(posePage_,QStringLiteral("位置与姿态"));
        auto *errorPage=new QWidget;errorPage->setObjectName("errorPage");auto *errorLayout=new QVBoxLayout(errorPage);errorLayout->setContentsMargins(5,4,5,4);
        errors_=new ErrorPanel;errors_->setObjectName("errorPanel");errorLayout->addWidget(errors_,1);
        inspector_->addTab(errorPage,QStringLiteral("误差趋势"));
        rightLayout->addWidget(inspector_);
        split->addWidget(right);split->setStretchFactor(0,3);split->setStretchFactor(1,7);split->setSizes({450,1000});

        auto *dock=new QFrame;dock->setObjectName("playbackDock");auto *dockLayout=new QVBoxLayout(dock);dockLayout->setContentsMargins(12,9,12,9);dockLayout->setSpacing(7);
        auto *timeline=new QHBoxLayout;timeline->setSpacing(10);
        auto *progress=new QLabel(QStringLiteral("回放进度"));progress->setStyleSheet("color:#91aac4;font-size:11px;");timeline->addWidget(progress);
        slider_=new QSlider(Qt::Horizontal);slider_->setObjectName("frameSlider");slider_->setRange(0,0);slider_->setTracking(false);timeline->addWidget(slider_,1);
        timeline->addWidget(new QLabel(QStringLiteral("帧")));
        spin_=new QSpinBox;spin_->setObjectName("frameSpin");spin_->setRange(0,0);spin_->setMinimumWidth(82);timeline->addWidget(spin_);
        count_=new QLabel(QStringLiteral("/ —"));count_->setObjectName("frameCountLabel");count_->setStyleSheet("color:#a5bad0;font-size:11px;");timeline->addWidget(count_);
        dockLayout->addLayout(timeline);
        auto *transport=new QHBoxLayout;transport->setSpacing(8);
        prev_=makeButton(QStringLiteral("上一帧"),"previousButton");play_=makeButton(QStringLiteral("播放"),"playButton");next_=makeButton(QStringLiteral("下一帧"),"nextButton");
        play_->setMinimumWidth(110);play_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        play_->setToolTip(QStringLiteral("空格播放 / 暂停；输入框和选择控件保留原有键盘操作。"));
        transport->addWidget(prev_);transport->addWidget(play_);transport->addWidget(next_);transport->addSpacing(6);
        auto *speedLabel=new QLabel(QStringLiteral("速度"));speedLabel->setStyleSheet("color:#91aac4;");transport->addWidget(speedLabel);
        speed_=new QComboBox;speed_->setObjectName("speedCombo");for(double speed:{.25,.5,1.,2.,4.,8.,16.})speed_->addItem(QString::number(speed)+QStringLiteral(" ×"),speed);speed_->setCurrentIndex(2);speed_->setMinimumWidth(72);transport->addWidget(speed_);
        transport->addStretch();
        summary_=new QLabel(QStringLiteral("等待帧数据 · 已用 — · 同步差 —"));summary_->setObjectName("playbackSummary");summary_->setStyleSheet("color:#b9cbdd;font-size:11px;");transport->addWidget(summary_);
        auto *details=makeButton(QStringLiteral("同步详情"),"syncDetailsButton");transport->addWidget(details);
        timestamp_=new QLabel(QStringLiteral("雷达时间戳 —   |   相机时间戳 —   |   已用时间 —   |   同步差 —"),this);timestamp_->setObjectName("timestampLabel");timestamp_->hide();
        dockLayout->addLayout(transport);root->addWidget(dock);

        auto *footer=new QHBoxLayout;footer->setSpacing(10);
        status_=new QLabel(QStringLiteral("就绪 · 打开数据包开始回放"));status_->setObjectName("statusLabel");status_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);footer->addWidget(status_,1);
        auto *license=new QLabel(QStringLiteral("Oxford Radar RobotCar · CC BY-NC-SA 4.0 · <a style=\"color:#87b9c7\" href=\"https://oxford-robotics-institute.github.io/radar-robotcar-dataset/\">数据集与署名</a> · <a style=\"color:#87b9c7\" href=\"https://creativecommons.org/licenses/by-nc-sa/4.0/\">许可条款</a>"));license->setObjectName("licenseLabel");license->setTextInteractionFlags(Qt::TextBrowserInteraction);license->setOpenExternalLinks(true);footer->addWidget(license);root->addLayout(footer);

        connect(open,&QPushButton::clicked,this,[this]{const auto directory=QFileDialog::getExistingDirectory(this,QStringLiteral("打开保存结果数据包"),dataset_.root);if(!directory.isEmpty())emit openRequested(directory);});
        connect(play_,&QPushButton::clicked,this,&radar::Frontend::toggleRequested);
        connect(prev_,&QPushButton::clicked,this,[this]{emit stepRequested(-1);});connect(next_,&QPushButton::clicked,this,[this]{emit stepRequested(1);});
        connect(slider_,&QSlider::valueChanged,this,[this](int index){QSignalBlocker block(spin_);spin_->setValue(index);emit seekRequested(index);});
        connect(spin_,qOverload<int>(&QSpinBox::valueChanged),this,[this](int index){QSignalBlocker block(slider_);slider_->setValue(index);emit seekRequested(index);});
        connect(speed_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){emit speedRequested(speed_->itemData(index).toDouble());});
        connect(export_,&QPushButton::clicked,this,[this]{exportDialog();});
        connect(details,&QPushButton::clicked,this,[this]{QDialog dialog(this);dialog.setObjectName("syncDetailsDialog");dialog.setWindowTitle(QStringLiteral("同步详情 · 保存的时间戳"));auto *layout=new QVBoxLayout(&dialog);auto *label=new QLabel(timestamp_->text());label->setObjectName("syncDetailsText");label->setWordWrap(true);label->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(label);auto *close=new QDialogButtonBox(QDialogButtonBox::Close);layout->addWidget(close);connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);dialog.resize(600,140);dialog.exec();});
        connect(inspector_,&QTabWidget::currentChanged,this,[this](int){fitPoseTable();});
        enabled(false);fitPoseTable();qApp->installEventFilter(this);
    }
    ~Dashboard() override {if(qApp)qApp->removeEventFilter(this);}
    void setDataset(radar::Dataset dataset) override {
        assertGui();dataset_=std::move(dataset);const int last=std::max(0,dataset_.frameCount-1);{QSignalBlocker a(slider_),b(spin_);slider_->setRange(0,last);spin_->setRange(0,last);slider_->setValue(0);spin_->setValue(0);}
        count_->setText(QStringLiteral("/ %1 · 共 %2 帧").arg(last).arg(dataset_.frameCount));datasetLabel_->setText(QStringLiteral("%1 · 共 %2 帧").arg(dataset_.sequence).arg(dataset_.frameCount));datasetLabel_->setToolTip(dataset_.sequence+QStringLiteral("\n")+dataset_.root);trajectory_->dataset(dataset_);errors_->reset();camera_->setImage({});radar_->setImage({});
        for(int row=0;row<7;++row)for(int col=1;col<7;++col)table_->item(row,col)->setText(QStringLiteral("—"));vo_->setText(QStringLiteral("图像主导估计：等待帧数据"));vo_->setProperty("accepted",false);timestamp_->setText(QStringLiteral("雷达时间戳 —   |   相机时间戳 —   |   已用时间 —   |   同步差 —"));summary_->setText(QStringLiteral("等待帧数据 · 已用 — · 同步差 —"));summary_->setToolTip(timestamp_->text());play_->setText(QStringLiteral("播放"));play_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));play_->setProperty("playing",false);enabled(dataset_.frameCount>0);showStatus(QStringLiteral("数据包已加载 · %1 帧").arg(dataset_.frameCount));
    }
    void showFrame(radar::Frame frame) override {
        assertGui();if(frame.index<0||frame.index>=dataset_.frameCount)return;
        {QSignalBlocker a(slider_),b(spin_);slider_->setValue(frame.index);spin_->setValue(frame.index);}
        camera_->setImage(frame.stereoImage);radar_->setImage(frame.radarImage);trajectory_->frame(frame);errors_->frame(frame);
        timestamp_->setText(QStringLiteral("帧 %1   |   雷达 %2 µs   |   相机 %3 µs   |   已用 %4 s   |   同步差（相机 − 雷达）%5 ms").arg(frame.index).arg(frame.radarTimestamp).arg(frame.stereoTimestamp).arg(QString::number((frame.radarTimestamp-dataset_.firstTimestamp)/1e6,'f',3)).arg(QString::number((frame.stereoTimestamp-frame.radarTimestamp)/1000.,'f',3)));
        summary_->setText(QStringLiteral("当前帧 %1 · 已用 %2 s · 同步差 %3 ms").arg(frame.index).arg(QString::number((frame.radarTimestamp-dataset_.firstTimestamp)/1e6,'f',3)).arg(QString::number((frame.stereoTimestamp-frame.radarTimestamp)/1000.,'f',3)));summary_->setToolTip(timestamp_->text());
        for(int i=0;i<7;++i) {const auto &pose=frame.poses[i];const QStringList values{number(pose.north),number(pose.east),number(pose.down),number(pose.yaw),i==0?QStringLiteral("参考"):number(radar::planarError(frame,i)),i==0?QStringLiteral("参考"):number(radar::yawError(frame,i))};for(int j=0;j<6;++j)table_->item(i,j+1)->setText(values[j]);}
        vo_->setText(frame.voModelAccepted?QStringLiteral("图像主导估计：已采用模型结果"):QStringLiteral("图像主导估计：未采用模型结果"));vo_->setProperty("accepted",frame.voModelAccepted);
    }
    void setPlaybackState(bool playing,int index,double speed) override {
        assertGui();play_->setText(playing?QStringLiteral("暂停"):QStringLiteral("播放"));play_->setIcon(style()->standardIcon(playing?QStyle::SP_MediaPause:QStyle::SP_MediaPlay));play_->setProperty("playing",playing);
        QSignalBlocker a(slider_),b(spin_),c(speed_);slider_->setValue(index);spin_->setValue(index);for(int i=0;i<speed_->count();++i)if(qFuzzyCompare(speed_->itemData(i).toDouble(),speed))speed_->setCurrentIndex(i);
    }
    void showStatus(QString detail) override {assertGui();detail.replace('\n',' ');status_->setText(detail.left(600));status_->setToolTip(detail.left(1200));}
    void exportCompleted(QString path,int rows) override {showStatus(QStringLiteral("CSV 导出完成 · %1 行 · %2").arg(rows).arg(path));}
protected:
    void showEvent(QShowEvent *event) override {QWidget::showEvent(event);fitPoseTable();}
    void resizeEvent(QResizeEvent *event) override {QWidget::resizeEvent(event);fitPoseTable();}
    bool eventFilter(QObject *watched,QEvent *event) override {
        if(table_ && (event->type()==QEvent::LayoutRequest && (watched==table_||watched==posePage_||watched==inspector_))) fitPoseTable();
        if(table_ && watched==table_->horizontalScrollBar() && (event->type()==QEvent::Show||event->type()==QEvent::Hide||event->type()==QEvent::Resize)) fitPoseTable();
        if(event->type()!=QEvent::KeyPress||!play_||!play_->isEnabled()||!isVisible()||QApplication::activeModalWidget()||QApplication::activePopupWidget()) return QWidget::eventFilter(watched,event);
        const auto *key=static_cast<QKeyEvent*>(event);
        if(key->key()!=Qt::Key_Space||key->modifiers()!=Qt::NoModifier) return QWidget::eventFilter(watched,event);
        auto *target=qobject_cast<QWidget*>(watched);
        if(!target||(target!=this&&!isAncestorOf(target))) return QWidget::eventFilter(watched,event);
        QWidget *focus=qApp->focusWidget();if(!focus||(!isAncestorOf(focus)&&focus!=this))focus=target;
        for(QWidget *control=focus;control&&control!=this;control=control->parentWidget()) {
            if(qobject_cast<QLineEdit*>(control)||qobject_cast<QAbstractSpinBox*>(control)||qobject_cast<QComboBox*>(control)||qobject_cast<QAbstractButton*>(control)||qobject_cast<QAbstractItemView*>(control)||qobject_cast<QTabBar*>(control)) return QWidget::eventFilter(watched,event);
        }
        if(!key->isAutoRepeat())emit toggleRequested();return true;
    }
private:
    void fitPoseTable() {
        if(!table_||!inspector_||!posePage_||!vo_||fittingTable_)return;
        QScopedValueRollback<bool> fitting(fittingTable_,true);
        table_->ensurePolished();table_->horizontalHeader()->ensurePolished();table_->verticalHeader()->ensurePolished();
        int rows=0;for(int row=0;row<table_->rowCount();++row)rows+=table_->rowHeight(row);
        const int header=std::max(table_->horizontalHeader()->height(),table_->horizontalHeader()->sizeHint().height());
        const int horizontal=table_->horizontalScrollBar()->maximum()>0&&table_->horizontalScrollBarPolicy()!=Qt::ScrollBarAlwaysOff?table_->horizontalScrollBar()->sizeHint().height():0;
        const int height=header+rows+2*table_->frameWidth()+horizontal+2;
        if(table_->minimumHeight()!=height||table_->maximumHeight()!=height)table_->setFixedHeight(height);
        const auto margins=posePage_->layout()->contentsMargins();
        const int pageHeight=height+vo_->sizeHint().height()+posePage_->layout()->spacing()+margins.top()+margins.bottom();
        inspector_->tabBar()->ensurePolished();const int tabs=std::max(inspector_->tabBar()->height(),inspector_->tabBar()->sizeHint().height());
        const int inspectorHeight=pageHeight+tabs+8;
        if(inspector_->minimumHeight()!=inspectorHeight||inspector_->maximumHeight()!=inspectorHeight)inspector_->setFixedHeight(inspectorHeight);
    }
    void enabled(bool on) {const std::array<QWidget*,7> widgets{{prev_,play_,next_,export_,slider_,spin_,speed_}};for(QWidget *w:widgets)w->setEnabled(on);}
    void exportDialog() {
        QDialog dialog(this);dialog.setObjectName("exportDialog");dialog.setWindowTitle(QStringLiteral("导出 CSV · 包含起止帧"));auto *layout=new QVBoxLayout(&dialog);layout->addWidget(new QLabel(QStringLiteral("七种方法的保存姿态；起止帧均包含在导出结果中。")));
        auto *path=new QLineEdit(QDir(dataset_.root).filePath("radar-replay.csv"));path->setObjectName("exportPath");auto *pathRow=new QHBoxLayout;pathRow->addWidget(path,1);auto *browse=new QPushButton(QStringLiteral("选择文件…"));browse->setObjectName("exportBrowse");pathRow->addWidget(browse);layout->addLayout(pathRow);connect(browse,&QPushButton::clicked,&dialog,[this,path]{const QString file=QFileDialog::getSaveFileName(this,QStringLiteral("导出保存结果 CSV"),path->text(),QStringLiteral("CSV (*.csv)"));if(!file.isEmpty())path->setText(file);});
        auto *range=new QHBoxLayout;auto *first=new QSpinBox;auto *last=new QSpinBox;first->setObjectName("exportFirst");last->setObjectName("exportLast");first->setRange(0,dataset_.frameCount-1);last->setRange(0,dataset_.frameCount-1);last->setValue(dataset_.frameCount-1);range->addWidget(new QLabel(QStringLiteral("起始帧")));range->addWidget(first);range->addWidget(new QLabel(QStringLiteral("结束帧")));range->addWidget(last);layout->addLayout(range);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);buttons->setObjectName("exportButtons");buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("导出"));buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(first,qOverload<int>(&QSpinBox::valueChanged),&dialog,[last](int value){last->setMinimum(value);});connect(last,qOverload<int>(&QSpinBox::valueChanged),&dialog,[first](int value){first->setMaximum(value);});
        dialog.resize(560,170);if(dialog.exec()==QDialog::Accepted&&!path->text().trimmed().isEmpty())emit exportRequested(path->text().trimmed(),first->value(),last->value());
    }
    radar::Dataset dataset_;QLabel *datasetLabel_=nullptr,*count_=nullptr,*timestamp_=nullptr,*summary_=nullptr,*vo_=nullptr,*status_=nullptr;QPushButton *prev_=nullptr,*play_=nullptr,*next_=nullptr,*export_=nullptr;QSlider *slider_=nullptr;QSpinBox *spin_=nullptr;QComboBox *speed_=nullptr;ImagePanel *camera_=nullptr,*radar_=nullptr;TrajectoryPanel *trajectory_=nullptr;ErrorPanel *errors_=nullptr;QTableWidget *table_=nullptr;QTabWidget *inspector_=nullptr;QWidget *posePage_=nullptr;bool fittingTable_=false;
};
}
extern "C" RADAR_FRONTEND_API radar::Frontend *radar_create_frontend(QWidget *parent) {assertGui();return new Dashboard(parent);}
