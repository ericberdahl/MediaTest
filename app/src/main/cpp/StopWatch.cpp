//
// Created by Eric Berdahl on 2019-05-14.
//

#include "StopWatch.hpp"

StopWatch::StopWatch()
{
    restart();
}

void StopWatch::restart()
{
    mStartTime = clock::now();
}

StopWatch::duration StopWatch::getSplitTime() const
{
    const auto endTime = clock::now();
    return endTime - mStartTime;
}
