#include <radar/contracts.h>
#include <QtCore/QJsonValue>
#include <cmath>
#include <limits>

namespace radar {
void registerTypes(){qRegisterMetaType<Frame>();qRegisterMetaType<Dataset>();}
bool parseFrame(const QJsonArray &row,Frame &frame,QString &error){
    if(row.size()!=32){error="Expected 32 numeric fields";return false;}
    for(const auto &value:row){
        if(!value.isDouble()||!std::isfinite(value.toDouble())){error="Frame fields must be finite numbers";return false;}
    }
    auto integer=[&](int field,double minimum,double maximum){
        const double value=row[field].toDouble();return value>=minimum&&value<=maximum&&std::floor(value)==value;
    };
    if(!integer(0,0,std::numeric_limits<int>::max())||!integer(1,1,9007199254740991.0)||!integer(31,1,9007199254740991.0)||!integer(30,0,1)){
        error="Invalid frame index, timestamps, or acceptance flag";return false;
    }
    Frame parsed;parsed.index=row[0].toInt();parsed.radarTimestamp=qint64(row[1].toDouble());parsed.stereoTimestamp=qint64(row[31].toDouble());
    parsed.voModelAccepted=row[30].toInt()==1;
    for(int method=0;method<7;++method){const int offset=2+4*method;parsed.poses[method]={row[offset].toDouble(),row[offset+1].toDouble(),row[offset+2].toDouble(),row[offset+3].toDouble()};}
    frame=parsed;error.clear();return true;
}
QJsonArray frameToJson(const Frame &frame){
    QJsonArray row;row.append(frame.index);row.append(double(frame.radarTimestamp));
    for(const auto &pose:frame.poses){row.append(pose.north);row.append(pose.east);row.append(pose.down);row.append(pose.yaw);}
    row.append(frame.voModelAccepted?1:0);row.append(double(frame.stereoTimestamp));return row;
}
QStringList methodNames(){return {QStringLiteral("参考位置"),QStringLiteral("雷达主导估计"),QStringLiteral("多帧估计"),QStringLiteral("单帧估计"),QStringLiteral("图像主导估计"),QStringLiteral("仅图像里程计"),QStringLiteral("仅雷达里程计")};}
double planarError(const Frame &frame,int method){
    if(method<0||method>=7)return std::numeric_limits<double>::quiet_NaN();
    return std::hypot(frame.poses[method].north-frame.poses[0].north,frame.poses[method].east-frame.poses[0].east);
}
double yawError(const Frame &frame,int method){
    if(method<0||method>=7)return std::numeric_limits<double>::quiet_NaN();
    return std::abs(std::remainder(frame.poses[method].yaw-frame.poses[0].yaw,360.0));
}
}
