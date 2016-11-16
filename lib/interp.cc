#include "crucible/interp.h"

#include "crucible/chatter.h"

namespace crucible {
	using namespace std;

	int
	Proc::exec(const ArgList &args)
	{
		return m_cmd(args);
	}

	Proc::Proc(const function<int(const ArgList &)> &f) :
		m_cmd(f)
	{
	}

	Command::~Command()
	{
	}

	ArgList::ArgList(const char **argv)
	{
		while (argv && *argv) {
			push_back(*argv++);
		}
	}

	ArgList::ArgList(const vector<string> &&that) :
		vector<string>(that)
	{
	}

	Interp::~Interp()
	{
	}

	Interp::Interp(const map<string, shared_ptr<Command> > &cmdlist) :
		m_commands(cmdlist)
	{
	}

	void
	Interp::add_command(const string &name, const shared_ptr<Command> &command)
	{
		m_commands[name] = command;
	}

	int
	Interp::exec(const ArgList &args)
	{
		auto next_arg = args.begin();
		++next_arg;
		return m_commands.at(args[0])->exec(vector<string>(next_arg, args.end()));
	}

	ArgParser::~ArgParser()
	{
	}

	ArgParser::ArgParser()
	{
	}

	void
	ArgParser::add_opt(string opt, ArgActor actor)
	{
		m_string_opts[opt] = actor;
	}

	void
	ArgParser::parse_backend(void *t, const ArgList &args)
	{
		bool quote_args = false;
		for (string arg : args) {
			if (quote_args) {
				cerr << "arg: '" << arg << "'" << endl;
				continue;
			}
			if (arg == "--") {
				quote_args = true;
				continue;
			}
			if (arg.compare(0, 2, "--") == 0) {
				auto found = m_string_opts.find(arg.substr(2, string::npos));
				if (found != m_string_opts.end()) {
					found->second.predicate(t, "foo");
				}
				(void)t;
			}
		}
	}


};
