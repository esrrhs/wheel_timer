#pragma once

#include <cassert>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <sys/types.h>
#include <unistd.h>
#include <cmath>
#include <chrono>
#include <fstream>
#include <memory>
#include <algorithm>

static constexpr int DEFAULT_TICK_INTERVAL = 10;
static constexpr int WHEEL_BUCKETS = 4;
static constexpr int WHEEL_BITS = 8;
static constexpr unsigned int WHEEL_SIZE = (1 << WHEEL_BITS);
static constexpr unsigned int WHEEL_MASK = (WHEEL_SIZE - 1);
static constexpr uint32_t LARGEST_SLOT = 0xffffffffUL;
static constexpr std::chrono::milliseconds INTERVAL = std::chrono::milliseconds(DEFAULT_TICK_INTERVAL);

class WheelTimer {
public:
    WheelTimer() = default;

    ~WheelTimer() = default;

    // delete copy constructor
    WheelTimer(const WheelTimer &) = delete;

    // assignment operator
    WheelTimer &operator=(const WheelTimer &) = delete;

    uint32_t Add(uint32_t delay_ms) {
        auto now = Clock::now();
        int64_t end_tick = 0;
        if (delay_ms == 0) {
            // Expire on the next Update, even if the current tick has not advanced.
            end_tick = expireTick_;
        } else {
            auto deadline = now + std::chrono::milliseconds(delay_ms);
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>
                    (deadline - startTime_).count();
            // Round up so a delay of N ms is not truncated onto an earlier tick.
            end_tick = (elapsed_ms + INTERVAL.count() - 1) / INTERVAL.count();
            if (end_tick < expireTick_) {
                end_tick = expireTick_;
            }
        }

        auto t = std::make_shared<TimerNode>();
        t->id = timer_id_++;
        t->when = end_tick;

        AddImpl(t, end_tick);

        return t->id;
    }

    bool Del(uint32_t id) {
        auto it = timer_map_.find(id);
        if (it == timer_map_.end()) {
            return false;
        }

        auto t = it->second;

        auto index_bucket = t->index_bucket;
        auto index_idx = t->index_idx;
        if (index_bucket < 0 || index_bucket >= WHEEL_BUCKETS || index_idx < 0 || index_idx >= (int) WHEEL_SIZE) {
            timer_map_.erase(it);
            return false;
        }

        auto &bucket = buckets_[index_bucket][index_idx];

        if (t->index_vec < 0 || t->index_vec >= (int) bucket.size()) {
            timer_map_.erase(it);
            return false;
        }

        auto last = bucket.back();
        if (last->index_vec != t->index_vec) {
            bucket[t->index_vec] = last;
            last->index_vec = t->index_vec;
        }
        bucket.pop_back();
        timer_map_.erase(it);

        return true;
    }

    std::vector<uint32_t> Update() {
        auto now = Clock::now();
        auto now_tick = std::chrono::duration_cast<std::chrono::milliseconds>
                                (now - startTime_).count() / INTERVAL.count();
        std::vector<uint32_t> ret;
        // Include the current tick so a timer due at now_tick fires in this Update.
        while (expireTick_ <= now_tick) {
            int idx = expireTick_ & WHEEL_MASK;

            if (idx == 0) {
                // Cascade timers
                if (CascadeTimers(1, (expireTick_ >> WHEEL_BITS) & WHEEL_MASK)) {
                    if (CascadeTimers(2, (expireTick_ >> (2 * WHEEL_BITS)) & WHEEL_MASK)) {
                        CascadeTimers(3, (expireTick_ >> (3 * WHEEL_BITS)) & WHEEL_MASK);
                    }
                }
            }

            auto &bucket = buckets_[0][idx];
            expireTick_++;
            ret.reserve(ret.size() + bucket.size());
            for (auto &t: bucket) {
                // Skip nodes cancelled from the map but left on the wheel.
                if (timer_map_.erase(t->id)) {
                    ret.push_back(t->id);
                }
            }
            bucket.clear();
        }

        return ret;
    }

    size_t Size() const {
        return timer_map_.size();
    }

private:
    struct TimerNode {
        int64_t when;
        uint32_t id = 0;
        int index_bucket = 0;
        int index_idx = 0;
        int index_vec = 0;
    };

    typedef std::shared_ptr<TimerNode> TimerNodePtr;

    void AddImpl(TimerNodePtr t, int64_t end_tick) {
        int64_t diff = end_tick - expireTick_;
        if (diff < 0) {
            // Already due: place on the slot Update will process next, not a past index.
            t->when = expireTick_;
            t->index_bucket = 0;
            t->index_idx = expireTick_ & WHEEL_MASK;
        } else if (diff < WHEEL_SIZE) {
            t->index_bucket = 0;
            t->index_idx = end_tick & WHEEL_MASK;
        } else if (diff < 1 << (2 * WHEEL_BITS)) {
            t->index_bucket = 1;
            t->index_idx = (end_tick >> WHEEL_BITS) & WHEEL_MASK;
        } else if (diff < 1 << (3 * WHEEL_BITS)) {
            t->index_bucket = 2;
            t->index_idx = (end_tick >> (2 * WHEEL_BITS)) & WHEEL_MASK;
        } else {
            /* in largest slot */
            if (diff > LARGEST_SLOT) {
                diff = LARGEST_SLOT;
                end_tick = diff + expireTick_;
                t->when = end_tick;
            }
            t->index_bucket = 3;
            t->index_idx = (end_tick >> (3 * WHEEL_BITS)) & WHEEL_MASK;
        }

        auto &bucket = buckets_[t->index_bucket][t->index_idx];
        t->index_vec = (int) bucket.size();
        bucket.push_back(t);

        timer_map_[t->id] = t;
    }

    bool CascadeTimers(int bucket, int tick) {
        std::vector<TimerNodePtr> tmp;
        tmp.swap(buckets_[bucket][tick]);
        for (auto &t: tmp) {
            AddImpl(t, t->when);
        }

        // If tick is zero, timeoutExpired will cascade the next bucket.
        return tick == 0;
    }

private:
    uint32_t timer_id_ = 0;
    using Clock = std::chrono::steady_clock;

    Clock::time_point startTime_ = Clock::now();
    int64_t expireTick_ = 0;
    std::vector<TimerNodePtr> buckets_[WHEEL_BUCKETS][WHEEL_SIZE];
    std::unordered_map<uint32_t, TimerNodePtr> timer_map_;
};
