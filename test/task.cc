#include "tests.h"

#include "crucible/task.h"
#include "crucible/time.h"

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
		Task t([c, &task_done, &mtx, &cv]() {
			unique_lock<mutex> lock(mtx);
			// cerr << "Task #" << c << endl;
			task_done.at(c) = true;
			cv.notify_one();
		}, [=](ostream& os) -> ostream& { return os << "task #" << c; });
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

#define TASK_MACRO(printer, body) Task( \
	[=]() { body; }, \
	[=](ostream &__os) -> ostream& { return __os << printer; } \
)

void
test_macro()
{
	TASK_MACRO(
		"A Task",
		cerr << "Hello, World!" << endl;
	).run();
}

void
test_finish()
{
	ostringstream oss;
	TaskMaster::print_queue(oss);
	TaskMaster::print_workers(oss);
	TaskMaster::set_thread_count(0);
	// cerr << "finish done" << endl;
}

void
test_unfinish()
{
	TaskMaster::set_thread_count();
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
		Task t([c, &task_done, &mtx, &cv, bl]() mutable {
			// cerr << "Task #" << c << endl;
			unique_lock<mutex> lock(mtx);
			task_done.at(c) = true;
			bl.release();
		}, [=](ostream& os) -> ostream& { return os << "task #" << c; });
		t.run();
	}

	// Get current status
	ostringstream oss;
	TaskMaster::print_queue(oss);
	TaskMaster::print_workers(oss);

	bool done_flag = false;

	Task completed(
		[&mtx, &cv, &done_flag]() {
			unique_lock<mutex> lock(mtx);
			// cerr << "Running cv notify" << endl;
			done_flag = true;
			cv.notify_all();
		},
		[](ostream &os) -> ostream& {
			return os << "Waiting for Barrier";
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
	Exclusion excl;

	mutex mtx;
	condition_variable cv;

	unique_lock<mutex> lock(mtx);

	auto b = make_shared<Barrier>();

	// Run several tasks in parallel
	for (size_t c = 0; c < count; ++c) {
		auto bl = b->lock();
		Task t([c, &only_one, &mtx, &excl, bl]() mutable {
			// cerr << "Task #" << c << endl;
			auto lock = excl.try_lock();
			if (!lock) {
				excl.insert_task(Task::current_task());
				return;
			}
			bool locked = only_one.try_lock();
			assert(locked);
			nanosleep(0.0001);
			only_one.unlock();
			bl.release();
		}, [=](ostream& os) -> ostream& { return os << "task #" << c; });
		t.run();
	}

	bool done_flag = false;

	Task completed(
		[&mtx, &cv, &done_flag]() {
			unique_lock<mutex> lock(mtx);
			// cerr << "Running cv notify" << endl;
			done_flag = true;
			cv.notify_all();
		},
		[](ostream &os) -> ostream& {
			return os << "Waiting for Barrier";
		}
	);
	b->insert_task(completed);

	b.reset();

	while (true) {
		if (done_flag) {
			break;
		}

		cv.wait(lock);
	}
}

int
main(int, char**)
{
	// in case of deadlock
	alarm(9);

	RUN_A_TEST(test_tasks(256));
	RUN_A_TEST(test_macro());
	RUN_A_TEST(test_finish());
	RUN_A_TEST(test_unfinish());
	RUN_A_TEST(test_barrier(256));
	RUN_A_TEST(test_finish());
	RUN_A_TEST(test_unfinish());
	RUN_A_TEST(test_exclusion(256));
	RUN_A_TEST(test_finish());

	exit(EXIT_SUCCESS);
}
