#ifndef CRUCIBLE_SPANNER_H
#define CRUCIBLE_SPANNER_H

#include "crucible/error.h"

#include <memory>

namespace crucible {

	using namespace std;

	// C++20 is already using the name "span" for something similar.
	template <class T, class Head = T*, class Iter = Head>
	class Spanner {
	public:
		using iterator = Iter;
		using head_pointer = Head;
		using value_type = T;

		template <class Container>
		Spanner(Container& container);

		Spanner(head_pointer begin, iterator end);
		Spanner(size_t size, head_pointer begin);
		Spanner() = default;
		Spanner &operator=(const Spanner &that) = default;
		iterator begin() const;
		iterator end() const;
		value_type *data() const;
		value_type &at(size_t n) const;
		size_t size() const;
		bool empty() const;
		void clear();
		value_type &operator[](size_t n) const;
		iterator erase(iterator first, iterator last);
		iterator erase(iterator first);
	private:
		head_pointer	m_begin;
		size_t		m_size;
	};

	template <class Container, class Head = typename Container::value_type *, class Iter = Head>
	Spanner<typename Container::value_type, Head, Iter> make_spanner(Container &container)
	{
		return Spanner<typename Container::value_type, Head, Iter>(container);
	}

	// This template is an attempt to turn a shared_ptr to a container
	// into a range view that can be cheaply passed around.
	// It probably doesn't quite work in the general case.
	template <class Container, class Head = shared_ptr<typename Container::value_type>, class Iter = typename Container::value_type *>
	Spanner<typename Container::value_type, Head, Iter> make_spanner(shared_ptr<Container> &cont_ptr)
	{
		shared_ptr<typename Container::value_type> head(cont_ptr, cont_ptr->data());
		size_t const size = cont_ptr->size();
		return Spanner<typename Container::value_type, Head, Iter>(size, head);
	}

	template <class T, class Head, class Iter>
	template <class Container>
	Spanner<T, Head, Iter>::Spanner(Container &container) :
		m_begin(container.data()),
		m_size(container.size())
	{
	}

	template <class T, class Head, class Iter>
	Spanner<T, Head, Iter>::Spanner(head_pointer begin, iterator end) :
		m_begin(begin),
		m_size(end - begin)
	{
	}

	template <class T, class Head, class Iter>
	Spanner<T, Head, Iter>::Spanner(size_t size, head_pointer begin) :
		m_begin(begin),
		m_size(size)
	{
	}

	template <class T, class Head, class Iter>
	typename Spanner<T, Head, Iter>::iterator
	Spanner<T, Head, Iter>::erase(iterator first, iterator last)
	{
		auto end = m_begin + m_size;
		if (first == m_begin) {
			THROW_CHECK0(invalid_argument, last <= end);
			m_begin = last;
			return last;
		}
		if (last == end) {
			THROW_CHECK0(invalid_argument, m_begin <= first);
			m_size = first - m_begin;
			return first;
		}
		THROW_ERROR(invalid_argument, "first != begin() and last != end()");
	}

	template <class T, class Head, class Iter>
	typename Spanner<T, Head, Iter>::iterator
	Spanner<T, Head, Iter>::erase(iterator first)
	{
		return erase(first, first + 1);
	}

	template <class T, class Head, class Iter>
	typename Spanner<T, Head, Iter>::value_type &
	Spanner<T, Head, Iter>::operator[](size_t n) const
	{
		return at(n);
	}

	template <class T, class Head, class Iter>
	void
	Spanner<T, Head, Iter>::clear()
	{
		m_begin = head_pointer();
		m_size = 0;
	}

	template <class T, class Head, class Iter>
	bool
	Spanner<T, Head, Iter>::empty() const
	{
		return m_size == 0;
	}

	template <class T, class Head, class Iter>
	size_t
	Spanner<T, Head, Iter>::size() const
	{
		return m_size;
	}

	template <class T, class Head, class Iter>
	typename Spanner<T, Head, Iter>::value_type *
	Spanner<T, Head, Iter>::data() const
	{
		return &(*m_begin);
	}

	template <class T, class Head, class Iter>
	typename Spanner<T, Head, Iter>::iterator
	Spanner<T, Head, Iter>::begin() const
	{
		return data();
	}

	template <class T, class Head, class Iter>
	typename Spanner<T, Head, Iter>::iterator
	Spanner<T, Head, Iter>::end() const
	{
		return data() + m_size;
	}

	template <class T, class Head, class Iter>
	typename Spanner<T, Head, Iter>::value_type &
	Spanner<T, Head, Iter>::at(size_t n) const
	{
		THROW_CHECK2(out_of_range, n, size(), n < size());
		return *(data() + n);
	}

}


#endif // CRUCIBLE_SPANNER_H
