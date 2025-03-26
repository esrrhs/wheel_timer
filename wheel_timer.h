#pragma once

#include <array>
#include <cassert>
#include <cstring>
#include <stdint.h>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <atomic>
#include <vector>
#include <map>
#include <sys/types.h>
#include <ctime>
#include <numeric>
#include <string>
#include <cstddef>
#include <unistd.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <functional>
#include <chrono>
#include <iterator>
#include <functional>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>
#include <iostream>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <iterator>
#include <queue>
#include <iostream>
#include <memory>
#include <type_traits>

static constexpr int DEFAULT_TICK_INTERVAL = 10;
static constexpr int WHEEL_BUCKETS = 4;
static constexpr int WHEEL_BITS = 8;
static constexpr unsigned int WHEEL_SIZE = (1 << WHEEL_BITS);
static constexpr unsigned int WHEEL_MASK = (WHEEL_SIZE - 1);
static constexpr uint32_t LARGEST_SLOT = 0xffffffffUL;

typedef std::chrono::milliseconds Duration;

namespace detail {
class HHWheelTimerDurationInterval {
public:
    explicit HHWheelTimerDurationInterval(Duration interval)
        : divInterval_(interval.count()),
          divIntervalForSteadyClock_(
              std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval).count()), interval_(interval) {
    }

    int64_t ToWheelTicksFromSteadyClock(std::chrono::steady_clock::duration t) const {
        return divIntervalForSteadyClock_.Divide(t.count());
    }

    int64_t ToWheelTicks(Duration t) const {
        return divInterval_.Divide(t.count());
    }

    Duration FromWheelTicks(int64_t t) const {
        return t * interval_;
    }

    Duration Interval() const {
        return interval_;
    }

    class Divider {
    public:
        explicit Divider(uint64_t v) : value_(v) {
        }

        uint64_t Divide(uint64_t num) const {
            return num / value_;
        }

    private:
        uint64_t value_;
    };

private:
    Divider divInterval_;
    Divider divIntervalForSteadyClock_;
    Duration interval_;
};
}

class WheelTimer {
public:
    WheelTimer() {
        startTime_ = GetCurTime();
        interval_ = {Duration(DEFAULT_TICK_INTERVAL)};
        bitmap_.fill(0);
    }

    ~WheelTimer() = default;


    uint32_t Add(uint32_t delay_ms) {
        auto timeout = std::chrono::milliseconds(delay_ms);
        auto now = GetCurTime();
        auto nextTick = CalcNextTick(now);

        auto t = std::make_shared<TimerNode>();
        t->id = timer_id_++;
        t->expiration = now + timeout;

        int64_t baseTick = nextTick;
        int64_t ticks = TimeToWheelTicks(timeout);
        int64_t due = ticks + nextTick;
        ScheduleTimeoutImpl(t, due, baseTick, nextTick);
    }

    bool Del(uint32_t id) {
    }

    std::vector<uint32_t> Update() {
    }

private:
    struct TimerNode {
        std::chrono::time_point<std::chrono::steady_clock> expiration;
        uint32_t id = 0;
    };

    typedef std::shared_ptr<TimerNode> TimerNodePtr;

    std::chrono::steady_clock::time_point GetCurTime() {
        return std::chrono::steady_clock::now();
    }

    int64_t CalcNextTick(std::chrono::steady_clock::time_point curTime) {
        return interval_.ToWheelTicksFromSteadyClock(curTime - startTime_);
    }

    int64_t TimeToWheelTicks(Duration t) {
        return interval_.ToWheelTicks(t);
    }

    void ScheduleTimeoutImpl(TimerNodePtr t, int64_t dueTick, int64_t nextTickToProcess, int64_t nextTick) {
        int64_t diff = dueTick - nextTickToProcess;

        auto bi = makeBitIterator(bitmap_.begin());

        if (diff < 0) {
            list = &buckets_[0][nextTick & WHEEL_MASK];
            *(bi + (nextTick & WHEEL_MASK)) = true;
            callback->bucket_ = nextTick & WHEEL_MASK;
        } else if (diff < WHEEL_SIZE) {
            list = &buckets_[0][dueTick & WHEEL_MASK];
            *(bi + (dueTick & WHEEL_MASK)) = true;
            callback->bucket_ = dueTick & WHEEL_MASK;
        } else if (diff < 1 << (2 * WHEEL_BITS)) {
            list = &buckets_[1][(dueTick >> WHEEL_BITS) & WHEEL_MASK];
        } else if (diff < 1 << (3 * WHEEL_BITS)) {
            list = &buckets_[2][(dueTick >> 2 * WHEEL_BITS) & WHEEL_MASK];
        } else {
            /* in largest slot */
            if (diff > LARGEST_SLOT) {
                diff = LARGEST_SLOT;
                dueTick = diff + nextTickToProcess;
            }
            list = &buckets_[3][(dueTick >> 3 * WHEEL_BITS) & WHEEL_MASK];
        }
        list->push_back(*callback);
    }

private:
    uint32_t timer_id_ = 0;
    std::chrono::steady_clock::time_point startTime_;
    detail::HHWheelTimerDurationInterval<Duration> interval_;
    std::array<std::size_t, (WHEEL_SIZE / sizeof(std::size_t)) / 8> bitmap_;
};
