#include <radar/contracts.h>
#include <QtTest/QtTest>
#include <limits>
#include <cmath>
class ContractsTest:public QObject {
    Q_OBJECT
private slots:
    void roundTrip(){radar::Frame frame;frame.index=7202;frame.radarTimestamp=1547559404102392LL;frame.stereoTimestamp=1547559404126575LL;frame.voModelAccepted=true;
        for(int method=0;method<7;++method)frame.poses[method]={5735999.86103+method,619889.885788-method,-113.949849,153.471694};
        radar::Frame parsed;QString error;QVERIFY(radar::parseFrame(radar::frameToJson(frame),parsed,error));QCOMPARE(parsed.index,frame.index);QCOMPARE(parsed.radarTimestamp,frame.radarTimestamp);QCOMPARE(parsed.stereoTimestamp,frame.stereoTimestamp);QCOMPARE(parsed.poses[6].east,frame.poses[6].east);QVERIFY(parsed.voModelAccepted);
    }
    void rejectsInvalidAtomically(){radar::Frame original;original.index=42;radar::Frame valid;valid.index=1;valid.radarTimestamp=1;valid.stereoTimestamp=2;QString error;
        auto row=radar::frameToJson(valid);row[0]=1.5;QVERIFY(!radar::parseFrame(row,original,error));QCOMPARE(original.index,42);
        row=radar::frameToJson(valid);row[1]="1547557604078984";QVERIFY(!radar::parseFrame(row,original,error));
        row=radar::frameToJson(valid);row[30]=2;QVERIFY(!radar::parseFrame(row,original,error));
        row=radar::frameToJson(valid);row[7]=QJsonValue::Null;QVERIFY(!radar::parseFrame(row,original,error));
        row=radar::frameToJson(valid);row[31]=9007199254740992.0;QVERIFY(!radar::parseFrame(row,original,error));
    }
    void functionalErrors(){radar::Frame frame;frame.poses[0]={10,20,30,179};frame.poses[1]={13,24,99,-179};QCOMPARE(radar::planarError(frame,1),5.0);QCOMPARE(radar::yawError(frame,1),2.0);QCOMPARE(radar::methodNames().size(),7);QVERIFY(std::isnan(radar::planarError(frame,7)));}
};
QTEST_GUILESS_MAIN(ContractsTest)
#include "contracts_test.moc"
