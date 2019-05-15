//
// Created by Eric Berdahl on 2019-05-14.
//

#ifndef MEDIATEST_STOPWATCH_H
#define MEDIATEST_STOPWATCH_H

#include <chrono>

class StopWatch
{
public:
    typedef std::chrono::high_resolution_clock  clock;
    typedef std::chrono::duration<double>       duration;

    StopWatch();

    void        restart();
    duration    getSplitTime() const;

private:
    clock::time_point   mStartTime;
};


#endif //MEDIATEST_STOPWATCH_H
