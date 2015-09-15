#pragma once
#include <type_traits>
#include <limits>
template<class T>
class num_bits {
	static_assert(std::is_integral<T>::value, "Type must be integral type");

};

