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

//this mutex won't favor writers -
//that is, if a writer tries to acquire the mutex
//with readers, it will simply fail and not prevent more
//readers from coming through
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

//This mutex is also a shared_exclusive mutex, except
//when a writer fails, it will make the mutex as waiting
//on a writer block incoming readers
//until that writer is satisfied
//after that writer, it becomes a free-for-all again
}
