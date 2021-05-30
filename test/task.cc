#include "tests.h"

#include "crucible/task.h"
#include "crucible/time.h"

#include <atomic>
#include <chrono>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <vector>

#include <unistd.h>

using namespace crucible;
using namespace std;

void
test_tasks(size_t count)
{
	TaskMaster::set_thread_count();

	vector<bool> task_done(count, false);

	mutex mtx;
	condition_variable cv;

	unique_lock<mutex> lock(mtx);

	// Run several tasks in parallel
	for (size_t c = 0; c < count; ++c) {
		ostringstream oss;
		oss << "task #" << c;
		Task t(
			oss.str(),
			[c, &task_done, &mtx, &cv]() {
				unique_lock<mutex> lock(mtx);
				// cerr << "Task #" << c << endl;
				task_done.at(c) = true;
				cv.notify_one();
			}
		);
		t.run();
	}

	// Get current status
	ostringstream oss;
	TaskMaster::print_queue(oss);
	TaskMaster::print_workers(oss);

	while (true) {
		size_t tasks_done = 0;
		for (auto i : task_done) {
			if (i) {
				++tasks_done;
			}
		}
		if (tasks_done == count) {
			return;
		}
		// cerr << "Tasks done: " << tasks_done << endl;

		cv.wait(lock);
	}
}

void
test_finish()
{
	ostringstream oss;
	TaskMaster::print_queue(oss);
	TaskMaster::print_workers(oss);
	TaskMaster::set_thread_count(0);
	cerr << "finish done...";
}

void
test_unfinish()
{
	TaskMaster::set_thread_count();
	cerr << "unfinish done...";
}


void
test_barrier(size_t count)
{
	vector<bool> task_done(count, false);

	mutex mtx;
	condition_variable cv;

	unique_lock<mutex> lock(mtx);

	auto b = make_shared<Barrier>();

	// Run several tasks in parallel
	for (size_t c = 0; c < count; ++c) {
		auto bl = b->lock();
		ostringstream oss;
		oss << "task #" << c;
		Task t(
			oss.str(),
			[c, &task_done, &mtx, bl]() mutable {
				// cerr << "Task #" << c << endl;
				unique_lock<mutex> lock(mtx);
				task_done.at(c) = true;
				bl.release();
			}
		);
		t.run();
	}

	// Get current status
	ostringstream oss;
	TaskMaster::print_queue(oss);
	TaskMaster::print_workers(oss);

	bool done_flag = false;

	Task completed(
		"Waiting for Barrier",
		[&mtx, &cv, &done_flag]() {
			unique_lock<mutex> lock(mtx);
			// cerr << "Running cv notify" << endl;
			done_flag = true;
			cv.notify_all();
		}
	);
	b->insert_task(completed);

	b.reset();

	while (true) {
		size_t tasks_done = 0;
		for (auto i : task_done) {
			if (i) {
				++tasks_done;
			}
		}
		// cerr << "Tasks done: " << tasks_done << " done_flag " << done_flag << endl;
		if (tasks_done == count && done_flag) {
			break;
		}

		cv.wait(lock);
	}
	// cerr << "test_barrier return" << endl;
}

void
test_exclusion(size_t count)
{
	mutex only_one;
	auto excl = make_shared<Exclusion>("test_excl");

	mutex mtx;
	condition_variable cv;

	size_t tasks_running(0);
	atomic<size_t> lock_success_count(0);
	atomic<size_t> lock_failure_count(0);

	vector<size_t> pings;
	pings.resize(count);

	// Run several tasks in parallel
	for (size_t c = 0; c < count; ++c) {
		ostringstream oss;
		oss << "task #" << c;
		Task t(
			oss.str(),
			[c, &only_one, excl, &lock_success_count, &lock_failure_count, &pings, &tasks_running, &cv, &mtx]() mutable {
				// cerr << "Task #" << c << endl;
				(void)c;
				auto lock = excl->try_lock();
				if (!lock) {
					excl->insert_task(Task::current_task());
					++lock_failure_count;
					return;
				}
				++lock_success_count;
				bool locked = only_one.try_lock();
				assert(locked);
				nanosleep(0.0001);
				only_one.unlock();
				unique_lock<mutex> mtx_lock(mtx);
				--tasks_running;
				++pings[c];
				cv.notify_all();
			}
		);
		unique_lock<mutex> mtx_lock(mtx);
		++tasks_running;
		t.run();
	}

	// excl.reset();

	unique_lock<mutex> lock(mtx);
	while (tasks_running) {
		auto cv_rv = cv.wait_for(lock, chrono::duration<double>(1));
		if (cv_rv == cv_status::timeout) {
			// TaskMaster::print_tasks(cerr);
			for (auto i : pings) {
				cerr << i << " ";
			}
			cerr << endl << "tasks_running = " << tasks_running << endl;
			cerr << "lock_success_count " << lock_success_count << endl;
			cerr << "lock_failure_count " << lock_failure_count << endl;
		}
	}
	cerr << "lock_success_count " << lock_success_count << endl;
	cerr << "lock_failure_count " << lock_failure_count << endl;

	bool oops = false;
	for (size_t c = 0; c < pings.size(); ++c) {
		if (pings[c] != 1) {
			cerr << "pings[" << c << "] = " << pings[c] << endl;
			oops = true;
		}
	}
	if (oops) {
		assert(!"Pings not OK");
	} else {
		cerr << "Pings OK" << endl;
	}
}

int
main(int, char**)
{
	// in case of deadlock
	alarm(9);

	RUN_A_TEST(test_tasks(256));
	RUN_A_TEST(test_finish());
	RUN_A_TEST(test_unfinish());
	RUN_A_TEST(test_barrier(256));
	RUN_A_TEST(test_finish());
	RUN_A_TEST(test_unfinish());
	RUN_A_TEST(test_exclusion(256));
	RUN_A_TEST(test_finish());

	exit(EXIT_SUCCESS);
}
