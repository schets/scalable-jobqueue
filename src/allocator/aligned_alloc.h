#pragma once
namespace forkjoin {
	void *al_alloc(size_t size, size_t align);
	void al_free(void *tofree);
};
