#pragma once

//!This file contains the global allocator used for
//!allocating toplevel forkjoin-tasks
//!is a simple lockfree stack, expect there to be low contention

//can merge with page allocator? very similar code