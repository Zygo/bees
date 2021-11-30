#include "bees.h"

// tracing ----------------------------------------

int bees_log_level = 8;

thread_local BeesTracer *BeesTracer::tl_next_tracer = nullptr;
thread_local bool BeesTracer::tl_first = true;
thread_local bool BeesTracer::tl_silent = false;

#if __cplusplus >= 201703
static
bool
exception_check()
{
	return uncaught_exceptions();
}
#else
static
bool
exception_check()
{
	return uncaught_exception();
}
#endif

BeesTracer::~BeesTracer()
{
	if (!tl_silent && exception_check()) {
		if (tl_first) {
			BEESLOGNOTICE("--- BEGIN TRACE --- exception ---");
			tl_first = false;
		}
		try {
			m_func();
		} catch (exception &e) {
			BEESLOGNOTICE("Nested exception: " << e.what());
		} catch (...) {
			BEESLOGNOTICE("Nested exception ...");
		}
		if (!m_next_tracer) {
			BEESLOGNOTICE("---  END  TRACE --- exception ---");
		}
	}
	tl_next_tracer = m_next_tracer;
	if (!m_next_tracer) {
		tl_silent = false;
		tl_first = true;
	}
}

BeesTracer::BeesTracer(function<void()> f, bool silent) :
	m_func(f)
{
	m_next_tracer = tl_next_tracer;
	tl_next_tracer = this;
	tl_silent = silent;
}

void
BeesTracer::trace_now()
{
	BeesTracer *tp = tl_next_tracer;
	BEESLOGNOTICE("--- BEGIN TRACE ---");
	while (tp) {
		tp->m_func();
		tp = tp->m_next_tracer;
	}
	BEESLOGNOTICE("---  END  TRACE ---");
}

bool
BeesTracer::get_silent()
{
	return tl_silent;
}

void
BeesTracer::set_silent()
{
	tl_silent = true;
}

thread_local BeesNote *BeesNote::tl_next = nullptr;
mutex BeesNote::s_mutex;
map<pid_t, BeesNote*> BeesNote::s_status;
thread_local string BeesNote::tl_name;

BeesNote::~BeesNote()
{
	tl_next = m_prev;
	unique_lock<mutex> lock(s_mutex);
	if (tl_next) {
		s_status[crucible::gettid()] = tl_next;
	} else {
		s_status.erase(crucible::gettid());
	}
}

BeesNote::BeesNote(function<void(ostream &os)> f) :
	m_func(f)
{
	m_name = get_name();
	m_policy = get_policy();
	m_prev = tl_next;
	tl_next = this;
	unique_lock<mutex> lock(s_mutex);
	s_status[crucible::gettid()] = tl_next;
}

void
BeesNote::set_name(const string &name)
{
	tl_name = name;
	catch_all([&]() {
		DIE_IF_MINUS_ERRNO(pthread_setname_np(pthread_self(), name.c_str()));
	});
}

string
BeesNote::get_name()
{
	// Use explicit name if given
	if (!tl_name.empty()) {
		return tl_name;
	}

	// Try a Task name.  If there is one, return it, but do not
	// remember it.  Each output message may be a different Task.
	// The current task is thread_local so we don't need to worry
	// about it being destroyed under us.
	auto current_task = Task::current_task();
	if (current_task) {
		return current_task.title();
	}

	// OK try the pthread name next.
	char buf[24];
	memset(buf, '\0', sizeof(buf));
	int err = pthread_getname_np(pthread_self(), buf, sizeof(buf));
	if (err) {
		return string("pthread_getname_np: ") + strerror(err);
	}
	buf[sizeof(buf) - 1] = '\0';

	// thread_getname_np returns process name
	// ...by default?  ...for the main thread?
	// ...except during exception handling?
	// ...randomly?
	return buf;
}

int
BeesNote::get_policy()
{
	int policy = SCHED_OTHER;
	sched_param param = { .sched_priority = 0 };
	pthread_getschedparam(pthread_self(), &policy, &param);

	return policy;
}

BeesNote::ThreadStatusMap
BeesNote::get_status()
{
	unique_lock<mutex> lock(s_mutex);
	ThreadStatusMap rv;
	for (auto t : s_status) {
		ostringstream oss;
		if (!t.second->m_name.empty()) {
			oss << t.second->m_name << ": ";
		}
		if (t.second->m_timer.age() > BEES_TOO_LONG) {
			oss << "[" << t.second->m_timer << "s] ";
		}
		switch (t.second->m_policy) {
			case SCHED_BATCH:
				oss << "[BT] ";
				break;
			case SCHED_IDLE:
				oss << "[ID] ";
				break;
		}
		t.second->m_func(oss);
		rv[t.first] = oss.str();
	}
	return rv;
}

