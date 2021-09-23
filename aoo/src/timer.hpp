#pragma once

#include "aoo/aoo.h"

#include "imp.hpp"

#ifndef HAVE_64BIT_ATOMICS
# include "common/sync.hpp"
#endif

#include "common/time.hpp"

#include <memory>
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
    timer(timer&& other);
    timer& operator=(timer&& other);

    void setup(int32_t sr, int32_t blocksize, bool check);
    void reset();
    double get_elapsed() const;
    time_tag get_absolute() const;
    state update(time_tag t, double& error);
private:
#ifdef HAVE_64BIT_ATOMICS
    std::atomic<uint64_t> last_;
    std::atomic<double> elapsed_{0};
#else
    uint64_t last_;
    double elapsed_{0};
    mutable sync::spinlock lock_;
    using scoped_lock = sync::scoped_lock<sync::spinlock>;
#endif

    // moving average filter to detect timing issues
    struct moving_average_check {
        static const size_t buffersize = 64;

        moving_average_check(double delta)
            : delta_(delta) {
            static_assert(is_pow2(buffersize),
                          "buffer size must be power of 2!");
        }

        state check(double delta, double& error);
        void reset();

        double delta_ = 0;
        double sum_ = 0;
        std::array<double, buffersize> buffer_;
        int32_t head_ = 0;
    };

    std::unique_ptr<moving_average_check> mavg_check_;
};

}
