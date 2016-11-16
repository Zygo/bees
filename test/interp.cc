#include "tests.h"

#include "crucible/interp.h"

using namespace crucible;
using namespace std;

/***********************************************************************

How this should work:

Interpreter reads an arg list:

	argv[0] --method0args --method1arg arg1 --method1arg=arg1 -- args...

argv[0] should look up a shared_ptr<Command> which creates an object of
type shared_ptr<Process>.  This object is used to receive args by
method calls or one at a time.

<Command> and <Process> can be the same object, or not.

Process p methods:

	p->spawn(Interp*) -> Process
	p->exec(ArgList) -> Process / Result
	p->method (from ArgParser<>)
		p->finish() -> void (destroys object without early destruction warnings...?)
		p->~Process() -> complains loudly if finish() not called first...?

Result might be a pair of Process, string.  Or just string.

ArgParser should be more like GetOpt:

	build a dictionary and an arg list from arguments
	Process methods should interrogate ArgParser
	ArgParser might have a table of boolean and string option names so it can reject invalid options
		but if it had that, we could also pass in Process and have it call methods on it
		...but that is a _lot_ of pointer-hiding when we could KISS
		...but if we had that solved, argparser tables look like lists of method names
	ArgParser<T> has a table of names and methods on object of type T
		ArgParser hides everything behind void* and hands off to a compiled implementation to do callbacks

Extreme simplification:  arguments are themselves executable

	so '--method_foo arg' really means construct MethodFoo(arg) and cast to shared_ptr<ProcArg>
	then Process->invokeSomething(ProcArg)
	too extreme, use argparser instead

***********************************************************************/

void
test_arg_parser()
{
	ArgParser ap;
	ArgList al( { "abc", "--def", "ghi" } );
	ap.parse(NULL, al);
}

struct Thing {
	int m_i;
	double m_d;
	string m_s;

	void set_i(int i) { cerr << "i = " << i << endl; m_i = i; }
	void set_d(double d) { cerr << "d = " << d << endl; m_d = d; }
	void set_s(string s) { cerr << "s = " << s << endl; m_s = s; }
};

template <typename F, typename T, typename A>
void
assign(T& t, F f, A a)
{
	cerr << __PRETTY_FUNCTION__ << " - a = " << a << endl;
	(t.*f)(a);
}

int
main(int, char**)
{
	RUN_A_TEST(test_arg_parser());

	Thing p;
	assign(p, &Thing::set_i, 5);

	cerr << "p.m_i = " << p.m_i << endl;

	exit(EXIT_SUCCESS);
}
