#include <thread>

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
    std::this_thread::sleep_for(std::chrono::milliseconds(1010));
    auto ret = t.Update();
    ASSERT(ret.size() == 1);
    ASSERT(ret[0] == t1);
    std::cout << "tid " << t1 << " timeout" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1010));
    ret = t.Update();
    ASSERT(ret.size() == 1);
    ASSERT(ret[0] == t2);
    std::cout << "tid " << t2 << " timeout" << std::endl;
    auto ok = t.Del(t3);
    ASSERT(ok);
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
    std::cout << "add 1000000 timers cost " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - begin).count() << "ms" << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    WheelTimer tmp = t;
    begin = std::chrono::system_clock::now();
    for (auto id : ids) {
        auto ok = tmp.Del(id);
        ASSERT(ok);
    }
    std::cout << "del 1000000 timers cost " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - begin).count() << "ms" << std::endl;

    begin = std::chrono::system_clock::now();
    auto ret = t.Update();
    std::cout << "update cost " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - begin).count() << "ms" << std::endl;

    for (auto id : ret) {
        ASSERT(ids.find(id) != ids.end());
        ids.erase(id);
    }
    std::cout << "benchmark passed" << std::endl;
    return 0;
}

int main() {
    test();
    benchmark();
    return 0;
}
