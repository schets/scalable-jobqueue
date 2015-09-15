#pragma once

#include <memory>

namespace work_stealing {
namespace _private {
template<class T>
class shared_array {

	//must put size info in del so it can be swapped
	//atomically with the ptr
	struct my_del {
		size_t size;
		void operator (void *todel) {
			delete[] todel;
		}
	};
	std::shared_ptr<T> myptr;
public:

	shared_array() = default;
	shared_array(size_t s) {
		mydel del;
		del.size = s;
		myptr = std::shared_ptr(new T[s], del);
	}

	size_t size() {
		return std::get_deleter<my_del, T>(del).size;
	}

	T *get() {
		return myptr.get();
	}

	shared_array load(std::memory_order ord = std::memory_order_relaxed) const {
		shared_array rval;
		rval.myptr = std::atomic_load_explicit(myptr, ord);
		return rval;
	}

	void store(const shared_array& ar,
			   std::memory_order ord = std::memory_order_relaxed) {
		std::atomic_store_explicit(&myptr, ar, ord);
	}
};

} // namespace _private
} // namespace work_stealing