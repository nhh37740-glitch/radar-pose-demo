#include <radar/contracts.h>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace {
class RecordedPlayback final : public radar::Playback {
public:
    explicit RecordedPlayback(QObject *parent) : Playback(parent), timer_(new QTimer(this)) {
        clock_.start();
        timer_->setSingleShot(true);
        timer_->setTimerType(Qt::PreciseTimer);
        connect(timer_, &QTimer::timeout, this, [this] {
            if (!playing_ || pending_ || stopped_) return;
            if (index_ + 1 >= dataset_.frameCount) finish();
            else {
                currentDeadlineMs_ = nextDeadlineMs_;
                request(index_ + 1, true);
            }
        });
    }

    void setDataset(radar::Dataset dataset) override {
        if (stopped_) return;
        timer_->stop();
        playing_ = false;
        pending_ = false;
        loaded_ = false;
        endNotified_ = false;
        timelineActive_ = false;
        index_ = -1;
        timestamps_.clear();
        dataset_ = std::move(dataset);
        observedIntervalUs_ = 0;
        if (dataset_.frameCount <= 0) {
            state();
            emit error(QStringLiteral("Cannot replay a dataset with no frames."));
            return;
        }
        meanIntervalUs_ = 100000.0;
        if (dataset_.frameCount > 1 && dataset_.lastTimestamp > dataset_.firstTimestamp)
            meanIntervalUs_ = (static_cast<long double>(dataset_.lastTimestamp)
                             - dataset_.firstTimestamp) / (dataset_.frameCount - 1);
        for (const auto &frame : dataset_.overview)
            if (frame.index >= 0 && frame.index < dataset_.frameCount)
                timestamps_[frame.index] = frame.radarTimestamp;
        request(0);
    }

    void play() override {
        if (!ready()) return;
        if (playing_) return;
        playing_ = true;
        endNotified_ = false;
        // Starting at the last frame begins a new replay from frame zero.
        if (index_ == dataset_.frameCount - 1) request(0);
        else {
            state();
            if (!pending_) {
                if (loaded_) schedule();
                else request(index_);
            }
        }
    }

    void pause() override {
        if (!ready()) return;
        timer_->stop();
        playing_ = false;
        timelineActive_ = false;
        state();
    }

    void toggle() override { if (playing_) pause(); else play(); }

    void seek(int index) override {
        if (!ready()) return;
        if (index < 0 || index >= dataset_.frameCount) {
            emit error(QStringLiteral("Frame index is outside the dataset."));
            return;
        }
        if (index < dataset_.frameCount - 1) endNotified_ = false;
        request(index);
    }

    void step(int delta) override {
        if (!ready()) return;
        timer_->stop();
        playing_ = false;
        const qint64 target = static_cast<qint64>(index_) + delta;
        seek(static_cast<int>(std::clamp<qint64>(target, 0, dataset_.frameCount - 1)));
    }

    void setSpeed(double speed) override {
        if (!ready()) return;
        if (!std::isfinite(speed) || speed < 0.25 || speed > 16.0) {
            emit error(QStringLiteral("Playback speed must be finite and between 0.25 and 16."));
            return;
        }
        speed_ = speed;
        timelineActive_ = false;
        state();
        if (playing_ && !pending_ && loaded_) schedule();
    }

    void acceptFrame(quint64 requestId, radar::Frame frame) override {
        if (stopped_ || !pending_ || requestId != pendingId_) return;
        if (frame.index != index_ || frame.index < 0 || frame.index >= dataset_.frameCount) {
            acceptError(requestId, QStringLiteral("Reader returned an unexpected frame index."));
            return;
        }
        pending_ = false;
        loaded_ = true;
        const auto previous = timestamps_.find(index_ - 1);
        if (previous != timestamps_.end() && frame.radarTimestamp > previous->second)
            observedIntervalUs_ = static_cast<long double>(frame.radarTimestamp) - previous->second;
        timestamps_[index_] = frame.radarTimestamp;
        emit showFrame(frame);
        // A direct GUI receiver may seek or replace the dataset while showing a frame.
        if (playing_ && loaded_ && !pending_ && requestId == pendingId_) {
            if (index_ == dataset_.frameCount - 1) finish();
            else schedule();
        }
    }

    void acceptError(quint64 requestId, QString detail) override {
        if (stopped_ || !pending_ || requestId != pendingId_) return;
        pending_ = false;
        loaded_ = false;
        playing_ = false;
        timer_->stop();
        timelineActive_ = false;
        state();
        emit error(detail);
    }

    void shutdown() override {
        timer_->stop();
        stopped_ = true;
        pending_ = false;
        playing_ = false;
        state();
    }

private:
    bool ready() {
        if (stopped_) return false;
        if (dataset_.frameCount > 0) return true;
        emit error(QStringLiteral("Open a dataset before controlling playback."));
        return false;
    }
    void state() { emit stateChanged(playing_, index_, speed_); }
    void request(int index, bool automatic = false) {
        timer_->stop();
        if (!automatic) timelineActive_ = false;
        index_ = index;
        pending_ = true;
        loaded_ = false;
        pendingId_ = ++serial_;
        const auto requestId = pendingId_;
        state();
        if (pending_ && pendingId_ == requestId)
            emit requestFrame(requestId, index);
    }
    void schedule() {
        if (!playing_ || pending_ || !loaded_) return;
        if (index_ == dataset_.frameCount - 1) { finish(); return; }
        long double intervalUs = observedIntervalUs_ > 0 ? observedIntervalUs_ : meanIntervalUs_;
        const auto current = timestamps_.find(index_);
        const auto next = timestamps_.find(index_ + 1);
        if (current != timestamps_.end() && next != timestamps_.end() && next->second > current->second)
            intervalUs = static_cast<long double>(next->second) - current->second;
        const long double milliseconds = intervalUs / (speed_ * 1000.0L);
        if (!timelineActive_) {
            currentDeadlineMs_ = clock_.elapsed();
            timelineActive_ = true;
        }
        nextDeadlineMs_ = currentDeadlineMs_ + milliseconds;
        // Keep the recorded deadline across slow reads instead of accumulating I/O delay.
        const auto remainingMs = nextDeadlineMs_ - clock_.elapsed();
        timer_->start(static_cast<int>(std::clamp<long double>(std::ceil(remainingMs), 1,
                                                            std::numeric_limits<int>::max())));
    }
    void finish() {
        timer_->stop();
        playing_ = false;
        state();
        if (!endNotified_) { endNotified_ = true; emit finished(); }
    }
    QTimer *timer_;
    QElapsedTimer clock_;
    radar::Dataset dataset_;
    std::map<int, qint64> timestamps_;
    int index_ = -1;
    quint64 serial_ = 0, pendingId_ = 0;
    double speed_ = 1;
    long double meanIntervalUs_ = 100000, observedIntervalUs_ = 0;
    long double currentDeadlineMs_ = 0, nextDeadlineMs_ = 0;
    bool playing_ = false, pending_ = false, loaded_ = false;
    bool endNotified_ = false, stopped_ = false;
    bool timelineActive_ = false;
};
}

extern "C" RADAR_PLAYBACK_API radar::Playback *radar_create_playback(QObject *parent) {
    return new RecordedPlayback(parent);
}
