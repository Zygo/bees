#include "tests.h"
#include "crucible/error.h"
#include "crucible/namedptr.h"

#include <cassert>
#include <set>

using namespace crucible;

struct named_thing {
	static set<named_thing*> s_set;
	int m_a, m_b;
	named_thing() = delete;
	named_thing(const named_thing &that) :
		m_a(that.m_a),
		m_b(that.m_b)
	{
		cerr << "named_thing(" << m_a << ", " << m_b << ") " << this << " copied from " << &that << "." << endl;
		auto rv = s_set.insert(this);
		THROW_CHECK1(runtime_error, *rv.first, rv.second);
	}
	named_thing(int a, int b) :
		m_a(a), m_b(b)
	{
		cerr << "named_thing(" << a << ", " << b << ") " << this << " constructed." << endl;
		auto rv = s_set.insert(this);
		THROW_CHECK1(runtime_error, *rv.first, rv.second);
	}
	~named_thing() {
		auto rv = s_set.erase(this);
		assert(rv == 1);
		cerr << "named_thing(" << m_a << ", " << m_b << ") " << this << " destroyed." << endl;
		m_a = ~m_a;
		m_b = ~m_b;
	}
	void check(int a, int b) {
		THROW_CHECK2(runtime_error, m_a, a, m_a == a);
		THROW_CHECK2(runtime_error, m_b, b, m_b == b);
	}
	static void check_empty() {
		THROW_CHECK1(runtime_error, s_set.size(), s_set.empty());
	}
};

set<named_thing*> named_thing::s_set;

static
void
test_namedptr()
{
	NamedPtr<named_thing, int, int> names;
	names.func([](int a, int b) -> shared_ptr<named_thing> { return make_shared<named_thing>(a, b); });

	auto a_3_5 = names(3, 5);
	auto b_3_5 = names(3, 5);
	{
		auto c_2_7 = names(2, 7);
		b_3_5 = a_3_5;
		a_3_5->check(3, 5);
		b_3_5->check(3, 5);
		c_2_7->check(2, 7);
	}
	auto d_2_7 = names(2, 7);
	a_3_5->check(3, 5);
	a_3_5.reset();
	b_3_5->check(3, 5);
	d_2_7->check(2, 7);
}

static
void
test_leak()
{
	named_thing::check_empty();
}

int
main(int, char**)
{
	RUN_A_TEST(test_namedptr());
	RUN_A_TEST(test_leak());

	exit(EXIT_SUCCESS);
}
