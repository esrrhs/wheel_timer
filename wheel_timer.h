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
        auto timeout = std::chrono::milliseconds(delay_ms);
        auto now = std::chrono::system_clock::now();
        auto end_time = now + timeout;
        auto end_tick = std::chrono::duration_cast<std::chrono::milliseconds>
                                (end_time - startTime_).count() / INTERVAL.count();

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
        timer_map_.erase(it);

        auto index_bucket = t->index_bucket;
        auto index_idx = t->index_idx;
        if (index_bucket < 0 || index_bucket >= WHEEL_BUCKETS || index_idx < 0 || index_idx >= (int) WHEEL_SIZE ||
            buckets_[index_bucket][index_idx].find(t->id) == buckets_[index_bucket][index_idx].end()) {
            return false;
        }

        buckets_[index_bucket][index_idx].erase(t->id);
        return true;
    }

    std::vector<uint32_t> Update() {
        auto now = std::chrono::system_clock::now();
        auto now_tick = std::chrono::duration_cast<std::chrono::milliseconds>
                                (now - startTime_).count() / INTERVAL.count();
        std::vector<uint32_t> ret;
        while (expireTick_ < now_tick) {
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
            for (auto &it: bucket) {
                ret.push_back(it.second->id);
                timer_map_.erase(it.second->id);
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
    };

    typedef std::shared_ptr<TimerNode> TimerNodePtr;

    void AddImpl(TimerNodePtr t, int64_t end_tick) {
        int64_t diff = end_tick - expireTick_;
        if (diff < WHEEL_SIZE) {
            t->index_bucket = 0;
            t->index_idx = end_tick & WHEEL_MASK;
        } else if (diff < 1 << (2 * WHEEL_BITS)) {
            t->index_bucket = 1;
            t->index_idx = (end_tick >> WHEEL_BITS) & WHEEL_MASK;
        } else if (diff < 1 << (3 * WHEEL_BITS)) {
            t->index_bucket = 2;
            t->index_idx = (end_tick >> 2 * WHEEL_BITS) & WHEEL_MASK;
        } else {
            /* in largest slot */
            if (diff > LARGEST_SLOT) {
                diff = LARGEST_SLOT;
                end_tick = diff + expireTick_;
            }
            t->index_bucket = 3;
            t->index_idx = (end_tick >> 3 * WHEEL_BITS) & WHEEL_MASK;
        }
        buckets_[t->index_bucket][t->index_idx][t->id] = t;
        timer_map_[t->id] = t;
    }

    bool CascadeTimers(int bucket, int tick) {
        std::unordered_map<uint32_t, TimerNodePtr> tmp;
        tmp.swap(buckets_[bucket][tick]);
        for (auto &it: tmp) {
            auto t = it.second;
            AddImpl(t, t->when);
        }

        // If tick is zero, timeoutExpired will cascade the next bucket.
        return tick == 0;
    }

private:
    uint32_t timer_id_ = 0;
    std::chrono::system_clock::time_point startTime_ = std::chrono::system_clock::now();
    int64_t expireTick_ = 0;
    std::unordered_map<uint32_t, TimerNodePtr> buckets_[WHEEL_BUCKETS][WHEEL_SIZE];
    std::unordered_map<uint32_t, TimerNodePtr> timer_map_;
};
