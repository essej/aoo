#include <atomic>

volatile std::atomic<double> d{0};

int main() {
    d.store(1);
    if (d.is_lock_free()) {
        return 0;
    } else {
        return 1;
    }
}
