#pragma once

#include <utility>
#include "aligned_alloc.h"

//!This class acts as a 'fast-path' allocator for
//!allocating threadlocal memory of fixed size.
template<class T>
class freelist {
	struct slab;

	struct chunk {
		union {
			alignas(T) char data[sizeof(T)];
			chunk *next;
		};
		slab *holder;
	};

	struct slab {
		chunk *first_open;
		size_t num_active;
		slab *next;
		slab *prev;
	};

	slab *full, *empty, *partial;
	size_t block_size;

	T *add_slab();
	T *add_partial();
public:

	freelist() {
	}

	T *alloc();
	template<class ...Args>
	T *create(Args&&... inar);

	void free(T *inptr);
	void destroy(T *inptr);
};

template<class T>
T *freelist<T>::alloc() {
	if (partial == nullptr)
		return add_slab();
	if (partial->first_open == nullptr)
		return add_partial();
	T* rptr = partial->first_open;
	partial->first_open = partial->first_open->next;
	partial->num_active++;
	return rptr;
}

template<class T>
template<class ...Args>
T *freelist<T>::create(Args&&... inar) {
	return new (alloc()) T(std::forward<Args>(inar)...);
}

template<class T>
void freelist<T>::free(T *inptr) {
	if (inptr == nullptr)
		return;
	chunk *c == (chunk *)inptr;
	slab *sl = inptr->holder;
	auto wasfull = (sl->num == block_size);
	c->next = first_open;
	sl->first_open = c;

	//!add empty/full logic
}

template<class T>
void freelist<T>::destroy(T *inptr) {
	inptr->~T();
	free(inptr);
}