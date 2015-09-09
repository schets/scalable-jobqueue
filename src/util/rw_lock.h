#pragma once

#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
//use pthreads
#include <pthreads.h>
namespace work_stealing {
namespace _private {
typedef pthread_rwlock_t rw_lock_t;

static inline void ac_read(rw_lock_t *lck) {
	pthread_rwlock_rdlock(lck);
}
static inline void rl_read(rw_lock_t *lck) {
	pthread_rwlock_unlock(lck);
}
static inline void ac_write(rw_lock_t *lck) {
	pthread_rwlock_wrlock(lck);
}
static inline void rl_write(rw_lock_t *lck) {
	pthread_rwlock_unlock(lck);
}
static inline void init_lock(rw_lock_t *lck) {
	pthread_rwlock_init(lck);
}
static inline void del_lock(rw_lock_t *lck) {
	pthread_rwlock_destroy(lck);
}
}
}
#elif defined(_MSC_VER)
#include <Windows.h>
typedef SRWLOCK rw_lock_t;
static inline void init_lock(rw_lock_t *lck) {
	InitializeSRWLock(lck);
}
static inline void del_lock(rw_lock_t *lck) {}
static inline void ac_read(rw_lock_t *lck) {
	AcquireSRWLockShared(lck);
}
static inline void rl_read(rw_lock_t *lck) {
	ReleaseSRWLockShared(lck);
}
static inline void ac_write(rw_lock_t *lck) {
	AcquireSRWLockExclusive(lck);
}
static inline void rl_write(rw_lock_t *lck) {
	ReleaseSRWLockExclusive(lck);
}
#else
#include <thread>
typedef std::mutex rw_lock_t;
//just use normal mutex
#endif
//A platform-independant reader-writer lock
//used for updating the hash table
namespace work_stealing {
namespace _private {
class rw_lock {
	rw_lock_t lock;
	bool initted;
public:
	void acquire_read() { ac_read(&lock); }
	void release_read() { rl_read(&lock); }
	void acquire_write() { ac_write(&lock); }
	void release_write() { rl_write(&lock); }
	void init() {
		if (initted) {
			return;
		}
		initted = true;
		init_lock(&lock);
	}
	void destroy() {
		if (initted) {
			initted = false;
			del_lock(&lock);
		}
	}
	rw_lock(bool create = false) {
		if (create)
			init();
	}
	//must manually destroy
	~rw_lock() {}
};

}
}
