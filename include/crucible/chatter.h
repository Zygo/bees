#ifndef CRUCIBLE_CHATTER_H
#define CRUCIBLE_CHATTER_H

#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <typeinfo>

#include <syslog.h>

  /** \brief Chatter wraps a std::ostream reference with a destructor that
      writes a newline, and inserts timestamp, pid, and tid prefixes on output.

      Typical usage is expressions like the following:

      int six = 6, nine = 9; \n
      Chatter() << "What you get when you multiply" << six
	<< "by" << nine << '?'; \n
      Chatter() << "forty two!";

      which results in output like the following:

      What you get when you multiply 6 by 9 ?\n
      forty-two!

      Note that newlines and timestamps are injected automatically in
      the output by the Chatter destructor.  You can also use std::endl
      explicitly, although it will not have the effect of flushing the
      buffer.
   */

namespace crucible {
	using namespace std;

	class Chatter {
		int m_loglevel;
		string m_name;
		ostream &m_os;
		ostringstream m_oss;

	public:
		Chatter(int loglevel, string name, ostream &os = cerr);
		Chatter(Chatter &&c);
		ostream &get_os() { return m_oss; }

		template <class T> Chatter &operator<<(const T& arg);

		~Chatter();

		static void enable_timestamp(bool prefix_timestamp);
		static void enable_level(bool prefix_level);
	};

	template <class Argument>
	struct ChatterTraits {
		Chatter &operator()(Chatter &c, const Argument &arg)
		{
			c.get_os() << arg;
			return c;
		}
	};

	template <class T>
	Chatter &
	Chatter::operator<<(const T& arg)
	{
		return ChatterTraits<T>()(*this, arg);
	}

	template <class Argument>
	struct ChatterTraits<const Argument *> {
		Chatter &operator()(Chatter &c, const Argument *arg)
		{
			if (arg) {
				c.get_os() << "(pointer to " << typeid(*arg).name() << ")(" << reinterpret_cast<const void *>(arg) << ")";
			} else {
				c.get_os() << "(NULL pointer to " << typeid(arg).name() << ')';
			}
			return c;
		}
	};

	template <>
	struct ChatterTraits<const char *> {
		Chatter &
		operator()(Chatter &c, const char *arg)
		{
			c.get_os() << arg;
			return c;
		}
	};

	/// Per-call-site logging gate, typically stored as a static local variable.
	/// Wraps a source location (file, line, function name) and an enabled/disabled
	/// state.  When enabled, operator<< constructs a Chatter and emits the message.
	/// All ChatterBox instances are registered in a global set, allowing runtime
	/// enable/disable of individual call sites.
	class ChatterBox {
		string m_file;
		int m_line;
		string m_pretty_function;
		bool m_enabled;
		ostream& m_os;

		static set<ChatterBox*> s_boxes;

	public:
		ChatterBox(string file, int line, string pretty_function, ostream &os = cerr);
		~ChatterBox();

		template <class T> Chatter operator<<(const T &t)
		{
			Chatter c(LOG_NOTICE, m_pretty_function, m_os);
			c << t;
			return c;
		}

		/// Return true if this call site is currently enabled.
		bool enabled() const { return m_enabled; }
		/// Enable or disable logging at this call site.
		void set_enable(bool en);

		/// Return the global set of all registered ChatterBox instances.
		static set<ChatterBox*>& all_boxes();
	};

	/// RAII helper that invokes a callback on destruction.
	/// Used with CHATTER_UNWIND to log a message when leaving a scope,
	/// typically to record what was happening when an exception was thrown.
	class ChatterUnwinder {
		function<void()> m_func;
	public:
		ChatterUnwinder(function<void()> f);
		~ChatterUnwinder();
	};
};

/// Log a message at the current call site if the site is enabled.
/// @p x is an expression chain suitable for operator<< on a stream.
/// Example: CHATTER("value is " << v);
#define CHATTER(x) do { \
	using namespace crucible; \
	static ChatterBox crucible_chatterbox_cb(__FILE__, __LINE__, __func__); \
	if (crucible_chatterbox_cb.enabled()) { \
		crucible_chatterbox_cb << x; \
	} \
} while (0)

/// Like CHATTER but prepends "file:line: " to the message.
#define CHATTER_TRACE(x) do { \
	using namespace crucible; \
	static ChatterBox crucible_chatterbox_cb(__FILE__, __LINE__, __func__); \
	if (crucible_chatterbox_cb.enabled()) { \
		crucible_chatterbox_cb << __FILE__ << ":" << __LINE__ << ": " << x; \
	} \
} while (0)

#define WTF_C(x, y) x##y
#define SRSLY_WTF_C(x, y) WTF_C(x, y)
/// Register a CHATTER_TRACE message to be emitted when the current scope exits.
/// Useful for logging context when an exception propagates out of a function.
/// @p x is an expression chain suitable for operator<< on a stream.
#define CHATTER_UNWIND(x) \
	crucible::ChatterUnwinder SRSLY_WTF_C(chatterUnwinder_, __LINE__) ([&]() { \
		CHATTER_TRACE(x); \
	})

#endif // CRUCIBLE_CHATTER_H
