/*
	Author: Arne-Tobias Rak
	TU Darmstadt
*/

#pragma once

#include <cstdint>

class IStream
{
public:
	virtual int read(char* buf, int buf_size) = 0;
	virtual int64_t seek(int64_t offset, int whence) = 0;
};