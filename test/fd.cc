// TEST DATA DO NOT REMOVE THIS LINE

#include "tests.h"

#include "crucible/chatter.h"
#include "crucible/error.h"
#include "crucible/fd.h"

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <ios>
#include <map>
#include <string>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace crucible;

static
void
test_default_constructor_and_destructor()
{
	Fd f;
}

static
void
test_basic_read()
{
	Fd f = open_or_die("fd.cc");
	const char test_string[] = "// TEST DATA DO NOT REMOVE THIS LINE";
	const int test_string_len = sizeof(test_string) - 1;
	char read_buf[test_string_len];
	read_or_die(f, read_buf);
	assert(!strncmp(read_buf, test_string, test_string_len));
	f->close();
}

static
void
test_create_read_write()
{
	Fd f = open_or_die("tmp/fd-read-write", O_CREAT | O_RDWR | O_TRUNC);

	struct test_str_out {
		int i;
		float f;
	} tso = {
		.i = 5,
		.f = 3.14159,
	}, tsi = {
		.i = 0,
		.f = 0,
	};

	size_t bytes_read = 0;
	read_partial_or_die(f, tsi, bytes_read);
	assert(bytes_read == 0);
	assert(tsi.i == 0);
	assert(tsi.f == 0);

	pwrite_or_die(f, tso, 1024);
	pread_or_die(f, tsi, 1024);
	assert(!memcmp(&tsi, &tso, sizeof(tsi)));

}

static
void
test_flags()
{
#define FLAG_TEST(x) cerr << #x << ": " << flush; cerr << x << endl;

	FLAG_TEST(o_flags_ntoa(O_RDONLY));
	FLAG_TEST(o_flags_ntoa(O_WRONLY));
	FLAG_TEST(o_flags_ntoa(O_RDWR));
	FLAG_TEST(o_flags_ntoa(O_CREAT|O_WRONLY|O_TRUNC));
	FLAG_TEST(o_mode_ntoa(0001));
	FLAG_TEST(o_mode_ntoa(0002));
	FLAG_TEST(o_mode_ntoa(0004));
	FLAG_TEST(o_mode_ntoa(0010));
	FLAG_TEST(o_mode_ntoa(0020));
	FLAG_TEST(o_mode_ntoa(0040));
	FLAG_TEST(o_mode_ntoa(0100));
	FLAG_TEST(o_mode_ntoa(0200));
	FLAG_TEST(o_mode_ntoa(0400));
	FLAG_TEST(o_mode_ntoa(01000));
	FLAG_TEST(o_mode_ntoa(02000));
	FLAG_TEST(o_mode_ntoa(04000));
	FLAG_TEST(o_mode_ntoa(010000));
	FLAG_TEST(o_mode_ntoa(020000));
	FLAG_TEST(o_mode_ntoa(040000));
	FLAG_TEST(o_mode_ntoa(0777));
	FLAG_TEST(o_mode_ntoa(02775));
	FLAG_TEST(o_mode_ntoa(01777));
	FLAG_TEST(o_mode_ntoa(022));
	FLAG_TEST(o_mode_ntoa(077));
}

// Test code
namespace crucible {
	extern bool assert_no_leaked_fds();
};

struct FdChecker {
	~FdChecker()
	{
		assert_no_leaked_fds();
	}
};

static FdChecker fd_destructor_check;

static inline void assert_is_closed(int i, bool closed = true)
{
	pid_t self_pid = getpid();
	char buf[1024];
	snprintf(buf, sizeof(buf), "/proc/%d/fd/%d", self_pid, i);
	assert(access(buf, F_OK) ? closed : !closed);
}

static void test_construct_destroy()
{
	int i;
	{
		Fd fd(open("fd.cc", O_RDONLY));
		i = fd;
	}
	assert_is_closed(i);
}

static void test_construct_copy()
{
	int i;
	{
		Fd fd(open("fd.cc", O_RDONLY));
		i = fd;
		Fd fd2(fd);
		int j = fd2;
		assert(i == j);
	}
	assert_is_closed(i);
}

static void test_construct_default_assign()
{
	int i;
	{
		i = open("fd.cc", O_RDONLY);
		Fd fd;
		fd = i;
		Fd fd2;
		fd2 = fd;
		int j = fd2;
		assert(i == j);
	}
	assert_is_closed(i);
}

static void test_assign_int()
{
	int i;
	{
		i = open("fd.cc", O_RDONLY);
		Fd fd;
		fd = i;
		Fd fd2;
		fd2 = i;
		int j = fd2;
		assert(i == j);
	}
	assert_is_closed(i);
}

static void test_assign_int_survives_scope()
{
	int i, j;
	{
		Fd fd2;
		{
			i = open("fd.cc", O_RDONLY);
			Fd fd;
			fd = i;
			fd2 = i;
			j = fd2;
			assert(i == j);
		}
		assert_is_closed(i, false);
	}
	assert_is_closed(i, true);
}

static void test_assign_int_close()
{
	int i;
	{
		Fd fd(open("fd.cc", O_RDONLY));
		i = fd;
		assert_is_closed(i, false);
		fd = -1;
		assert_is_closed(i, true);
		int j = fd;
		assert(j == -1);
		// Bonus conversion operator tests
		assert(fd == -1);
		// Chasing a closed ref no longer triggers an exception
		assert(fd->get_fd() == -1);
	}
	assert_is_closed(i, true);
}

static void test_assign_int_close_2()
{
	int i;
	{
		Fd fd(open("fd.cc", O_RDONLY));
		i = fd;
		assert_is_closed(i, false);
		// -2 is null...
		fd = -2;
		assert_is_closed(i, true);
		int j = fd;
		// ...but it will come back as -1
		assert(j == -1);
		// Bonus conversion operator tests
		assert(fd == -1);
		// Chasing a closed ref no longer triggers an exception
		assert(fd->get_fd() == -1);
	}
	assert_is_closed(i, true);
}

static void test_map()
{
	int a, b, c;
	map<string, Fd> fds;
	{
		Fd fd_dot_cc = open("fd.cc", O_RDONLY);
		a = fd_dot_cc;
		assert_is_closed(a, false);
		Fd fd_tests_h = open("tests.h", O_RDONLY);
		b = fd_tests_h;
		assert_is_closed(b, false);
		Fd fd_makefile = open("Makefile", O_RDONLY);
		c = fd_makefile;
		assert_is_closed(c, false);
		fds["fd.cc"] = fd_dot_cc;
		fds.insert(make_pair("tests.h", fd_tests_h));
		int j = fds["Makefile"];
		assert(j == -1);
		fds["Makefile"] = fd_makefile;
		assert_is_closed(a, false);
		assert_is_closed(b, false);
		assert_is_closed(c, false);
	}
	assert_is_closed(a, false);
	assert_is_closed(b, false);
	assert_is_closed(c, false);
}

static void test_close_method()
{
	Fd fd = open("fd.cc", O_RDONLY);
	int i = fd;
	assert_is_closed(i, false);
	fd->close();
	assert_is_closed(i, true);
}

static void test_shared_close_method()
{
	Fd fd = open("fd.cc", O_RDONLY);
	int i = fd;
	Fd fd2 = fd;
	assert_is_closed(i, false);
	assert_is_closed(fd2, false);
	fd->close();
	assert_is_closed(i, true);
	assert_is_closed(fd2, true);
}

struct DerivedFdResource : public Fd::resource_type {
	string	m_name;
	DerivedFdResource(string name) : Fd::resource_type(open(name.c_str(), O_RDONLY)), m_name(name) {
		assert_is_closed(this->get_fd(), false);
	}
	const string &name() const { return m_name; }
};

template<class T>
shared_ptr<T>
cast(const Fd &fd)
{
	auto dp = dynamic_pointer_cast<T>(fd.operator->());
	if (!dp) {
		throw bad_cast();
	}
	return dp;
}

struct DerivedFd : public Fd {
	using resource_type = DerivedFdResource;
	DerivedFd(string name) {
		shared_ptr<DerivedFdResource> ptr = make_shared<DerivedFdResource>(name);
		Fd::operator=(static_pointer_cast<Fd::resource_type>(ptr));
	}
	shared_ptr<DerivedFdResource> operator->() const {
		shared_ptr<DerivedFdResource> rv = cast<DerivedFdResource>(*this);
		THROW_CHECK1(out_of_range, rv, rv);
		return rv;
	}
private:
	DerivedFd() = default;
};

static void test_derived_resource_type()
{
	DerivedFd fd("fd.cc");
	assert_is_closed(fd, false);
	assert(fd->name() == "fd.cc");
	DerivedFd fd3(fd);
	assert_is_closed(fd, false);
	assert_is_closed(fd3, false);
	Fd fd2(fd3);
	assert_is_closed(fd, false);
	assert_is_closed(fd2, false);
	assert_is_closed(fd3, false);
}

static void test_derived_cast()
{
	DerivedFd fd("fd.cc");
	assert_is_closed(fd, false);
	Fd fd2(fd);
	Fd fd3 = open("fd.cc", O_RDONLY);
	assert(fd->name() == "fd.cc");
	assert(cast<Fd::resource_type>(fd));
	assert(cast<DerivedFd::resource_type>(fd));
	assert(cast<Fd::resource_type>(fd2));
	assert(cast<DerivedFd::resource_type>(fd2));
	assert(cast<Fd::resource_type>(fd3));
	assert(catch_all([&](){ assert(!cast<DerivedFd::resource_type>(fd3)); } ));
}

static void test_derived_map()
{
	int a, b, c;
	map<string, Fd> fds;
	{
		DerivedFd fd_dot_cc("fd.cc");
		a = fd_dot_cc;
		assert_is_closed(a, false);
		Fd fd_tests_h = open("tests.h", O_RDONLY);
		b = fd_tests_h;
		assert_is_closed(b, false);
		DerivedFd fd_makefile("Makefile");
		c = fd_makefile;
		assert_is_closed(c, false);
		fds["fd.cc"] = fd_dot_cc;
		fds.insert(make_pair("tests.h", fd_tests_h));
		int j = fds["Makefile"];
		assert(j == -1);
		fds["Makefile"] = fd_makefile;
		assert_is_closed(a, false);
		assert_is_closed(b, false);
		assert_is_closed(c, false);
	}
	assert_is_closed(a, false);
	assert_is_closed(b, false);
	assert_is_closed(c, false);
}

int main(int, const char **)
{

	RUN_A_TEST(test_default_constructor_and_destructor());
	RUN_A_TEST(test_basic_read());
	RUN_A_TEST(test_create_read_write());

	RUN_A_TEST(test_flags());

	RUN_A_TEST(test_construct_destroy());
	RUN_A_TEST(test_construct_copy());
	RUN_A_TEST(test_construct_default_assign());
	RUN_A_TEST(test_assign_int());
	RUN_A_TEST(test_assign_int_survives_scope());
	RUN_A_TEST(test_assign_int_close());
	RUN_A_TEST(test_assign_int_close_2());
	RUN_A_TEST(test_map());
	RUN_A_TEST(test_close_method());
	RUN_A_TEST(test_shared_close_method());
	RUN_A_TEST(test_derived_resource_type());
	RUN_A_TEST(test_derived_map());
	RUN_A_TEST(test_derived_cast());

	assert_no_leaked_fds();

	return 0;
}
