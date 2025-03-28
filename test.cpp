#include <thread>
#include <iostream>
#include <map>

#include "wheel_timer.h"

#define ASSERT(x) \
    if (!(x)) { \
        std::cout << "Assertion failed " << #x << " on line " << __LINE__ << std::endl; \
        exit(1); \
    }

int test() {
    WheelTimer t;
    auto t1 = t.Add(1000);
    auto t2 = t.Add(2000);
    auto t3 = t.Add(3000);
    ASSERT(t.Size() == 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(1010));
    auto ret = t.Update();
    ASSERT(ret.size() == 1);
    ASSERT(ret[0] == t1);
    ASSERT(t.Size() == 2);
    std::cout << "tid " << t1 << " timeout" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1010));
    ret = t.Update();
    ASSERT(ret.size() == 1);
    ASSERT(ret[0] == t2);
    ASSERT(t.Size() == 1);
    std::cout << "tid " << t2 << " timeout" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1010));
    auto ok = t.Del(t3);
    ASSERT(ok);
    ASSERT(t.Size() == 0);
    ret = t.Update();
    ASSERT(ret.size() == 0);
    return 0;
}

int benchmark() {
    WheelTimer t;
    std::unordered_set<uint32_t> ids;

    auto begin = std::chrono::system_clock::now();
    for (int i = 0; i < 1000000; i++) {
        auto id = t.Add(1000);
        ids.insert(id);
    }
    ASSERT(t.Size() == 1000000);
    std::cout << "add 1000000 timers cost " << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now() - begin).count() << "ms" << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    WheelTimer tmp;
    for (int i = 0; i < 1000000; i++) {
        tmp.Add(1000);
    }
    begin = std::chrono::system_clock::now();
    for (auto id: ids) {
        auto ok = tmp.Del(id);
        ASSERT(ok);
    }
    ASSERT(tmp.Size() == 0);
    std::cout << "del 1000000 timers cost " << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now() - begin).count() << "ms" << std::endl;

    begin = std::chrono::system_clock::now();
    auto ret = t.Update();
    std::cout << "update cost " << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now() - begin).count() << "ms" << std::endl;

    for (auto id: ret) {
        ASSERT(ids.find(id) != ids.end());
        ids.erase(id);
    }
    ASSERT(ids.empty());
    ASSERT(t.Size() == 0);

    std::unordered_map<uint32_t, std::chrono::time_point<std::chrono::system_clock>> id_time;
    int count = 0;
    std::map<uint32_t, int> diffs;
    std::chrono::duration<int64_t, std::nano> max_add_time(0);
    std::chrono::duration<int64_t, std::nano> max_update_time(0);
    while (true) {
        if (count >= 100000) {
            if (id_time.empty()) {
                break;
            }
        } else {
            auto random_time = rand() % 100000 + 100;
            auto now = std::chrono::system_clock::now();
            auto id = t.Add(random_time);
            auto tmp = std::chrono::system_clock::now();
            if ((tmp - now) > max_add_time) {
                max_add_time = tmp - now;
            }
            id_time[id] = now + std::chrono::milliseconds(random_time);
        }
        count = count + 1;

        auto tmp = std::chrono::system_clock::now();
        auto ids = t.Update();
        auto now = std::chrono::system_clock::now();
        if ((now - tmp) > max_update_time) {
            max_update_time = now - tmp;
        }
        for (auto id: ids) {
            auto it = id_time.find(id);
            ASSERT(it != id_time.end());
            auto expect_time_ms = it->second;
            auto diff = now - expect_time_ms;
            diffs[std::chrono::duration_cast<std::chrono::milliseconds>(diff).count()]++;
            if (id % 10000 == 0) {
                std::cout << "tid " << id << " diff " << std::chrono::duration_cast<std::chrono::milliseconds>(diff)
                        .count() << "ms" << std::endl;
            }
            ASSERT(now >= expect_time_ms);
            id_time.erase(it);
        }
    }

    for (auto &it: diffs) {
        std::cout << "diff " << it.first << "ms count " << it.second << std::endl;
    }
    std::cout << "max add time " << max_add_time.count() / 1000000 << "ms" << std::endl;
    std::cout << "max update time " << max_update_time.count() / 1000000 << "ms" << std::endl;

    std::cout << "benchmark passed" << std::endl;
    return 0;
}

int main() {
    srand(0);
    test();
    benchmark();
    return 0;
}
