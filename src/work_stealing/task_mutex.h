#include <atomic>

namespace work_stealing {
	class mutex {
		std::atomic<bool> is_held;
	};
}
