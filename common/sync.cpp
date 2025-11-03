/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "sync.hpp"

#ifdef _WIN32
# include <windows.h>
#else
# include <sys/time.h>
# include <pthread.h>
#endif

#include <cassert>

namespace aoo {
namespace sync {

//-------------------------- thread priority -----------------------------//

void set_low_realtime_priority()
{
#if defined(_WIN32)
    // lower thread priority only for high priority or real time processes
    DWORD cls = GetPriorityClass(GetCurrentProcess());
    if (cls == HIGH_PRIORITY_CLASS || cls == REALTIME_PRIORITY_CLASS){
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    }
#elif defined(__APPLE__)
    // make sure that the network thread is not scheduled on an efficiency core.
    // Otherwise it might not be able to keep up with the audio thread.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#else
    // QUESTION: what should we do on Linux?
#endif
}

//---------------------------- atomics -------------------------------------//

namespace detail {
static padded_spinlock g_atomic_spinlock;

void global_spinlock_lock() { g_atomic_spinlock.lock(); }

void global_spinlock_unlock() { g_atomic_spinlock.unlock(); }

}

//---------------------- mutex -------------------------//

#ifdef _WIN32

mutex::mutex() {
    InitializeSRWLock((PSRWLOCK)& mutex_);
}

mutex::~mutex() {}

void mutex::lock() {
    AcquireSRWLockExclusive((PSRWLOCK)&mutex_);
}

bool mutex::try_lock() {
    return TryAcquireSRWLockExclusive((PSRWLOCK)&mutex_);
}

void mutex::unlock() {
    ReleaseSRWLockExclusive((PSRWLOCK)&mutex_);
}

#else

mutex::mutex() {
    pthread_mutex_init(&mutex_, nullptr);
}

mutex::~mutex() {
    pthread_mutex_destroy(&mutex_);
}

void mutex::lock() {
    pthread_mutex_lock(&mutex_);
}

bool mutex::try_lock() {
    return pthread_mutex_trylock(&mutex_) == 0;
}

void mutex::unlock() {
    pthread_mutex_unlock(&mutex_);
}

#endif

void recursive_mutex::lock(void) {
    auto id = std::this_thread::get_id();
    if (owner_.load(std::memory_order_relaxed) != id) {
        mutex::lock();
        owner_.store(id, std::memory_order_relaxed);
    }
    count_++;
}

bool recursive_mutex::try_lock() {
    auto id = std::this_thread::get_id();
    if (owner_.load(std::memory_order_relaxed) != id) {
        if (mutex::try_lock()) {
            owner_.store(id, std::memory_order_relaxed);
        } else {
            return false;
        }
    }
    count_++;
    return true;
}

void recursive_mutex::unlock(void) {
    assert(count_ > 0);
    if (--count_ == 0) {
        owner_.store(std::thread::id{}, std::memory_order_relaxed);
        mutex::unlock();
    }
}

//-------------------- shared_mutex -------------------------//

#if defined(_WIN32)

shared_mutex::shared_mutex() {
    InitializeSRWLock((PSRWLOCK)& rwlock_);
}

shared_mutex::~shared_mutex() {}

// exclusive
void shared_mutex::lock() {
    AcquireSRWLockExclusive((PSRWLOCK)&rwlock_);
}

bool shared_mutex::try_lock() {
    return TryAcquireSRWLockExclusive((PSRWLOCK)&rwlock_);
}

void shared_mutex::unlock() {
    ReleaseSRWLockExclusive((PSRWLOCK)&rwlock_);
}

// shared
void shared_mutex::lock_shared() {
    AcquireSRWLockShared((PSRWLOCK)&rwlock_);
}

bool shared_mutex::try_lock_shared() {
    return TryAcquireSRWLockShared((PSRWLOCK)&rwlock_);
}

void shared_mutex::unlock_shared() {
    ReleaseSRWLockShared((PSRWLOCK)&rwlock_);
}

#elif defined(AOO_HAVE_PTHREAD_RWLOCK)

shared_mutex::shared_mutex() {
    pthread_rwlock_init(&rwlock_, nullptr);
}

shared_mutex::~shared_mutex() {
    pthread_rwlock_destroy(&rwlock_);
}

// exclusive
void shared_mutex::lock() {
    pthread_rwlock_wrlock(&rwlock_);
}

bool shared_mutex::try_lock() {
    return pthread_rwlock_trywrlock(&rwlock_) == 0;
}
void shared_mutex::unlock() {
    pthread_rwlock_unlock(&rwlock_);
}

// shared
void shared_mutex::lock_shared() {
    pthread_rwlock_rdlock(&rwlock_);
}

bool shared_mutex::try_lock_shared() {
    return pthread_rwlock_tryrdlock(&rwlock_) == 0;
}

void shared_mutex::unlock_shared() {
    pthread_rwlock_unlock(&rwlock_);
}

#endif // _WIN32 || AOO_HAVE_PTHREAD_RWLOCK

void shared_recursive_mutex::lock(void) {
    auto id = std::this_thread::get_id();
    if (owner_.load(std::memory_order_acquire) != id) {
        shared_mutex::lock();
        owner_.store(id, std::memory_order_release);
    }
    count_++;
}

bool shared_recursive_mutex::try_lock() {
    auto id = std::this_thread::get_id();
    if (owner_.load(std::memory_order_acquire) != id) {
        if (shared_mutex::try_lock()) {
            owner_.store(id, std::memory_order_release);
        } else {
            return false;
        }
    }
    count_++;
    return true;
}

void shared_recursive_mutex::unlock(void) {
    assert(count_ > 0);
    assert(owner_.load() == std::this_thread::get_id());
    if (--count_ == 0) {
        owner_.store(std::thread::id{}, std::memory_order_release);
        shared_mutex::unlock();
    }
}

void shared_recursive_mutex::lock_shared() {
    if (owner_.load(std::memory_order_acquire) != std::this_thread::get_id()) {
        shared_mutex::lock_shared();
    }
}

bool shared_recursive_mutex::try_lock_shared() {
    if (owner_.load(std::memory_order_acquire) != std::this_thread::get_id()) {
        return shared_mutex::try_lock_shared();
    } else {
        return true;
    }
}

void shared_recursive_mutex::unlock_shared() {
    if (owner_.load(std::memory_order_acquire) != std::this_thread::get_id()) {
        shared_mutex::unlock_shared();
    }
}

//-------------------- native_semaphore -----------------------//

#ifdef HAVE_SEMAPHORE

namespace detail {

native_semaphore::native_semaphore(){
#if defined(_WIN32)
    sem_ = CreateSemaphoreA(0, 0, LONG_MAX, 0);
#elif defined(__APPLE__)
    semaphore_create(mach_task_self(), &sem_, SYNC_POLICY_FIFO, 0);
#else // posix
    sem_init(&sem_, 0, 0);
#endif
}

native_semaphore::~native_semaphore(){
#if defined(_WIN32)
    CloseHandle(sem_);
#elif defined(__APPLE__)
    semaphore_destroy(mach_task_self(), sem_);
#else // posix
    sem_destroy(&sem_);
#endif
}

void native_semaphore::post(){
#if defined(_WIN32)
    ReleaseSemaphore(sem_, 1, 0);
#elif defined(__APPLE__)
    semaphore_signal(sem_);
#else // posix
    sem_post(&sem_);
#endif
}

void native_semaphore::wait(){
#if defined(_WIN32)
    WaitForSingleObject(sem_, INFINITE);
#elif defined(__APPLE__)
    semaphore_wait(sem_);
#else // posix
    while (sem_wait(&sem_) == -1 && errno == EINTR) continue;
#endif
}

bool native_semaphore::try_wait(){
#if defined(_WIN32)
    // timeout: WAIT_TIMEOUT
    return WaitForSingleObject(sem_, 0) == 0;
#elif defined(__APPLE__)
    mach_timespec_t ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    // timeout: KERN_OPERATION_TIMED_OUT
    return semaphore_timedwait(sem_, ts) == KERN_SUCCESS;
#else // posix
    while (sem_trywait(&sem_) == -1) {
        if (errno != EINTR) // EAGAIN
            return false;
    }
    return true;
#endif
}

bool native_semaphore::wait_for(double seconds){
#if defined(_WIN32)
    // timeout: WAIT_TIMEOUT
    return WaitForSingleObject(sem_, seconds * 1000) == 0;
#elif defined(__APPLE__)
    mach_timespec_t ts;
    ts.tv_sec = seconds;
    ts.tv_nsec = (seconds - ts.tv_sec) * 1000000000;
    // timeout: KERN_OPERATION_TIMED_OUT
    return semaphore_timedwait(sem_, ts) == KERN_SUCCESS;
#else // posix
    struct timeval now;
    gettimeofday(&now, 0);
    // add fractional part to timeout
    seconds += now.tv_usec * 0.000001;
    struct timespec ts;
    ts.tv_sec = now.tv_sec + (time_t)seconds;
    ts.tv_nsec = (seconds - (time_t)seconds) * 1000000000;
    while (sem_timedwait(&sem_, &ts) == -1)
    {
        if (errno != EINTR) // ETIMEDOUT
            return false;
    }
    return true;
#endif
}

} // detail

#endif // HAVE_SEMAPHORE

//---------------------- semaphore ---------------------//

#ifdef HAVE_SEMAPHORE

semaphore::semaphore() {}

semaphore::~semaphore() {}

void semaphore::post() {
    auto old = count_.fetch_add(1, std::memory_order_release);
    if (old < 0){
        sem_.post();
    }
}

void semaphore::wait() {
    auto old = count_.fetch_sub(1, std::memory_order_acquire);
    if (old <= 0){
        sem_.wait();
    }
}

bool semaphore::try_wait() {
    auto count = count_.load(std::memory_order_relaxed);
    for (;;) {
        if (count > 0) {
            if (count_.compare_exchange_weak(count, count - 1,
                    std::memory_order_acquire, std::memory_order_relaxed))
                return true;
            // try again; count has been updated
        }
    }
    return false;
}

bool semaphore::wait_for(double seconds) {
    auto old = count_.fetch_sub(1, std::memory_order_acquire);
    if (old > 0)
        return true;

    if (sem_.wait_for(seconds))
        return true;

    // Thanks to moodycamel!
    // See https://github.com/cameron314/concurrentqueue/blob/master/lightweightsemaphore.h
    // "At this point, we've timed out waiting for the semaphore, but the
    // count is still decremented indicating we may still be waiting on
    // it. So we have to re-adjust the count, but only if the semaphore
    // wasn't signaled enough times for us too since then. If it was, we
    // need to release the semaphore too."
    for (;;) {
        old = count_.load(std::memory_order_acquire);
        if (old >= 0 && sem_.try_wait())
            return true;
        if (old < 0 && count_.compare_exchange_strong(old, old + 1,
                std::memory_order_relaxed, std::memory_order_relaxed))
            return false;
    }
}

#else

semaphore::semaphore() {
    pthread_mutex_init(&mutex_, nullptr);
    pthread_cond_init(&condition_, nullptr);
}

semaphore::~semaphore() {
    pthread_mutex_destroy(&mutex_);
    pthread_cond_destroy(&condition_);
}

void semaphore::post() {
    pthread_mutex_lock(&mutex_);
    count_++;
    pthread_mutex_unlock(&mutex_);
    pthread_cond_signal(&condition_);
}

void semaphore::wait() {
    pthread_mutex_lock(&mutex_);
    // wait till count is larger than zero
    while (count_ == 0) {
        pthread_cond_wait(&condition_, &mutex_);
    }
    count_--; // release
    pthread_mutex_unlock(&mutex_);
}

bool semaphore::try_wait() {
    pthread_mutex_lock(&mutex_);
    auto success = count_ > 0;
    if (success) {
        count_--;
    }
    pthread_mutex_unlock(&mutex_);
    return success;
}

bool semaphore::wait_for(double seconds) {
    pthread_mutex_lock(&mutex_);
    if (count_ > 0) {
        count_--;
        pthread_mutex_unlock(&mutex_);
        return true;
    }

    // let's unlock before calling gettimeofday()
    pthread_mutex_unlock(&mutex_);
    struct timeval now;
    gettimeofday(&now, 0);
    // add fractional part to timeout
    seconds += now.tv_usec * 0.000001;
    struct timespec ts;
    ts.tv_sec = now.tv_sec + (time_t)seconds;
    ts.tv_nsec = (seconds - (time_t)seconds) * 1000000000;

    pthread_mutex_lock(&mutex_);
    // wait till count is larger than zero
    while (count_ == 0) {
        if (pthread_cond_timedwait(&condition_, &mutex_, &ts) == ETIMEDOUT) {
            pthread_mutex_unlock(&mutex_);
            return false;
        }
    }
    count_--; // release
    pthread_mutex_unlock(&mutex_);
    return true;
}

#endif // HAVE_SEMAPHORE

//---------------------- event -------------------------//

#ifdef HAVE_SEMAPHORE

event::event() {}

event::~event() {}

void event::set() {
    int oldcount = count_.load(std::memory_order_relaxed);
    for (;;) {
        // don't increment past 1
        // NOTE: we have to use the CAS loop even if we don't
        // increment 'oldcount', because a another thread
        // might decrement the counter concurrently!
        auto newcount = oldcount >= 0 ? 1 : oldcount + 1;
        if (count_.compare_exchange_weak(oldcount, newcount, std::memory_order_release,
                                         std::memory_order_relaxed))
            break;
    }
    if (oldcount < 0)
        sem_.post(); // release one waiting thread
}

void event::wait() {
    auto old = count_.fetch_sub(1, std::memory_order_acquire);
    if (old <= 0){
        sem_.wait();
    }
}

bool event::try_wait() {
    auto count = count_.load(std::memory_order_relaxed);
    for (;;) {
        if (count > 0) {
            if (count_.compare_exchange_weak(count, count - 1,
                    std::memory_order_acquire, std::memory_order_relaxed))
                return true;
            // try again; count has been updated
        }
    }
    return false;
}

bool event::wait_for(double seconds) {
    auto old = count_.fetch_sub(1, std::memory_order_acquire);
    if (old > 0)
        return true;

    if (sem_.wait_for(seconds))
        return true;

    // See semaphore::wait_for()
    for (;;) {
        old = count_.load(std::memory_order_acquire);
        if (old >= 0 && sem_.try_wait())
            return true;
        if (old < 0 && count_.compare_exchange_strong(old, old + 1,
                std::memory_order_relaxed, std::memory_order_relaxed))
            return false;
    }
}

#else

event::event() {
    pthread_mutex_init(&mutex_, nullptr);
    pthread_cond_init(&condition_, nullptr);
}

event::~event() {
    pthread_mutex_destroy(&mutex_);
    pthread_cond_destroy(&condition_);
}

void event::set() {
    pthread_mutex_lock(&mutex_);
    state_ = true;
    pthread_mutex_unlock(&mutex_);
    pthread_cond_signal(&condition_);
}

void event::wait() {
    pthread_mutex_lock(&mutex_);
    // wait till event is set
    while (!state_) {
        pthread_cond_wait(&condition_, &mutex_);
    }
    state_ = false; // unset
    pthread_mutex_unlock(&mutex_);
}

bool event::try_wait() {
    pthread_mutex_lock(&mutex_);
    bool success = state_;
    if (success) {
        state_ = false; // unset
    }
    pthread_mutex_unlock(&mutex_);
    return success;
}

bool event::wait_for(double seconds) {
    pthread_mutex_lock(&mutex_);
    if (state_) {
        state_ = false;
        pthread_mutex_unlock(&mutex_);
        return true;
    }

    // let's unlock before calling gettimeofday()
    pthread_mutex_unlock(&mutex_);
    struct timeval now;
    gettimeofday(&now, 0);
    // add fractional part to timeout
    seconds += now.tv_usec * 0.000001;
    struct timespec ts;
    ts.tv_sec = now.tv_sec + (time_t)seconds;
    ts.tv_nsec = (seconds - (time_t)seconds) * 1000000000;

    pthread_mutex_lock(&mutex_);
    // wait till count is larger than zero
    while (!state_) {
        if (pthread_cond_timedwait(&condition_, &mutex_, &ts) == ETIMEDOUT) {
            pthread_mutex_unlock(&mutex_);
            return false;
        }
    }
    state_ = false; // unset
    pthread_mutex_unlock(&mutex_);
    return true;
}

#endif // HAVE_SEMAPHORE

} // sync
} // aoo
