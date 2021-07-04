#pragma once

template <class T>
struct CakelispVector
{
	T* data;
	size_t size;
	size_t capacity;

	CakelispVector()
	{
		const size_t c_defaultSize = 8;
		data = (T*)calloc(c_defaultSize, sizeof(T));
		size = 0;
		capacity = initialSize;
	}

	CakelispVector(size_t initialSize)
	{
		data = (T*)calloc(initialSize, sizeof(T));
		size = initialSize;
		capacity = initialSize;
	}

	~CakelispVector()
	{
		for (size_t i = 0; i < size; ++i)
		{
			data[i]->~T();
		}

		free(data);
	}

	ResizeIfNecessary()
	{
		if (size == capacity)
		{
			size_t newCapacity = capacity * 2;
			T* newData = (T*)calloc(newCapacity, sizeof(T));
		}
	}
};
