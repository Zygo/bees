#include "crucible/bytevector.h"

#include "crucible/error.h"
#include "crucible/hexdump.h"
#include "crucible/string.h"

#include <cassert>

namespace crucible {
	using namespace std;

	ByteVector::iterator
	ByteVector::begin() const
	{
		unique_lock<mutex> lock(m_mutex);
		return m_ptr.get();
	}

	ByteVector::iterator
	ByteVector::end() const
	{
		unique_lock<mutex> lock(m_mutex);
		return m_ptr.get() + m_size;
	}

	size_t
	ByteVector::size() const
	{
		return m_size;
	}

	bool
	ByteVector::empty() const
	{
		return !m_ptr || !m_size;
	}

	void
	ByteVector::clear()
	{
		unique_lock<mutex> lock(m_mutex);
		m_ptr.reset();
		m_size = 0;
	}

	ByteVector::value_type&
	ByteVector::operator[](size_t index) const
	{
		unique_lock<mutex> lock(m_mutex);
		return m_ptr.get()[index];
	}

	ByteVector::ByteVector(const ByteVector &that)
	{
		unique_lock<mutex> lock(that.m_mutex);
		m_ptr = that.m_ptr;
		m_size = that.m_size;
	}

	ByteVector&
	ByteVector::operator=(const ByteVector &that)
	{
		// If &that == this, there's no need to do anything, but
		// especially don't try to lock the same mutex twice.
		if (&m_mutex != &that.m_mutex) {
			unique_lock<mutex> lock_this(m_mutex, defer_lock);
			unique_lock<mutex> lock_that(that.m_mutex, defer_lock);
			lock(lock_this, lock_that);
			m_ptr = that.m_ptr;
			m_size = that.m_size;
		}
		return *this;
	}

	ByteVector::ByteVector(const ByteVector &that, size_t start, size_t length)
	{
		THROW_CHECK0(out_of_range, that.m_ptr);
		THROW_CHECK2(out_of_range, start, that.m_size, start <= that.m_size);
		THROW_CHECK2(out_of_range, start + length, that.m_size + length, start + length <= that.m_size + length);
		m_ptr = Pointer(that.m_ptr, that.m_ptr.get() + start);
		m_size = length;
	}

	ByteVector
	ByteVector::at(size_t start, size_t length) const
	{
		return ByteVector(*this, start, length);
	}

	ByteVector::value_type&
	ByteVector::at(size_t size) const
	{
		unique_lock<mutex> lock(m_mutex);
		THROW_CHECK0(out_of_range, m_ptr);
		THROW_CHECK2(out_of_range, size, m_size, size < m_size);
		return m_ptr.get()[size];
	}

	static
	void *
	bv_allocate(size_t size)
	{
#ifdef BEES_VALGRIND
		// XXX: only do this to shut up valgrind
		return calloc(1, size);
#else
		return malloc(size);
#endif
	}

	ByteVector::ByteVector(size_t size)
	{
		m_ptr = Pointer(static_cast<value_type*>(bv_allocate(size)), free);
		// bad_alloc doesn't fit THROW_CHECK's template
		THROW_CHECK0(runtime_error, m_ptr);
		m_size = size;
	}

	ByteVector::ByteVector(iterator begin, iterator end, size_t min_size)
	{
		const size_t size = end - begin;
		const size_t alloc_size = max(size, min_size);
		m_ptr = Pointer(static_cast<value_type*>(bv_allocate(alloc_size)), free);
		THROW_CHECK0(runtime_error, m_ptr);
		m_size = alloc_size;
		memcpy(m_ptr.get(), begin, size);
	}

	bool
	ByteVector::operator==(const ByteVector &that) const
	{
		unique_lock<mutex> lock_this(m_mutex, defer_lock);
		unique_lock<mutex> lock_that(that.m_mutex, defer_lock);
		lock(lock_this, lock_that);
		if (!m_ptr) {
			return !that.m_ptr;
		}
		if (!that.m_ptr) {
			return false;
		}
		if (m_size != that.m_size) {
			return false;
		}
		if (m_ptr.get() == that.m_ptr.get()) {
			return true;
		}
		return !memcmp(m_ptr.get(), that.m_ptr.get(), m_size);
	}

	void
	ByteVector::erase(iterator begin, iterator end)
	{
		unique_lock<mutex> lock(m_mutex);
		const size_t size = end - begin;
		if (!size) return;
		THROW_CHECK0(out_of_range, m_ptr);
		const iterator my_begin = m_ptr.get();
		const iterator my_end = my_begin + m_size;
		THROW_CHECK4(out_of_range, my_begin, begin, my_end, end, my_begin == begin || my_end == end);
		if (begin == my_begin) {
			if (end == my_end) {
				m_size = 0;
				m_ptr.reset();
				return;
			}
			m_ptr = Pointer(m_ptr, end);
		}
		m_size -= size;
	}

	void
	ByteVector::erase(iterator begin)
	{
		erase(begin, begin + 1);
	}

	ByteVector::value_type*
	ByteVector::data() const
	{
		unique_lock<mutex> lock(m_mutex);
		return m_ptr.get();
	}

	ostream&
	operator<<(ostream &os, const ByteVector &bv) {
		hexdump(os, bv);
		return os;
	}
}
