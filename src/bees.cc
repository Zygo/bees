#include "bees.h"

#include "crucible/limits.h"
#include "crucible/process.h"
#include "crucible/string.h"
#include "crucible/task.h"

#include <cctype>
#include <cmath>

#include <iostream>
#include <memory>
#include <sstream>

// PRIx64
#include <inttypes.h>

#include <sched.h>
#include <sys/fanotify.h>

#include <linux/fs.h>
#include <sys/ioctl.h>

// setrlimit
#include <sys/time.h>
#include <sys/resource.h>

#include <getopt.h>

using namespace crucible;
using namespace std;

int
do_cmd_help(char *argv[])
{
	cerr << "Usage: " << argv[0] << " [options] fs-root-path [fs-root-path-2...]\n"
		"Performs best-effort extent-same deduplication on btrfs.\n"
		"\n"
		"fs-root-path MUST be the root of a btrfs filesystem tree (id 5).\n"
		"Other directories will be rejected.\n"
		"\n"
		"Options:\n"
		"\t-h, --help\t\tShow this help\n"
		"\t-c, --thread-count\tWorker thread count (default CPU count * factor)\n"
		"\t-C, --thread-factor\tWorker thread factor (default " << BEES_DEFAULT_THREAD_FACTOR << ")\n"
		"\t-m, --scan-mode\t\tScanning mode (0..1, default 0)\n"
		"\t-t, --timestamps\tShow timestamps in log output (default)\n"
		"\t-T, --no-timestamps\tOmit timestamps in log output\n"
		"\t-p, --absolute-paths\tShow absolute paths (default)\n"
		"\t-P, --strip-paths\tStrip $CWD from beginning of all paths in the log\n"
		"\n"
		"Optional environment variables:\n"
		"\tBEESHOME\tPath to hash table and configuration files\n"
		"\t\t\t(default is .beeshome/ in the root of each filesystem).\n"
		"\n"
		"\tBEESSTATUS\tFile to write status to (tmpfs recommended, e.g. /run).\n"
		"\t\t\tNo status is written if this variable is unset.\n"
		"\n"
	<< endl;
	return 0;
}

// tracing ----------------------------------------

thread_local BeesTracer *BeesTracer::tl_next_tracer = nullptr;

BeesTracer::~BeesTracer()
{
	if (uncaught_exception()) {
		try {
			m_func();
		} catch (exception &e) {
			BEESLOGERR("Nested exception: " << e.what());
		} catch (...) {
			BEESLOGERR("Nested exception ...");
		}
		if (!m_next_tracer) {
			BEESLOGERR("---  END  TRACE --- exception ---");
		}
	}
	tl_next_tracer = m_next_tracer;
}

BeesTracer::BeesTracer(function<void()> f) :
	m_func(f)
{
	m_next_tracer = tl_next_tracer;
	tl_next_tracer = this;
}

void
BeesTracer::trace_now()
{
	BeesTracer *tp = tl_next_tracer;
	BEESLOGERR("--- BEGIN TRACE ---");
	while (tp) {
		tp->m_func();
		tp = tp->m_next_tracer;
	}
	BEESLOGERR("---  END  TRACE ---");
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
		s_status[gettid()] = tl_next;
	} else {
		s_status.erase(gettid());
	}
}

BeesNote::BeesNote(function<void(ostream &os)> f) :
	m_func(f)
{
	m_name = get_name();
	m_prev = tl_next;
	tl_next = this;
	unique_lock<mutex> lock(s_mutex);
	s_status[gettid()] = tl_next;
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
		t.second->m_func(oss);
		rv[t.first] = oss.str();
	}
	return rv;
}

// static inline helpers ----------------------------------------

static inline
bool
bees_addr_check(uint64_t v)
{
	return !(v & (1ULL << 63));
}

static inline
bool
bees_addr_check(int64_t v)
{
	return !(v & (1ULL << 63));
}

string
pretty(double d)
{
	static const char * units[] = { "", "K", "M", "G", "T", "P", "E" };
	static const char * *units_stop = units + sizeof(units) / sizeof(units[0]) - 1;
	const char * *unit = units;
	while (d >= 1024 && unit < units_stop) {
		d /= 1024;
		++unit;
	}
	ostringstream oss;
	oss << (round(d * 1000.0) / 1000.0) << *unit;
	return oss.str();
}

// ostream operators ----------------------------------------

template <class T>
ostream &
operator<<(ostream &os, const BeesStatTmpl<T> &bs)
{
	unique_lock<mutex> lock(bs.m_mutex);
	bool first = true;
	string last_tag;
	for (auto i : bs.m_stats_map) {
		if (i.second == 0) {
			continue;
		}
		string tag = i.first.substr(0, i.first.find_first_of("_"));
		if (!last_tag.empty() && tag != last_tag) {
			os << "\n\t";
		} else if (!first) {
			os << " ";
		}
		last_tag = tag;
		first = false;
		os << i.first << "=" << i.second;
	}
	return os;
}

// other ----------------------------------------

template <class T>
T&
BeesStatTmpl<T>::at(string idx)
{
	if (!m_stats_map.count(idx)) {
		m_stats_map[idx] = 0;
	}
	return m_stats_map[idx];
}

template <class T>
T
BeesStatTmpl<T>::at(string idx) const
{
	unique_lock<mutex> lock(m_mutex);
	auto rv = m_stats_map.at(idx);
	return rv;
}

template <class T>
void
BeesStatTmpl<T>::add_count(string idx, size_t amount)
{
	unique_lock<mutex> lock(m_mutex);
	if (!m_stats_map.count(idx)) {
		m_stats_map[idx] = 0;
	}
	m_stats_map.at(idx) += amount;
}

template <class T>
BeesStatTmpl<T>::BeesStatTmpl(const BeesStatTmpl &that)
{
	if (&that == this) return;
	unique_lock<mutex> lock(m_mutex);
	unique_lock<mutex> lock2(that.m_mutex);
	m_stats_map = that.m_stats_map;
}

template <class T>
BeesStatTmpl<T> &
BeesStatTmpl<T>::operator=(const BeesStatTmpl<T> &that)
{
	if (&that == this) return *this;
	unique_lock<mutex> lock(m_mutex);
	unique_lock<mutex> lock2(that.m_mutex);
	m_stats_map = that.m_stats_map;
	return *this;
}

BeesStats BeesStats::s_global;

BeesStats
BeesStats::operator-(const BeesStats &that) const
{
	if (&that == this) return BeesStats();

	unique_lock<mutex> this_lock(m_mutex);
	BeesStats this_copy;
	this_copy.m_stats_map = m_stats_map;
	this_lock.unlock();

	unique_lock<mutex> that_lock(that.m_mutex);
	BeesStats that_copy;
	that_copy.m_stats_map = that.m_stats_map;
	that_lock.unlock();

	for (auto i : that.m_stats_map) {
		if (i.second != 0) {
			this_copy.at(i.first) -= i.second;
		}
	}
	return this_copy;
}

BeesRates
BeesStats::operator/(double d) const
{
	BeesRates rv;
	unique_lock<mutex> lock(m_mutex);
	for (auto i : m_stats_map) {
		rv.m_stats_map[i.first] = ceil(i.second / d * 1000) / 1000;
	}
	return rv;
}

BeesStats::operator bool() const
{
	unique_lock<mutex> lock(m_mutex);
	for (auto i : m_stats_map) {
		if (i.second != 0) {
			return true;
		}
	}
	return false;
}

BeesTooLong::BeesTooLong(const string &s, double limit) :
	m_limit(limit),
	m_func([s](ostream &os) { os << s; })
{
}

BeesTooLong::BeesTooLong(const func_type &func, double limit) :
	m_limit(limit),
	m_func(func)
{
}

void
BeesTooLong::check() const
{
	if (age() > m_limit) {
		ostringstream oss;
		m_func(oss);
		BEESLOGWARN("PERFORMANCE: " << *this << " sec: " << oss.str());
	}
}

BeesTooLong::~BeesTooLong()
{
	check();
}

BeesTooLong &
BeesTooLong::operator=(const func_type &f)
{
	m_func = f;
	return *this;
}

void
bees_sync(int fd)
{
	Timer sync_timer;
	BEESNOTE("syncing " << name_fd(fd));
	BEESTOOLONG("syncing " << name_fd(fd));
	DIE_IF_NON_ZERO(fsync(fd));
	BEESCOUNT(sync_count);
	BEESCOUNTADD(sync_ms, sync_timer.age() * 1000);
}

BeesStringFile::BeesStringFile(Fd dir_fd, string name, size_t limit) :
	m_dir_fd(dir_fd),
	m_name(name),
	m_limit(limit)
{
	BEESLOGINFO("BeesStringFile " << name_fd(m_dir_fd) << "/" << m_name << " max size " << pretty(m_limit));
}

void
BeesStringFile::name(const string &new_name)
{
	m_name = new_name;
}

string
BeesStringFile::name() const
{
	return m_name;
}

string
BeesStringFile::read()
{
	BEESNOTE("opening " << m_name << " in " << name_fd(m_dir_fd));
	Fd fd(openat(m_dir_fd, m_name.c_str(), FLAGS_OPEN_FILE));
	if (!fd) {
		return string();
	}

	BEESNOTE("sizing " << m_name << " in " << name_fd(m_dir_fd));
	Stat st(fd);
	THROW_CHECK1(out_of_range, st.st_size, st.st_size > 0);
	THROW_CHECK1(out_of_range, st.st_size, st.st_size < ranged_cast<off_t>(m_limit));

	BEESNOTE("reading " << m_name << " in " << name_fd(m_dir_fd));
	return read_string(fd, st.st_size);
}

void
BeesStringFile::write(string contents)
{
	THROW_CHECK2(out_of_range, contents.size(), m_limit, contents.size() < m_limit);
	auto tmpname = m_name + ".tmp";

	BEESNOTE("unlinking " << tmpname << " in " << name_fd(m_dir_fd));
	unlinkat(m_dir_fd, tmpname.c_str(), 0);
	// ignore error

	BEESNOTE("closing " << tmpname << " in " << name_fd(m_dir_fd));
	{
		Fd ofd = openat_or_die(m_dir_fd, tmpname, FLAGS_CREATE_FILE, S_IRUSR | S_IWUSR);
		BEESNOTE("writing " << tmpname << " in " << name_fd(m_dir_fd));
		write_or_die(ofd, contents);
#if 0
		// This triggers too many btrfs bugs.  I wish I was kidding.
		// Forget snapshots, balance, compression, and dedup:
		// the system call you have to fear on btrfs is fsync().
		// Also note that when bees renames a temporary over an
		// existing file, it flushes the temporary, so we get
		// the right behavior if we just do nothing here
		// (except when the file is first created; however,
		// in that case the result is the same as if the file
		// did not exist, was empty, or was filled with garbage).
		BEESNOTE("fsyncing " << tmpname << " in " << name_fd(m_dir_fd));
		DIE_IF_NON_ZERO(fsync(ofd));
#endif
	}
	BEESNOTE("renaming " << tmpname << " to " << m_name << " in FD " << name_fd(m_dir_fd));
	BEESTRACE("renaming " << tmpname << " to " << m_name << " in FD " << name_fd(m_dir_fd));
	renameat_or_die(m_dir_fd, tmpname, m_dir_fd, m_name);
}

void
BeesTempFile::create()
{
	// BEESLOG("creating temporary file in " << m_ctx->root_path());
	BEESNOTE("creating temporary file in " << m_ctx->root_path());
	BEESTOOLONG("creating temporary file in " << m_ctx->root_path());

	Timer create_timer;
	DIE_IF_MINUS_ONE(m_fd = openat(m_ctx->root_fd(), ".", FLAGS_OPEN_TMPFILE, S_IRUSR | S_IWUSR));
	BEESCOUNT(tmp_create);

	// Can't reopen this file, so don't allow any resolves there
	// Resolves won't work there anyway.  There are lots of tempfiles
	// and they're short-lived, so this ends up being just a memory leak
	// m_ctx->blacklist_add(BeesFileId(m_fd));

	// Put this inode in the cache so we can resolve it later
	m_ctx->insert_root_ino(m_fd);

	// Set compression attribute
	BEESTRACE("Getting FS_COMPR_FL on m_fd " << name_fd(m_fd));
	int flags = ioctl_iflags_get(m_fd);
	flags |= FS_COMPR_FL;
	BEESTRACE("Setting FS_COMPR_FL on m_fd " << name_fd(m_fd) << " flags " << to_hex(flags));
	ioctl_iflags_set(m_fd, flags);

	// Always leave first block empty to avoid creating a file with an inline extent
	m_end_offset = BLOCK_SIZE_CLONE;

	// Count time spent here
	BEESCOUNTADD(tmp_create_ms, create_timer.age() * 1000);
}

void
BeesTempFile::resize(off_t offset)
{
	BEESTOOLONG("Resizing temporary file to " << to_hex(offset));
	BEESNOTE("Resizing temporary file " << name_fd(m_fd) << " to " << to_hex(offset));
	BEESTRACE("Resizing temporary file " << name_fd(m_fd) << " to " << to_hex(offset));

	// Ensure that file covers m_end_offset..offset
	THROW_CHECK2(invalid_argument, m_end_offset, offset, m_end_offset < offset);

	// Truncate
	Timer resize_timer;
	DIE_IF_NON_ZERO(ftruncate(m_fd, offset));
	BEESCOUNT(tmp_resize);

	// Success
	m_end_offset = offset;

	// Count time spent here
	BEESCOUNTADD(tmp_resize_ms, resize_timer.age() * 1000);
}

BeesTempFile::BeesTempFile(shared_ptr<BeesContext> ctx) :
	m_ctx(ctx),
	m_end_offset(0)
{
	create();
}

void
BeesTempFile::realign()
{
	if (m_end_offset > BLOCK_SIZE_MAX_TEMP_FILE) {
		BEESLOGINFO("temporary file size " << to_hex(m_end_offset) << " > max " << BLOCK_SIZE_MAX_TEMP_FILE);
		BEESCOUNT(tmp_trunc);
		return create();
	}
	if (m_end_offset & BLOCK_MASK_CLONE) {
		// BEESTRACE("temporary file size " << to_hex(m_end_offset) << " not aligned");
		BEESCOUNT(tmp_realign);
		return create();
	}
	// OK as is
	BEESCOUNT(tmp_aligned);
}

BeesFileRange
BeesTempFile::make_hole(off_t count)
{
	THROW_CHECK1(invalid_argument, count, count > 0);
	realign();

	BEESTRACE("make hole at " << m_end_offset);

	auto end = m_end_offset + count;
	BeesFileRange rv(m_fd, m_end_offset, end);

	resize(end);

	BEESTRACE("created temporary hole " << rv);
	BEESCOUNT(tmp_hole);
	return rv;
}

BeesFileRange
BeesTempFile::make_copy(const BeesFileRange &src)
{
	BEESLOGINFO("copy: " << src);
	BEESNOTE("Copying " << src);
	BEESTRACE("Copying " << src);

	THROW_CHECK1(invalid_argument, src, src.size() > 0);

	// FIEMAP used to give us garbage data, e.g. distinct adjacent
	// extents merged into a single entry in the FIEMAP output.
	// FIEMAP didn't stop giving us garbage data, we just stopped
	// using FIEMAP.
	// We shouldn't get absurdly large extents any more; however,
	// it's still a problem if we do, so bail out and leave a trace
	// in the log.
	THROW_CHECK1(invalid_argument, src, src.size() < BLOCK_SIZE_MAX_TEMP_FILE);

	realign();

	auto begin = m_end_offset;
	auto end = m_end_offset + src.size();
	resize(end);

	Timer copy_timer;
	BeesFileRange rv(m_fd, begin, end);
	BEESTRACE("copying to: " << rv);
	BEESNOTE("copying " << src << " to " << rv);

	auto src_p = src.begin();
	auto dst_p = begin;

	bool did_block_write = false;
	while (dst_p < end) {
		auto len = min(BLOCK_SIZE_CLONE, end - dst_p);
		BeesBlockData bbd(src.fd(), src_p, len);
		// Don't fill in holes
		if (bbd.is_data_zero()) {
			BEESCOUNT(tmp_block_zero);
		} else {
			BEESNOTE("copying " << src << " to " << rv << "\n"
				"\tpwrite " << bbd << " to " << name_fd(m_fd) << " offset " << to_hex(dst_p) << " len " << len);
			pwrite_or_die(m_fd, bbd.data().data(), len, dst_p);
			did_block_write = true;
			BEESCOUNT(tmp_block);
			BEESCOUNTADD(tmp_bytes, len);
		}
		src_p += len;
		dst_p += len;
	}
	BEESCOUNTADD(tmp_copy_ms, copy_timer.age() * 1000);

	// We seem to get lockups without this!
	if (did_block_write) {
#if 1
		// Is this fixed by "Btrfs: fix deadlock between dedup on same file and starting writeback"?
		// No.
		bees_sync(m_fd);
#endif
	}

	BEESCOUNT(tmp_copy);
	return rv;
}

int
bees_main(int argc, char *argv[])
{
	set_catch_explainer([&](string s) {
		BEESLOGERR("\n\n*** EXCEPTION ***\n\t" << s << "\n***\n");
		BEESCOUNT(exception_caught);
	});

	// The thread name for the main function is also what the kernel
	// Oops messages call the entire process.  So even though this
	// thread's proper title is "main", let's call it "bees".
	BeesNote::set_name("bees");
	BEESNOTE("main");

	list<shared_ptr<BeesContext>> all_contexts;
	shared_ptr<BeesContext> bc;

	THROW_CHECK1(invalid_argument, argc, argc >= 0);

	string cwd(readlink_or_die("/proc/self/cwd"));

	// Defaults
	bool chatter_prefix_timestamp = true;
	double thread_factor = 0;
	unsigned thread_count = 0;

	// Parse options
	int c;
	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{ "thread-count",   required_argument, NULL, 'c' },
			{ "thread-factor",  required_argument, NULL, 'C' },
			{ "scan-mode", 	    required_argument, NULL, 'm' },
			{ "timestamps",     no_argument,       NULL, 't' },
			{ "no-timestamps",  no_argument,       NULL, 'T' },
			{ "absolute-paths", no_argument,       NULL, 'p' },
			{ "strip-paths",    no_argument,       NULL, 'P' },
			{ "help",           no_argument,       NULL, 'h' }
		};

		c = getopt_long(argc, argv, "c:C:m:TtPph", long_options, &option_index);
		if (-1 == c) {
			break;
		}

		switch (c) {
			case 'c':
				thread_count = stoul(optarg);
				break;
			case 'C':
				thread_factor = stod(optarg);
				break;
			case 'm':
				BeesRoots::set_scan_mode(static_cast<BeesRoots::ScanMode>(stoul(optarg)));
				break;
			case 'T':
				chatter_prefix_timestamp = false;
				break;
			case 't':
				chatter_prefix_timestamp = true;
				break;
			case 'P':
				crucible::set_relative_path(cwd);
				break;
			case 'p':
				crucible::set_relative_path("");
				break;
			case 'h':
				do_cmd_help(argv); // fallthrough
			default:
				return 2;
		}
	}

	Chatter::enable_timestamp(chatter_prefix_timestamp);

	if (!relative_path().empty()) {
		BEESLOGINFO("using relative path " << relative_path() << "\n");
	}

	BEESLOGINFO("setting rlimit NOFILE to " << BEES_OPEN_FILE_LIMIT);

	struct rlimit lim = {
		.rlim_cur = BEES_OPEN_FILE_LIMIT,
		.rlim_max = BEES_OPEN_FILE_LIMIT,
	};
	int rv = setrlimit(RLIMIT_NOFILE, &lim);
	if (rv) {
		BEESLOGINFO("setrlimit(RLIMIT_NOFILE, { " << lim.rlim_cur << " }): " << strerror(errno));
	};

	// Set up worker thread pool
	THROW_CHECK1(out_of_range, thread_factor, thread_factor >= 0);
	if (thread_count < 1) {
		if (thread_factor == 0) {
			thread_factor = BEES_DEFAULT_THREAD_FACTOR;
		}
		thread_count = max(1U, static_cast<unsigned>(ceil(thread::hardware_concurrency() * thread_factor)));
	}

	TaskMaster::set_thread_count(thread_count);

	// Create a context and start crawlers
	bool did_subscription = false;
	while (optind < argc) {
		catch_all([&]() {
			bc = make_shared<BeesContext>(bc);
			bc->set_root_path(argv[optind++]);
			did_subscription = true;
		});
	}

	if (!did_subscription) {
		BEESLOGWARN("WARNING: no filesystems added");
	}

	BeesThread status_thread("status", [&]() {
		bc->dump_status();
	});

	// Now we just wait forever
	bc->show_progress();

	// That is all.
	return 0;
}

int
main(int argc, char *argv[])
{
	cerr << "bees version " << BEES_VERSION << endl;

	if (argc < 2) {
		do_cmd_help(argv);
		return 2;
	}

	int rv = 1;
	catch_and_explain([&]() {
		rv = bees_main(argc, argv);
	});
	return rv;
}

// instantiate templates for linkage ----------------------------------------

template class BeesStatTmpl<uint64_t>;
template ostream & operator<<(ostream &os, const BeesStatTmpl<uint64_t> &bs);

template class BeesStatTmpl<double>;
template ostream & operator<<(ostream &os, const BeesStatTmpl<double> &bs);
