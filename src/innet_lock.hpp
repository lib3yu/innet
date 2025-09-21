#pragma once
#include <pthread.h>
#include <errno.h>

// ================= Mutex =================
class Mutex {
public:
    Mutex() { pthread_mutex_init(&m_, nullptr); }
    ~Mutex() { pthread_mutex_destroy(&m_); }

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    int lock()    { return pthread_mutex_lock(&m_); }
    int try_lock(){ return pthread_mutex_trylock(&m_); }
    int unlock()  { return pthread_mutex_unlock(&m_); }

    pthread_mutex_t* native_handle() { return &m_; }

private:
    pthread_mutex_t m_;
};

// ================= LockGuard =================
class LockGuard {
public:
    explicit LockGuard(Mutex& m) : m_(m) { m_.lock(); }
    ~LockGuard() { m_.unlock(); }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    Mutex& m_;
};

// ================= RWLock =================
class RWLock {
public:
    RWLock() { pthread_rwlock_init(&rw_, nullptr); }
    ~RWLock() { pthread_rwlock_destroy(&rw_); }

    RWLock(const RWLock&) = delete;
    RWLock& operator=(const RWLock&) = delete;

    int rdlock()    { return pthread_rwlock_rdlock(&rw_); }
    int try_rdlock(){ return pthread_rwlock_tryrdlock(&rw_); }
    int wrlock()    { return pthread_rwlock_wrlock(&rw_); }
    int try_wrlock(){ return pthread_rwlock_trywrlock(&rw_); }
    int unlock()    { return pthread_rwlock_unlock(&rw_); }

    pthread_rwlock_t* native_handle() { return &rw_; }

private:
    pthread_rwlock_t rw_;
};

// RAII read lock
class ReadGuard {
public:
    explicit ReadGuard(RWLock& rw) : rw_(rw) { rw_.rdlock(); }
    ~ReadGuard() { rw_.unlock(); }

    ReadGuard(const ReadGuard&) = delete;
    ReadGuard& operator=(const ReadGuard&) = delete;
private:
    RWLock& rw_;
};

// RAII write lock
class WriteGuard {
public:
    explicit WriteGuard(RWLock& rw) : rw_(rw) { rw_.wrlock(); }
    ~WriteGuard() { rw_.unlock(); }

    WriteGuard(const WriteGuard&) = delete;
    WriteGuard& operator=(const WriteGuard&) = delete;
private:
    RWLock& rw_;
};


// ================= UniqueLock =================
class UniqueLock {
public:
    explicit UniqueLock(Mutex& m) : mtx_(&m), owns_(false) {
        lock();
    }

    ~UniqueLock() {
        if (owns_) unlock();
    }

    UniqueLock(const UniqueLock&) = delete;
    UniqueLock& operator=(const UniqueLock&) = delete;

    // 移动语义
    UniqueLock(UniqueLock&& other) noexcept : mtx_(other.mtx_), owns_(other.owns_) {
        other.mtx_ = nullptr;
        other.owns_ = false;
    }
    UniqueLock& operator=(UniqueLock&& other) noexcept {
        if (this != &other) {
            if (owns_) unlock();
            mtx_ = other.mtx_;
            owns_ = other.owns_;
            other.mtx_ = nullptr;
            other.owns_ = false;
        }
        return *this;
    }

    int lock() {
        if (!mtx_ || owns_) return EINVAL;
        int r = mtx_->lock();
        if (r == 0) owns_ = true;
        return r;
    }

    int try_lock() {
        if (!mtx_ || owns_) return EINVAL;
        int r = mtx_->try_lock();
        if (r == 0) owns_ = true;
        return r;
    }

    int unlock() {
        if (!owns_) return EPERM;
        int r = mtx_->unlock();
        if (r == 0) owns_ = false;
        return r;
    }

    bool owns_lock() const { return owns_; }
    Mutex* mutex() const { return mtx_; }
    pthread_mutex_t* native_handle() { return mtx_ ? mtx_->native_handle() : nullptr; }

private:
    Mutex* mtx_;
    bool owns_;
};

// ================= Condition Variable =================
class CondVar {
public:
    CondVar() { pthread_cond_init(&cv_, nullptr); }
    ~CondVar() { pthread_cond_destroy(&cv_); }

    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;

    int wait(UniqueLock& lk) {
        if (!lk.mutex() || !lk.owns_lock()) return EINVAL;
        return pthread_cond_wait(&cv_, lk.native_handle());
    }

    int timedwait(UniqueLock& lk, const struct timespec* abstime) {
        if (!lk.mutex() || !lk.owns_lock()) return EINVAL;
        return pthread_cond_timedwait(&cv_, lk.native_handle(), abstime);
    }

    int signal()    { return pthread_cond_signal(&cv_); }
    int broadcast() { return pthread_cond_broadcast(&cv_); }

private:
    pthread_cond_t cv_;
};
