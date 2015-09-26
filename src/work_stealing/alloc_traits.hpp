#include <allocator>
#include <type_traits>

#include "task.hpp"

//Finish all of this later,
//ones there's actualy a work queue to work with
namespace work_stealing {

/**
 * This header holds the traits classes that
 * control how memory is managed.
 */

class default_alloc_traits {
public:

	/**
	 * If this trait is set to true,
	 * then all allocated memory will
	 * be deallocated by the same worker that
	 * allocated it, and that call will be made
	 * from the same thread
	 */
	static constexpr bool worker_local = false;

	/**
	 * This determines if the allocator behaves differently
	 * when allocating shared memory vs worker-local memory
	 * If this is true, then the allocator must have a function
	 * with the signature allocate_shared(size_t size, void *hint)
	 * and deallocate_shared(void *dat, size_t size)
	 */
	static constexpr bool shared_alloc = false;

	/**
	 * This trait determines whether objects
	 * can be passed between allocators without being
	 * modified. If this trait is false, then the allocator
	 * must provide the function transfer_object(T *obj)
	 *
	 * transfer_object is only called directly prior
	 * to the execution of the task in thread different
	 * than the allocating thread
	 */
	static constexpr bool transferable_objects = true;

	 /**
	  * This determines whether objects must be copied 
	  * into worker_local memory before being executed
	  * by another thread. If true, then shared_alloc
	  * will be ignored since all objects are going
	  * to be thread-local on execution.
	  *
	  * If this trait is false, then each allocator must
	  * provide a method to copy an object with the signature
	  * task *copy_object(task *from, Alloc &from).
	  * If the task must be copied, then the 
	  */
	static constexpr bool shareable_objects = true;

	/**
	 * This trait determines if objects can be destroyed in
	 * a worker different than the one they were created in
	 * If false, then objects will only be destroyed by the 
	 * allocating worker. Note that this implies worker_local,
	 * and will result in the workers to act as if that flag is
	 * set to true regardless of the actual value. Setting
	 * this to false may negatively affect performance
	 */
	static constexpr bool destroy_shared = true;
};

template<class alloc_type>
class alloc_traits : public default_alloc_traits {};

namespace _private {

//For reference, the false option will go the unspecialized;
//while the true option will be the specialization for
//all of the below. This is if specialization must happen!

//Methods for allocating shared memory
template<class Alloc, bool shared = alloc_traits<alloc>::shared_alloc>
struct shared_methods {
	static void *allocate_shared(Alloc& al, size_t s, void *p) {
		return al.allocate(s, p);
	}

	static void deallocate_shared(Alloc& al, void *p, size_t s) {
		al.deallocate(p, s);
	}
};

template<class Alloc, true>
struct shared_methods {
	static void *allocate_shared(Alloc &al, size_t s, void *p) {
		return al.allocate_shared(s, p);
	}

	static void deallocate_shared(Alloc& al, void *p, size_t s) {
		al.deallocate_shared(p, s);
	}
};



//!Methods to return a pointer to the owner

template<class Alloc, bool is_local = alloc_traits<alloc>::worker_local
						           || !alloc_traits<alloc>::destoy_shared>
struct return_methods { 
	template<class Rop>
	static void return_ptrs(Alloc& al, void *p, size_t s, const Rop &ret_v) {
		shared_methods<Alloc>::deallocate_shared(al, p, s);
	}
};

template<class Alloc, true>
struct return_methods {
	template<class Rop>
	static void return_ptrs(Alloc& al, void *p, size_t s, const Rop &ret_v) {
		ret_v(p, s);
	}
};

//!Methods for transferring objects

template<class Alloc, bool trans = alloc_traits<alloc>::transferable_objects>
struct transfer_methods {
	template<class t>
	void transfer_object(Alloc &al, task<T> *obj) {
		al.transfer_object(&obj->fnc);
	}
};

template<class Alloc, true>
struct transfer_methods {
	template<class T>
	void transfer_object(Alloc &al, task<T> *obj) {}
};

//This class makes it so that
//workers don't actually observe the
//values of alloc_traits -
//they simply makes calls
//assuming each one must happen
// (i.e always allocate_shared,)
//This also ensures that the traits are const
template<class Alloc>
struct create_alloc_functions : public Alloc {
	using traits = alloc_traits<Alloc>;
	using alloc = Alloc;

	void *allocate_shared(size_t s, void *hint) {
		return shared_methods<Alloc>::allocate_shared(*this, s, hint);
	}

	void deallocate_shared(void *p, size_t s) {
		shared_methods<Alloc>::deallocate_shared(*this, p, s);
	}

	template<class Rop>
	void return_ptrs(void *p, size_t s, const Rop& ro) {
		return_methods<Alloc>::return_ptrs(*this, p, s, ro);
	}

	template<class t>
	void transfer_object(task<t> *obj) {
		transfer_methods<Alloc>::transfer_object(obj);
	}
};

} // namespace _private

} // namespace work_stealing