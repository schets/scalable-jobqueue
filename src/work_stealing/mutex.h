#include <atomic>
#include <stdint.h>
namespace work_stealing {

	class mutex {
		std::atomic<char> is_held;
	public:
		bool try_lock();

		void unlock();

		bool locked();
		//This doesn't provide a blocking lock mechanism
		//It allows one to obtain the lock,
		//OR perform other action.
		//is now syntax sugar for the implementation
		template<class T>
		void lock_or(const op& op);
	};

	//this one is more complicated - 
	//various strategies can be used
	//to ensure that writes get access,
	//up to and including none at all


namespace _private {
//this ensures that the operations can actually
//be packed. If not, then there will be some
//performance problems under contention


}

//fails with more than 2^32 active readers
class shared_mutex {
	//store rw data mixed in a 64 bit thingy!
	rw_type<can_pack> lock_data;
public:
	bool try_exclusive_lock();
	void exclusive_unlock();
};

}
