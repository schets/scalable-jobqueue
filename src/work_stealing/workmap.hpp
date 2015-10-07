#pragma once

#include <atomic>

//This is used instead of a set of chars
//so that each one can be effectively updated
//unfortunately, the standard doesn't promise that
//sizeof std::atomic<char> is the same as
//std:;atomic<uint64_t> or whatever,
//so bitmaps it is.

/**
 * This class examines which elements of a bitmap are active
 * and takes the proper action for each flag. It allows atomic
 * updates and checking of the bitmap.
 * 
 * The tasks that are most likely should be given lower indices
 */

template<size_t task_ind>
class workmap_task {
public:
	//flag to fail!
	using dont_make_me_a_member = int;
};

template<class C, class Q = C::dont_make_me_a_member>
class check_fnc {
public:
	static constexpr bool is_good = false;
};

template<class C>
class check_fnc {
public:
	static constexpr bool is_good = true;
};


//Every single task up to and including nn-1 must have an associated
//function, that accepts the passed reference type
template<size_t nn, size_t check_every = 4>
class workmap {

	static_assert(nn < 64, "64 tasks is the limit");
	static_assert(nn > 0, "Must have a positive number of tasks to work on");

	template<size_t n>
	class verify_tasks {
		static_assert(check_fnc<workmap_task<n>>::is_good,
					  "Task failed to instantiate, is not densely packed");
		verify_tasks<n + 1> dummy;
	};

	template<>
	class verify_tasks<nn - 1> {
		static_assert(check_fnc<workmap_task<n>>::is_good,
					  "Task failed to instantiate, is not densely packed");
	};

	static constexpr uint64_t zero_n_bits(size_t up_to) {
		static constexpr uint64_t all_1 = ~0;
		return (all_1 << up_to) >> up_to;
	}

	std::atomic<uint64_t> blob;

	template<size_t off>
	void exec(void *inv) {
		workmap_task<off>::work(inv);
	}

	template<size_t off>
	static void operate_on(uint64_t cval, void *b) {

		if ((off % check_every) == 0) {
			cval &= zero_n_bits(off);
			if (cval == 0) {
				return;
			}
		}

		if (cval & (1 << off)) {
			exec<off>(b);
		}

		operate_on<off + 1>(cval, b);
	}

	template<>
	static void operate_on<nn - 1>(uint64_t cval, void *b) {
		if (cval & (1 << (nn - 1))) {
			exec<nn - 1>(b);
		}
	}
public:

	template<size_t which>
	void set_flag(std::memory_order ord = std::memory_order_relaxed) {
		static_assert(which < nn, "Requested flag number is too high");
		static_assert(which > 0, "This shouldn't ever go...");

		constexpr howhigh = 1 << which;
		if (blob.load(std::memory_order_relaxed) & which) {
			return;
		}

		//set flag
		blob.fetch_or(which, ord);
	}

	void acquire_tasks() {
		while (blob.load(std::memory_order_consume)) {
			uint64_t ctasks = blob.exchange(0, std::memory_order_acquire);
			operate_on(ctasks);
		}
	}
};
