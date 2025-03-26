#include <thread>

#include "wheel_timer.h"

#define ASSERT(x) \
    if (!(x)) { \
        std::cout << "Assertion failed " << #x << " on line " << __LINE__ << std::endl; \
        exit(1); \
    }

int test() {
    return 0;
}

int benchmark() {
    return 0;
}

int main() {
    test();
    benchmark();
    return 0;
}
