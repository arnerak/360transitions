/*
	Author: Arne-Tobias Rak
	TU Darmstadt
*/

#pragma once

#include <stdexcept>
#include <deque>

template<typename T, size_t s = 1000>
class CircularBuffer
{
public:
	CircularBuffer() { }

	void push(const T& elem)
	{
		buf.push_front(elem);
		while (buf.size() > s)
			buf.pop_back();
	}

	const T& operator[](size_t index) const
	{
		if (index >= buf.size())
			throw std::invalid_argument("CircularBuffer::operator[]: index exceeds bounds");
		
		return buf[index];
	}

	void clear()
	{
		buf.clear();
	}

	int size() const
	{
		return buf.size();
	}

	size_t capacity() const
	{
		return s;
	}

private:
	std::deque<T> buf;
};