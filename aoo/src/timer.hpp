#pragma once

#include "aoo/aoo.h"

#include "common/sync.hpp"
#include "common/time.hpp"

#include <atomic>
#include <array>

namespace aoo {

class timer {
public:
    enum class state {
        reset,
        ok,
        error
    };
    timer() = default;
    timer(const timer& other);
    timer& operator=(const timer& other);
    void setup(int32_t sr, int32_t blocksize);
    void reset();
    double get_elapsed() const;
    time_tag get_absolute() const;
    state update(time_tag t, double& error);
private:
    std::atomic<uint64_t> last_;
    std::atomic<double> elapsed_{0};

#if AOO_TIMEFILTER_CHECK
    // moving average filter to detect timing issues
    static const size_t buffersize_ = 64;

    double delta_ = 0;
    double sum_ = 0;
    std::array<double, buffersize_> buffer_;
    int32_t head_ = 0;
#endif
};

}
