#include <atomic>
#include <stdint.h>
namespace work_stealing {

class mutex {
	std::atomic<char> is_held;
public:
	bool try_lock();

	void unlock();

	bool locked();

};

//this mutex is a shared-exclusive mutex
//it is one of two available - each one has the
//same semantics, but treat writers differently

enum class writer_priority {
	favors_writers,
	favors_readers,
};

/**
 * This is a shared mutex with two versions -
 * one is a mutex which favors readers over writers,
 * and the other favors writers. What does that mean?
 * 
 * When a writer tries to acquire the favors_reader mutex and fails,
 * it simply fails to acquire the mutex. Readers can still acquire
 * the mutex - theoretically locking out the writer forever. This
 * version is more effecient, and is much less likely to block readers
 *
 * When a writer tries to acquires the favors_writer mutex, it prevents future
 * readers from acquiring the mutex until a writer gets a turn.
 * In addition, any writers that try to wait while another writer works
 * can prevent readers from acquiring after the writer leaves. This
 * mutex has more room for blocking readers and is slower, so
 * should only be considered if writers must have high priority
*/
template<writer_priority = writer_priority::favors_readers>
class shared_mutex {
	//store rw data mixed in a 64 bit thingy!
	std::atomic<uint64_t> lock_data;
public:
	bool try_exclusive_lock();
	void exclusive_unlock();

	bool try_shared_lock();
	void shared_unlock();
};

}
