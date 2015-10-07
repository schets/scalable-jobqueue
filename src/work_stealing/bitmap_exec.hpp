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
 * updates and checking of the bitmap
 */
class workmap
{
public:

	workmap()
	{
	}

	~workmap()
	{
	}
};

