#ifndef _CRUCIBLE_BYTEVECTOR_H_
#define _CRUCIBLE_BYTEVECTOR_H_

#include <crucible/error.h>

#include <memory>
#include <mutex>
#include <ostream>

#include <cstdint>
#include <cstdlib>

namespace crucible {
	using namespace std;
	// new[] is a little slower than malloc
	// shared_ptr is about 2x slower than unique_ptr
	// vector<uint8_t> is ~160x slower
	// so we won't bother with unique_ptr because we can't do shared copies with it

	class ByteVector {
	public:
		using Pointer = shared_ptr<uint8_t>;
		using value_type = Pointer::element_type;
		using iterator = value_type*;

		ByteVector() = default;
		ByteVector(const ByteVector &that);
		ByteVector& operator=(const ByteVector &that);
		ByteVector(size_t size);
		ByteVector(const ByteVector &that, size_t start, size_t length);
		ByteVector(iterator begin, iterator end, size_t min_size = 0);

		ByteVector at(size_t start, size_t length) const;

		value_type& at(size_t) const;
		iterator begin() const;
		void clear();
		value_type* data() const;
		bool empty() const;
		iterator end() const;
		value_type& operator[](size_t) const;
		size_t size() const;
		bool operator==(const ByteVector &that) const;

		// this version of erase only works at the beginning or end of the buffer, else throws exception
		void erase(iterator first);
		void erase(iterator first, iterator last);

		// An important use case is ioctls that have a fixed-size header struct
		// followed by a buffer for further arguments.  These templates avoid
		// doing reinterpret_casts every time.
		template <class T> ByteVector(const T& object, size_t min_size);
		template <class T> T* get() const;
	private:
		Pointer m_ptr;
		size_t m_size = 0;
		mutable mutex m_mutex;
	};

	template <class T>
	ByteVector::ByteVector(const T& object, size_t min_size)
	{
		const auto size = max(min_size, sizeof(T));
		m_ptr = Pointer(static_cast<value_type*>(malloc(size)), free);
		memcpy(m_ptr.get(), &object, sizeof(T));
		m_size = size;
	}

	template <class T>
	T*
	ByteVector::get() const
	{
		THROW_CHECK2(out_of_range, size(), sizeof(T), size() >= sizeof(T));
		return reinterpret_cast<T*>(data());
	}

	ostream& operator<<(ostream &os, const ByteVector &bv);
}

#endif // _CRUCIBLE_BYTEVECTOR_H_
