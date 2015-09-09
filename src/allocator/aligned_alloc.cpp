#include "aligned_alloc.h"
#include <stdlib.h>
//if only microsoft properly support C11 then could just be like
//aligned alloc yay

#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
#include "unistd.h"
#if defined(_POSIX_VERSION) && (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600)

namespace forkjoin {
	void *al_alloc(size_t size, size_t alignment) {
		void *rval;
		posix_memalign(&rval, alignment, size);
		return rval;
	}

	void al_free(void *tofree) {
		free(tofree);
	}
}
//try for c11, what little hope there is
#else
#define USE_C11_CODE
#endif
#elif defined(_MSC_VER)
namespace forkjoin {
#include <malloc.h>
	void *al_alloc(size_t size, size_t alignment) {
		return _aligned_malloc(size, alignment);
	}

	void al_free(void *tofree) {
		_aligned_free(tofree);
	}
}
#else
#define USE_C11_CODE
#endif

#ifdef USE_C11_CODE
namespace forkjoin {
	void *al_alloc(size_t size, size_t alignment) {
		return aligned_alloc(size, alignment);
	}

	void al_free(void *tofree) {
		free(tofree);
	}
}
#endif