#include "crucible/error.h"

#include <cstdarg>
#include <iostream>

#include <cxxabi.h>

namespace crucible {
	using namespace std;

	static
	string
	analyze_exception(const exception &e)
	{
		// Let's ignore all the potential memory allocation exceptions for now, K?
		ostringstream oss;

		int status;
		char *realname = abi::__cxa_demangle(typeid(e).name(), 0, 0, &status);
		oss << "exception type ";
		// This is questionable since anything that would cause
		// cxa_demangle to fail will probably cause an exception anyway.
		if (realname) {
			oss << realname;
			free(realname);
		} else {
			oss << typeid(e).name();
		}
		oss << ": " << e.what();
		return oss.str();
	}

	// FIXME:  could probably avoid some of these levels of indirection
	static
	function<void(string s)> current_catch_explainer = [](string s) {
		cerr << s << endl;
	};

	void
	set_catch_explainer(function<void(string s)> f)
	{
		current_catch_explainer = f;
	}

	void
	default_catch_explainer(string s)
	{
		current_catch_explainer(s);
	}

	int
	catch_all(const function<void()> &f, const function<void(string)> &explainer)
	{
		try {
			f();
			return 0;
		} catch (const exception &e) {
			explainer(analyze_exception(e));
			return 1;
		}
	}

	void
	catch_and_explain(const function<void()> &f, const function<void(string)> &explainer)
	{
		try {
			f();
		} catch (const exception &e) {
			explainer(analyze_exception(e));
			throw;
		}
	}

};
