#pragma once

template<size_t cache_size = 64, class ...Types>
class buffer_size {
	template<class ...tlist>
	struct calc_total_size;
	
	template<class T>
	struct calc_total_size<T> {
		static const constexpr size_t csize = sizeof(T);
	};

	template<>
	struct calc_total_size<> {
		static const constexpr size_t csize = 0;
	};

	template<class T, class ...other>
	struct calc_total_size<T, other...> {
		static const constexpr size_t csize = sizeof(T) + calc_total_size<other...>::csize;
	};

	static constexpr const size_t total_size = calc_total_size<Types...>::csize;

	static constexpr size_t calc_size() {
		return (cache_size - (total_size % cache_size)) % cache_size;
	}

	//since array of size 0 isn't defined by standard
	//use array of size cache_size. slightly less memory effecient but oh well
	static constexpr size_t fix_calc(size_t ss) {
		return ss == 0 ? cache_size : ss;
	}
	constexpr static const size_t bsize = fix_calc(calc_size());
public:
	typedef char buffer[bsize];
};