#ifndef CRUCIBLE_CHATTER_H
#define CRUCIBLE_CHATTER_H

#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <typeinfo>

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
		string m_name;
		ostream &m_os;
		ostringstream m_oss;

	public:
		Chatter(string name, ostream &os = cerr);
		Chatter(Chatter &&c);
		ostream &get_os() { return m_oss; }

		template <class T> Chatter &operator<<(const T& arg);

		~Chatter();
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
			Chatter c(m_pretty_function, m_os);
			c << t;
			return c;
		}

		bool enabled() const { return m_enabled; }
		void set_enable(bool en);

		static set<ChatterBox*>& all_boxes();
	};

	class ChatterUnwinder {
		function<void()> m_func;
	public:
		ChatterUnwinder(function<void()> f);
		~ChatterUnwinder();
	};
};

#define CHATTER(x) do { \
	using namespace crucible; \
	static ChatterBox crucible_chatterbox_cb(__FILE__, __LINE__, __func__); \
	if (crucible_chatterbox_cb.enabled()) { \
		crucible_chatterbox_cb << x; \
	} \
} while (0)

#define CHATTER_TRACE(x) do { \
	using namespace crucible; \
	static ChatterBox crucible_chatterbox_cb(__FILE__, __LINE__, __func__); \
	if (crucible_chatterbox_cb.enabled()) { \
		crucible_chatterbox_cb << __FILE__ << ":" << __LINE__ << ": " << x; \
	} \
} while (0)

#define WTF_C(x, y) x##y
#define SRSLY_WTF_C(x, y) WTF_C(x, y)
#define CHATTER_UNWIND(x) \
	crucible::ChatterUnwinder SRSLY_WTF_C(chatterUnwinder_, __LINE__) ([&]() { \
		CHATTER_TRACE(x); \
	})

#endif // CRUCIBLE_CHATTER_H
