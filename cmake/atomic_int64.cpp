#include <stdint.h>
#include <atomic>

volatile std::atomic<int64_t> i{0};

int main() {
    i.store(1);

    if (i.is_lock_free()) {
        return 0;
    } else {
        return 1;
    }
}
