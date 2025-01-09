#include "crucible/chatter.h"
#include "crucible/error.h"
#include "crucible/path.h"
#include "crucible/process.h"

#include <cassert>
#include <ctime>
#include <map>
#include <memory>
#include <set>
#include <string>

#include <pthread.h>

namespace crucible {
	using namespace std;

	static shared_ptr<set<string>> chatter_names;
	static const char *SPACETAB = " \t";
	static bool add_prefix_timestamp = true;
	static bool add_prefix_level = true;

	static
	void
	init_chatter_names()
	{
		if (!chatter_names.get()) {
			chatter_names.reset(new set<string>);
			const char *sp = ::getenv("CRUCIBLE_CHATTER");
			if (sp) {
				cerr << "CRUCIBLE_CHATTER = '" << sp << "'" << endl;
				string s(sp);
				while (!s.empty()) {
					s.erase(0, s.find_first_not_of(SPACETAB));
					if (s.empty()) {
						break;
					}
					size_t last = s.find_first_of(SPACETAB);
					string first_word = s.substr(0, last);
					cerr << "\t'" << first_word << "'" << endl;
					chatter_names->insert(first_word);
					s.erase(0, last);
				}
			}
		}
	}

	Chatter::Chatter(int loglevel, string name, ostream &os)
		: m_loglevel(loglevel), m_name(name), m_os(os)
	{
	}

	void
	Chatter::enable_timestamp(bool prefix_timestamp)
	{
		add_prefix_timestamp = prefix_timestamp;
	}

	void
	Chatter::enable_level(bool prefix_level)
	{
		add_prefix_level = prefix_level;
	}

	Chatter::~Chatter()
	{
		ostringstream header_stream;

		if (add_prefix_timestamp) {
			time_t ltime;
			DIE_IF_MINUS_ONE(time(&ltime));
			struct tm ltm;
			DIE_IF_ZERO(localtime_r(&ltime, &ltm));

			char buf[1024];
			DIE_IF_ZERO(strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ltm));

			header_stream << buf;
			header_stream << " " << getpid() << "." << gettid();
			if (add_prefix_level) {
				header_stream << "<" << m_loglevel << ">";
			}
			if (!m_name.empty()) {
				header_stream << " " << m_name;
			}
		} else {
			if (add_prefix_level) {
				header_stream << "<" << m_loglevel << ">";
			}
			header_stream << (m_name.empty() ? "thread" : m_name);
			header_stream << "[" << gettid() << "]";
		}

		header_stream << ": ";

		string out = m_oss.str();
		string header = header_stream.str();

		string::size_type start = 0;
		while (start < out.size()) {
			size_t end_line = out.find_first_of("\n", start);
			if (end_line != string::npos) {
				assert(out[end_line] == '\n');
				size_t end = end_line;
				m_os << (header + out.substr(start, end - start) + "\n") << flush;
				start = end_line + 1;
			} else {
				m_os << (header + out.substr(start) + "\n") << flush;
				start = out.size();
			}
		}
	}

	Chatter::Chatter(Chatter &&c)
		: m_loglevel(c.m_loglevel), m_name(c.m_name), m_os(c.m_os), m_oss(c.m_oss.str())
	{
		c.m_oss.str("");
	}

	set<ChatterBox*> ChatterBox::s_boxes;

	set<ChatterBox*>& ChatterBox::all_boxes()
	{
		return s_boxes;
	}

	ChatterBox::ChatterBox(string file, int line, string pretty_function, ostream &os)
		: m_file(basename(file)), m_line(line), m_pretty_function(pretty_function), m_enabled(false), m_os(os)
	{
		s_boxes.insert(this);
		init_chatter_names();
		if (chatter_names->find(m_file) != chatter_names->end()) {
			m_enabled = true;
		} else if (chatter_names->find(m_pretty_function) != chatter_names->end()) {
			m_enabled = true;
		} else if (!chatter_names->empty()) {
			cerr << "CRUCIBLE_CHATTER does not list '" << m_file << "' or '" << m_pretty_function << "'" << endl;
		}
		(void)m_line; // not implemented yet
		// cerr << "ChatterBox " << reinterpret_cast<void*>(this) << " constructed" << endl;
	}

	ChatterBox::~ChatterBox()
	{
		s_boxes.erase(this);
		// cerr << "ChatterBox " << reinterpret_cast<void*>(this) << " destructed" << endl;
	}

	void
	ChatterBox::set_enable(bool en)
	{
		m_enabled = en;
	}

	ChatterUnwinder::ChatterUnwinder(function<void()> f) :
		m_func(f)
	{
	}

	ChatterUnwinder::~ChatterUnwinder()
	{
		if (current_exception()) {
			m_func();
		}
	}

};
