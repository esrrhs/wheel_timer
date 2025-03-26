# WheelTimer
C++ Hashed Hierarchical Wheel Timer, inspired by [Folly HHWheelTimer](https://github.com/facebook/folly/blob/main/folly/io/async/HHWheelTimer.h).
header only, no dependencies.

## Usage
```cpp
WheelTimer t;

// add two timer with 1000ms
auto t1 = t.Add(1000);
auto t2 = t.Add(1000);

// delete the second timer
t.Del(t2); 

// wait for 1010ms, because the timer granularity is 10ms
std::this_thread::sleep_for(std::chrono::milliseconds(1010));

// update the timer, return the expired timers t1
auto ret = t.Update();
```

## Build
```bash
mkdir build
cd build
cmake ..
make
```
