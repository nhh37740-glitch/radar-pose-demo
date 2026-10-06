#include <radar/contracts.h>
#include <QtCore/QTimer>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <limits>
#include <memory>

namespace {
radar::Frame frame(int index, qint64 timestamp = -1) {
    radar::Frame value;
    value.index = index;
    value.radarTimestamp = timestamp < 0 ? index * 20000 : timestamp;
    return value;
}
radar::Dataset dataset(int count = 4, qint64 intervalUs = 20000, bool overview = true) {
    radar::Dataset value;
    value.frameCount = count;
    value.firstTimestamp = 0;
    value.lastTimestamp = (count - 1) * intervalUs;
    if (overview)
        for (int index = 0; index < count; ++index) value.overview.append(frame(index, index * intervalUs));
    return value;
}
quint64 id(const QSignalSpy &spy, int row = -1) {
    return spy.at(row < 0 ? spy.count() - 1 : row).at(0).toULongLong();
}
}

class PlaybackTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { radar::registerTypes(); }

    void rapidSeekAndStaleError() {
        std::unique_ptr<radar::Playback> playback(radar_create_playback(nullptr));
        QSignalSpy requests(playback.get(), &radar::Playback::requestFrame);
        QSignalSpy shown(playback.get(), &radar::Playback::showFrame);
        QSignalSpy errors(playback.get(), &radar::Playback::error);
        playback->setDataset(dataset());
        const auto initial = id(requests);
        playback->seek(1);
        const auto old = id(requests);
        playback->seek(3);
        const auto latest = id(requests);
        QVERIFY(initial < old && old < latest);
        playback->acceptFrame(initial, frame(0));
        playback->acceptFrame(old, frame(1));
        playback->acceptError(old, QStringLiteral("obsolete"));
        QCOMPARE(shown.count(), 0);
        QCOMPARE(errors.count(), 0);
        playback->acceptFrame(latest, frame(3));
        QCOMPARE(shown.count(), 1);
        QCOMPARE(qvariant_cast<radar::Frame>(shown.last().at(0)).index, 3);
        playback->acceptFrame(latest, frame(3));
        QCOMPARE(shown.count(), 1);
    }

    void pauseAndBoundedRequests() {
        std::unique_ptr<radar::Playback> playback(radar_create_playback(nullptr));
        QSignalSpy requests(playback.get(), &radar::Playback::requestFrame);
        QSignalSpy shown(playback.get(), &radar::Playback::showFrame);
        playback->setDataset(dataset());
        playback->play();
        QTest::qWait(80);
        QCOMPARE(requests.count(), 1); // Waiting for frame zero never polls the reader.
        playback->acceptFrame(id(requests), frame(0));
        QTRY_COMPARE_WITH_TIMEOUT(requests.count(), 2, 500);
        QTest::qWait(80);
        QCOMPARE(requests.count(), 2); // A pending automatic read holds advancement.
        playback->pause();
        playback->acceptFrame(id(requests), frame(1));
        QTest::qWait(80);
        QCOMPARE(shown.count(), 2);
        QCOMPARE(requests.count(), 2);
        QVERIFY(!playback->findChild<QTimer *>()->isActive());
        playback->play();
        QTRY_COMPARE_WITH_TIMEOUT(requests.count(), 3, 500);
        playback->acceptFrame(id(requests), frame(2));
        playback->pause(); // Also stop a timer which is already scheduled.
        QTest::qWait(80);
        QCOMPARE(requests.count(), 3);
    }

    void recordedTimingAndSpeed() {
        std::unique_ptr<radar::Playback> playback(radar_create_playback(nullptr));
        QSignalSpy requests(playback.get(), &radar::Playback::requestFrame);
        auto value = dataset(3, 500000);
        value.overview[1].radarTimestamp = 200000;
        playback->setDataset(value);
        playback->acceptFrame(id(requests), frame(0));
        playback->play();
        auto timer = playback->findChild<QTimer *>();
        QVERIFY(timer && timer->isSingleShot());
        QVERIFY(timer->interval() >= 195 && timer->interval() <= 200);
        playback->setSpeed(4);
        QVERIFY(timer->interval() >= 45 && timer->interval() <= 50);
        playback->setSpeed(0.25);
        QVERIFY(timer->interval() >= 795 && timer->interval() <= 800);
        playback->pause();
        playback->setDataset(dataset(3, 300000, false));
        playback->acceptFrame(id(requests), frame(0));
        playback->setSpeed(1);
        playback->play();
        QVERIFY(timer->interval() >= 295 && timer->interval() <= 300); // Recorded mean spacing.
        playback->seek(1);
        playback->acceptFrame(id(requests), frame(1, 120000));
        QVERIFY(timer->interval() >= 115 && timer->interval() <= 120); // Actual neighboring spacing.
    }

    void stepEndAndRestart() {
        std::unique_ptr<radar::Playback> playback(radar_create_playback(nullptr));
        QSignalSpy requests(playback.get(), &radar::Playback::requestFrame);
        QSignalSpy finished(playback.get(), &radar::Playback::finished);
        QSignalSpy states(playback.get(), &radar::Playback::stateChanged);
        playback->setDataset(dataset(3));
        playback->acceptFrame(id(requests), frame(0));
        playback->step(std::numeric_limits<int>::min());
        QCOMPARE(requests.last().at(1).toInt(), 0);
        playback->acceptFrame(id(requests), frame(0));
        playback->step(std::numeric_limits<int>::max());
        QCOMPARE(requests.last().at(1).toInt(), 2);
        playback->acceptFrame(id(requests), frame(2));
        playback->play();
        QCOMPARE(requests.last().at(1).toInt(), 0); // Play at end restarts the sequence.
        playback->acceptFrame(id(requests), frame(0));
        QTRY_COMPARE_WITH_TIMEOUT(requests.last().at(1).toInt(), 1, 500);
        playback->acceptFrame(id(requests), frame(1));
        QTRY_COMPARE_WITH_TIMEOUT(requests.last().at(1).toInt(), 2, 500);
        const auto endId = id(requests);
        playback->acceptFrame(endId, frame(2));
        QCOMPARE(finished.count(), 1);
        QVERIFY(!states.last().at(0).toBool());
        playback->acceptFrame(endId, frame(2));
        QTest::qWait(60);
        QCOMPARE(finished.count(), 1);
        playback->play();
        QCOMPARE(requests.last().at(1).toInt(), 0);
        QVERIFY(id(requests) > endId);
    }

    void invalidControlsAndWrongResult() {
        std::unique_ptr<radar::Playback> playback(radar_create_playback(nullptr));
        QSignalSpy requests(playback.get(), &radar::Playback::requestFrame);
        QSignalSpy errors(playback.get(), &radar::Playback::error);
        QSignalSpy shown(playback.get(), &radar::Playback::showFrame);
        QSignalSpy states(playback.get(), &radar::Playback::stateChanged);
        playback->play();
        playback->seek(0);
        playback->step(1);
        playback->pause();
        playback->setSpeed(2);
        QCOMPARE(errors.count(), 5);
        QCOMPARE(requests.count(), 0);
        playback->setDataset(dataset());
        const auto initial = id(requests);
        const double invalid[] = {0, -1, 0.24, 16.1, std::numeric_limits<double>::quiet_NaN(),
                                  std::numeric_limits<double>::infinity()};
        for (double speed : invalid) playback->setSpeed(speed);
        playback->seek(-1);
        playback->seek(4);
        QCOMPARE(errors.count(), 13);
        QCOMPARE(requests.count(), 1);
        QCOMPARE(states.last().at(2).toDouble(), 1.0);
        playback->setSpeed(16);
        QCOMPARE(states.last().at(2).toDouble(), 16.0);
        playback->play();
        playback->acceptFrame(initial, frame(1));
        QCOMPARE(shown.count(), 0);
        QCOMPARE(errors.count(), 14);
        QVERIFY(!states.last().at(0).toBool());
        playback->acceptFrame(initial, frame(0));
        QCOMPARE(shown.count(), 0);
        playback->play(); // Failed frame is retried with a fresh request.
        QCOMPARE(requests.count(), 2);
        QVERIFY(id(requests) > initial);
    }

    void datasetResetErrorAndShutdown() {
        std::unique_ptr<radar::Playback> playback(radar_create_playback(nullptr));
        QSignalSpy requests(playback.get(), &radar::Playback::requestFrame);
        QSignalSpy errors(playback.get(), &radar::Playback::error);
        QSignalSpy shown(playback.get(), &radar::Playback::showFrame);
        playback->setDataset(dataset());
        const auto old = id(requests);
        playback->play();
        playback->setDataset(dataset(2));
        const auto current = id(requests);
        QVERIFY(current > old);
        playback->acceptFrame(old, frame(0));
        playback->acceptError(old, QStringLiteral("old dataset"));
        QCOMPARE(shown.count(), 0);
        QCOMPARE(errors.count(), 0);
        playback->play();
        playback->acceptError(current, QStringLiteral("read failed"));
        QCOMPARE(errors.count(), 1);
        QVERIFY(!playback->findChild<QTimer *>()->isActive());
        QTest::qWait(60);
        QCOMPARE(requests.count(), 2);
        playback->seek(1);
        const auto shutdownId = id(requests);
        playback->play();
        playback->shutdown();
        playback->acceptFrame(shutdownId, frame(1));
        playback->acceptError(shutdownId, QStringLiteral("late"));
        playback->play();
        playback->seek(0);
        playback->setDataset(dataset());
        QTest::qWait(60);
        QCOMPARE(requests.count(), 4); // Play at last requested frame zero before shutdown.
        QCOMPARE(shown.count(), 0);
        QCOMPARE(errors.count(), 1);
    }

    void singleFrameAndEmptyDataset() {
        std::unique_ptr<radar::Playback> playback(radar_create_playback(nullptr));
        QSignalSpy requests(playback.get(), &radar::Playback::requestFrame);
        QSignalSpy finished(playback.get(), &radar::Playback::finished);
        QSignalSpy errors(playback.get(), &radar::Playback::error);
        QSignalSpy shown(playback.get(), &radar::Playback::showFrame);
        playback->setDataset(dataset(1));
        playback->play();
        playback->acceptFrame(id(requests), frame(0));
        QCOMPARE(finished.count(), 1);
        const auto old = id(requests);
        playback->setDataset(dataset(0));
        QCOMPARE(errors.count(), 1);
        playback->acceptFrame(old, frame(0));
        QCOMPARE(shown.count(), 1);
        playback->play();
        QCOMPARE(errors.count(), 2);
    }

    void ownerThreadAndShutdownStopsTimer() {
        QObject owner;
        auto playback = radar_create_playback(&owner);
        QCOMPARE(playback->parent(), &owner);
        QCOMPARE(playback->thread(), owner.thread());
        auto timer = playback->findChild<QTimer *>();
        QVERIFY(timer);
        QCOMPARE(timer->thread(), owner.thread());
        QSignalSpy requests(playback, &radar::Playback::requestFrame);
        playback->setDataset(dataset());
        playback->acceptFrame(id(requests), frame(0));
        playback->play();
        QVERIFY(timer->isActive());
        playback->shutdown();
        QVERIFY(!timer->isActive());
        QTest::qWait(80);
        QCOMPARE(requests.count(), 1);
    }

    void lateReadKeepsRecordedDeadline() {
        std::unique_ptr<radar::Playback> playback(radar_create_playback(nullptr));
        QSignalSpy requests(playback.get(), &radar::Playback::requestFrame);
        playback->setDataset(dataset(4, 20000));
        playback->acceptFrame(id(requests), frame(0));
        playback->play();
        QTRY_COMPARE_WITH_TIMEOUT(requests.count(), 2, 500);
        QTest::qWait(80); // Simulate a read slower than the recorded frame interval.
        QCOMPARE(requests.count(), 2);
        playback->acceptFrame(id(requests), frame(1));
        QCOMPARE(playback->findChild<QTimer *>()->interval(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(requests.count(), 3, 500);
        playback->pause();
    }
};

QTEST_GUILESS_MAIN(PlaybackTest)
#include "playback_test.moc"
