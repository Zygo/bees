#include "bees.h"

using namespace crucible;
using namespace std;

BeesThread::BeesThread(string name) :
	m_name(name)
{
	THROW_CHECK1(invalid_argument, name, !name.empty());
}

void
BeesThread::exec(function<void()> func)
{
	m_timer.reset();
	BEESLOG("BeesThread exec " << m_name);
	m_thread_ptr = make_shared<thread>([=]() {
		BEESLOG("Starting thread " << m_name);
		BeesNote::set_name(m_name);
		BEESNOTE("thread function");
		Timer thread_time;
		catch_all([&]() {
			DIE_IF_MINUS_ERRNO(pthread_setname_np(pthread_self(), m_name.c_str()));
		});
		catch_all([&]() {
			func();
		});
		BEESLOG("Exiting thread " << m_name << ", " << thread_time << " sec");
	});
}

BeesThread::BeesThread(string name, function<void()> func) :
	m_name(name)
{
	THROW_CHECK1(invalid_argument, name, !name.empty());
	BEESLOG("BeesThread construct " << m_name);
	exec(func);
}

void
BeesThread::join()
{
	if (!m_thread_ptr) {
		BEESLOG("Thread " << m_name << " no thread ptr");
		return;
	}

	BEESLOG("BeesThread::join " << m_name);
	if (m_thread_ptr->joinable()) {
		BEESLOG("Joining thread " << m_name);
		Timer thread_time;
		m_thread_ptr->join();
		BEESLOG("Waited for " << m_name << ", " << thread_time << " sec");
	} else if (!m_name.empty()) {
		BEESLOG("BeesThread " << m_name << " not joinable");
	} else {
		BEESLOG("BeesThread else " << m_name);
	}
}

void
BeesThread::set_name(const string &name)
{
	m_name = name;
}

BeesThread::~BeesThread()
{
	if (!m_thread_ptr) {
		BEESLOG("Thread " << m_name << " no thread ptr");
		return;
	}

	BEESLOG("BeesThread destructor " << m_name);
	if (m_thread_ptr->joinable()) {
		BEESLOG("Cancelling thread " << m_name);
		int rv = pthread_cancel(m_thread_ptr->native_handle());
		if (rv) {
			BEESLOG("pthread_cancel returned " << strerror(-rv));
		}
		BEESLOG("Waiting for thread " << m_name);
		Timer thread_time;
		m_thread_ptr->join();
		BEESLOG("Waited for " << m_name << ", " << thread_time << " sec");
	} else if (!m_name.empty()) {
		BEESLOG("Thread " << m_name << " not joinable");
	} else {
		BEESLOG("Thread destroy else " << m_name);
	}
}

