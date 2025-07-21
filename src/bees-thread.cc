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
	BEESLOGDEBUG("BeesThread exec " << m_name);
	m_thread_ptr = make_shared<thread>([this, func]() {
		BeesNote::set_name(m_name);
		BEESLOGDEBUG("Starting thread " << m_name);
		BEESNOTE("thread function");
		Timer thread_time;
		catch_all([&]() {
			func();
		});
		BEESLOGDEBUG("Exiting thread " << m_name << ", " << thread_time << " sec");
	});
}

BeesThread::BeesThread(string name, function<void()> func) :
	m_name(name)
{
	THROW_CHECK1(invalid_argument, name, !name.empty());
	BEESLOGDEBUG("BeesThread construct " << m_name);
	exec(func);
}

void
BeesThread::join()
{
	if (!m_thread_ptr) {
		BEESLOGDEBUG("Thread " << m_name << " no thread ptr");
		return;
	}

	BEESLOGDEBUG("BeesThread::join " << m_name);
	if (m_thread_ptr->joinable()) {
		BEESLOGDEBUG("Joining thread " << m_name);
		Timer thread_time;
		m_thread_ptr->join();
		BEESLOGDEBUG("Waited for " << m_name << ", " << thread_time << " sec");
	} else if (!m_name.empty()) {
		BEESLOGDEBUG("BeesThread " << m_name << " not joinable");
	} else {
		BEESLOGDEBUG("BeesThread else " << m_name);
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
		BEESLOGDEBUG("Thread " << m_name << " no thread ptr");
		return;
	}

	BEESLOGDEBUG("BeesThread destructor " << m_name);
	if (m_thread_ptr->joinable()) {
		BEESLOGDEBUG("Waiting for thread " << m_name);
		Timer thread_time;
		m_thread_ptr->join();
		BEESLOGDEBUG("Waited for " << m_name << ", " << thread_time << " sec");
	} else if (!m_name.empty()) {
		BEESLOGDEBUG("Thread " << m_name << " not joinable");
	} else {
		BEESLOGDEBUG("Thread destroy else " << m_name);
	}
}

