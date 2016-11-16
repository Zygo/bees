#ifndef CRUCIBLE_INTERP_H
#define CRUCIBLE_INTERP_H

#include "crucible/error.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace crucible {
	using namespace std;

	struct ArgList : public vector<string> {
		ArgList(const char **argv);
		// using vector<string>::vector ... doesn't work:
		// error: ‘std::vector<std::basic_string<char> >::vector’ names constructor
		// Still doesn't work in 4.9 because it can't manage a conversion
		ArgList(const vector<string> &&that);
	};

	struct ArgActor {
		struct ArgActorBase {
			virtual void predicate(void *obj, string arg);
		};

		template <class T>
		struct ArgActorDerived {
			function<void(T, string)> m_func;

			ArgActorDerived(decltype(m_func) func) :
				m_func(func)
			{
			}

			void predicate(void *obj, string arg) override
			{
				T &op = *(reinterpret_cast<T*>(obj));
				m_func(op, obj);
			}
		};

		template <class T>
		ArgActor(T, function<void(T, string)> func) :
			m_actor(make_shared(ArgActorDerived<T>(func)))
		{
		}

		ArgActor() = default;

		void predicate(void *t, string arg)
		{
			if (m_actor) {
				m_actor->predicate(t, arg);
			} else {
				THROW_ERROR(invalid_argument, "null m_actor for predicate arg '" << arg << "'");
			}
		}

	private:
		shared_ptr<ArgActorBase> m_actor;
	};

	struct ArgParser {
		~ArgParser();
		ArgParser();

		void add_opt(string opt, ArgActor actor);

		template <class T>
		void
		parse(T t, const ArgList &args)
		{
			void *vt = &t;
			parse_backend(vt, args);
		}
		
	private:
		void parse_backend(void *t, const ArgList &args);
		map<string, ArgActor>	m_string_opts;
	};

	struct Command {
		virtual ~Command();
		virtual int exec(const ArgList &args) = 0;
	};

	struct Proc : public Command {
		int exec(const ArgList &args) override;
		Proc(const function<int(const ArgList &)> &f);
	private:
		function<int(const ArgList &)> m_cmd;
	};

	struct Interp {
		virtual ~Interp();
		Interp(const map<string, shared_ptr<Command> > &cmdlist);
		void add_command(const string &name, const shared_ptr<Command> &command);
		int exec(const ArgList &args);
	private:
		Interp(const Interp &) = delete;
		map<string, shared_ptr<Command> > m_commands;
	};

};
#endif // CRUCIBLE_INTERP_H
