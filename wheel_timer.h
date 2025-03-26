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

class WheelTimer {
public:
    WheelTimer(): startTime_(GetCurTime()), expireTick_(1), interval_({Duration(DEFAULT_TICK_INTERVAL)}) {
    }

    ~WheelTimer() = default;

    uint32_t Add(uint32_t delay_ms) {
        auto timeout = std::chrono::milliseconds(delay_ms);
        auto now = GetCurTime();
        auto nextTick = CalcNextTick(now);

        auto t = std::make_shared<TimerNode>();
        t->id = timer_id_++;
        t->expiration = now + timeout;

        int64_t ticks = TimeToWheelTicks(timeout);
        int64_t due = ticks + nextTick;
        ScheduleTimeoutImpl(t, due, expireTick_);

        return t->id;
    }

    bool Del(uint32_t id) {
        auto it = timer_map_.find(id);
        if (it == timer_map_.end()) {
            return false;
        }

        auto t = it->second;
        timer_map_.erase(it);

        auto index1 = t->index1;
        auto index2 = t->index2;
        if (index1 < 0 || index1 >= WHEEL_BUCKETS || index2 < 0 || index2 >= (int) WHEEL_SIZE ||
            buckets_[index1][index2].find(t->id) == buckets_[index1][index2].end()) {
            return false;
        }

        buckets_[index1][index2].erase(t->id);
        return true;
    }

    std::vector<uint32_t> Update() {
        auto curTime = GetCurTime();
        auto nextTick = CalcNextTick(curTime);
        std::vector<uint32_t> ret;
        while (expireTick_ < nextTick) {
            int idx = expireTick_ & WHEEL_MASK;

            if (idx == 0) {
                // Cascade timers
                if (cascadeTimers(1, (expireTick_ >> WHEEL_BITS) & WHEEL_MASK, curTime) &&
                    cascadeTimers(2, (expireTick_ >> (2 * WHEEL_BITS)) & WHEEL_MASK, curTime)) {
                    cascadeTimers(3, (expireTick_ >> (3 * WHEEL_BITS)) & WHEEL_MASK, curTime);
                }
            }

            expireTick_++;
            for (auto& it: buckets_[0][idx]) {
                ret.push_back(it.second->id);
                timer_map_.erase(it.second->id);
            }
            buckets_[0][idx].clear();
        }

        return ret;
    }

private:
    struct TimerNode {
        std::chrono::time_point<std::chrono::steady_clock> expiration;
        uint32_t id = 0;
        int index1 = 0;
        int index2 = 0;
    };

    typedef std::shared_ptr<TimerNode> TimerNodePtr;

    class HHWheelTimerDurationInterval {
    public:
        explicit HHWheelTimerDurationInterval(Duration interval)
            : divInterval_(interval.count()),
              divIntervalForSteadyClock_(
                  std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval).count()),
              interval_(interval) {
        }

        int64_t ToWheelTicksFromSteadyClock(std::chrono::steady_clock::duration t) const {
            return t.count() / divIntervalForSteadyClock_;
        }

        int64_t ToWheelTicks(Duration t) const {
            return t.count() / divInterval_;
        }

        Duration FromWheelTicks(int64_t t) const {
            return t * interval_;
        }

        Duration Interval() const {
            return interval_;
        }

    private:
        uint64_t divInterval_;
        uint64_t divIntervalForSteadyClock_;
        Duration interval_;
    };

    std::chrono::steady_clock::time_point GetCurTime() {
        return std::chrono::steady_clock::now();
    }

    int64_t CalcNextTick(std::chrono::steady_clock::time_point curTime) {
        return interval_.ToWheelTicksFromSteadyClock(curTime - startTime_);
    }

    int64_t TimeToWheelTicks(Duration t) {
        return interval_.ToWheelTicks(t);
    }

    void ScheduleTimeoutImpl(TimerNodePtr t, int64_t dueTick, int64_t nextTickToProcess) {
        int64_t diff = dueTick - nextTickToProcess;
        assert(diff >= 0);
        if (diff < WHEEL_SIZE) {
            t->index1 = 0;
            t->index2 = dueTick & WHEEL_MASK;
            buckets_[t->index1][t->index2][t->id] = t;
        } else if (diff < 1 << (2 * WHEEL_BITS)) {
            buckets_[1][(dueTick >> WHEEL_BITS) & WHEEL_MASK][t->id] = t;
            t->index1 = 1;
            buckets_[t->index1][t->index2][t->id] = t;
        } else if (diff < 1 << (3 * WHEEL_BITS)) {
            t->index1 = 2;
            t->index2 = (dueTick >> 2 * WHEEL_BITS) & WHEEL_MASK;
            buckets_[t->index1][t->index2][t->id] = t;
        } else {
            /* in largest slot */
            if (diff > LARGEST_SLOT) {
                diff = LARGEST_SLOT;
                dueTick = diff + nextTickToProcess;
            }
            t->index1 = 3;
            t->index2 = (dueTick >> 3 * WHEEL_BITS) & WHEEL_MASK;
            buckets_[t->index1][t->index2][t->id] = t;
        }
        timer_map_[t->id] = t;
    }

    bool cascadeTimers(int bucket, int tick, const std::chrono::steady_clock::time_point curTime) {
        std::unordered_map<uint32_t, TimerNodePtr> tmp;
        tmp.swap(buckets_[bucket][tick]);
        auto nextTick = CalcNextTick(curTime);
        for (auto& it: tmp) {
            auto t = it.second;
            ScheduleTimeoutImpl(t, nextTick + TimeToWheelTicks(GetTimeRemaining(t, curTime)),
                                expireTick_);
        }

        // If tick is zero, timeoutExpired will cascade the next bucket.
        return tick == 0;
    }

    Duration GetTimeRemaining(TimerNodePtr t, std::chrono::steady_clock::time_point now) const {
        if (now >= t->expiration) {
            return Duration(0);
        }
        return std::chrono::duration_cast<Duration>(t->expiration - now);
    }

private:
    uint32_t timer_id_ = 0;
    std::chrono::steady_clock::time_point startTime_;
    int64_t expireTick_;
    HHWheelTimerDurationInterval interval_;
    std::unordered_map<uint32_t, TimerNodePtr> buckets_[WHEEL_BUCKETS][WHEEL_SIZE];
    std::unordered_map<uint32_t, TimerNodePtr> timer_map_;
};
