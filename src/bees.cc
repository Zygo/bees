#include "bees.h"

#include "crucible/limits.h"
#include "crucible/process.h"
#include "crucible/string.h"
#include "crucible/task.h"

#include <cctype>
#include <cmath>
#include <cstdio>

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

void
do_cmd_help(char *argv[])
{
	fprintf(stderr, BEES_USAGE, argv[0]);
}

// static inline helpers ----------------------------------------

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
		m_stats_map[idx] = amount;
	} else {
		m_stats_map[idx] += amount;
	}
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

static
bool
bees_readahead_check(int const fd, off_t const offset, size_t const size)
{
	// FIXME: the rest of the code calls this function more often than necessary,
	// usually back-to-back calls on the same range in a loop.
	// Simply discard requests that are identical to recent requests.
	const Stat stat_rv(fd);
	auto tup = make_tuple(offset, size, stat_rv.st_dev, stat_rv.st_ino);
	static mutex s_recent_mutex;
	static set<decltype(tup)> s_recent;
	unique_lock<mutex> lock(s_recent_mutex);
	if (s_recent.size() > BEES_MAX_EXTENT_REF_COUNT) {
		s_recent.clear();
		BEESCOUNT(readahead_clear);
	}
	const auto rv = s_recent.insert(tup);
	// If we recently did this readahead, we're done here
	if (!rv.second) {
		BEESCOUNT(readahead_skip);
	}
	return rv.second;
}

static
void
bees_readahead_nolock(int const fd, const off_t offset, const size_t size)
{
	if (!bees_readahead_check(fd, offset, size)) return;
	Timer readahead_timer;
	BEESNOTE("readahead " << name_fd(fd) << " offset " << to_hex(offset) << " len " << pretty(size));
	BEESTOOLONG("readahead " << name_fd(fd) << " offset " << to_hex(offset) << " len " << pretty(size));
#if 0
	// In the kernel, readahead() is identical to posix_fadvise(..., POSIX_FADV_DONTNEED)
	DIE_IF_NON_ZERO(readahead(fd, offset, size));
#else
	// Make sure this data is in page cache by brute force
	// The btrfs kernel code does readahead with lower ioprio
	// and might discard the readahead request entirely.
	BEESNOTE("emulating readahead " << name_fd(fd) << " offset " << to_hex(offset) << " len " << pretty(size));
	auto working_size = size;
	auto working_offset = offset;
	while (working_size) {
		// don't care about multithreaded writes to this buffer--it is garbage anyway
		static uint8_t dummy[BEES_READAHEAD_SIZE];
		const size_t this_read_size = min(working_size, sizeof(dummy));
		// Ignore errors and short reads.  It turns out our size
		// parameter isn't all that accurate, so we can't use
		// the pread_or_die template.
		(void)!pread(fd, dummy, this_read_size, working_offset);
		BEESCOUNT(readahead_count);
		BEESCOUNTADD(readahead_bytes, this_read_size);
		working_offset += this_read_size;
		working_size -= this_read_size;
	}
#endif
	BEESCOUNTADD(readahead_ms, readahead_timer.age() * 1000);
}

static mutex s_only_one;

void
bees_readahead_pair(int fd, off_t offset, size_t size, int fd2, off_t offset2, size_t size2)
{
	if (!bees_readahead_check(fd, offset, size) && !bees_readahead_check(fd2, offset2, size2)) return;
	BEESNOTE("waiting to readahead " << name_fd(fd) << " offset " << to_hex(offset) << " len " << pretty(size) << ","
		<< "\n\t" << name_fd(fd2) << " offset " << to_hex(offset2) << " len " << pretty(size2));
	unique_lock<mutex> m_lock(s_only_one);
	bees_readahead_nolock(fd, offset, size);
	bees_readahead_nolock(fd2, offset2, size2);
}

void
bees_readahead(int const fd, const off_t offset, const size_t size)
{
	if (!bees_readahead_check(fd, offset, size)) return;
	BEESNOTE("waiting to readahead " << name_fd(fd) << " offset " << to_hex(offset) << " len " << pretty(size));
	unique_lock<mutex> m_lock(s_only_one);
	bees_readahead_nolock(fd, offset, size);
}

void
bees_unreadahead(int const fd, off_t offset, size_t size)
{
	Timer unreadahead_timer;
	BEESNOTE("unreadahead " << name_fd(fd) << " offset " << to_hex(offset) << " len " << pretty(size));
	BEESTOOLONG("unreadahead " << name_fd(fd) << " offset " << to_hex(offset) << " len " << pretty(size));
	DIE_IF_NON_ZERO(posix_fadvise(fd, offset, size, POSIX_FADV_DONTNEED));
	BEESCOUNTADD(readahead_unread_ms, unreadahead_timer.age() * 1000);
}

thread_local random_device bees_random_device;
thread_local uniform_int_distribution<default_random_engine::result_type> bees_random_seed_dist(
	numeric_limits<default_random_engine::result_type>::min(),
	numeric_limits<default_random_engine::result_type>::max()
);
thread_local default_random_engine bees_generator(bees_random_seed_dist(bees_random_device));

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
		// Forget snapshots, balance, compression, and dedupe:
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
BeesTempFile::resize(off_t offset)
{
	BEESTOOLONG("Resizing temporary file to " << to_hex(offset));
	BEESNOTE("Resizing temporary file " << name_fd(m_fd) << " to " << to_hex(offset));
	BEESTRACE("Resizing temporary file " << name_fd(m_fd) << " to " << to_hex(offset));

	// Truncate
	Timer resize_timer;
	DIE_IF_NON_ZERO(ftruncate(m_fd, offset));
	BEESCOUNT(tmp_resize);

	// Success
	m_end_offset = offset;

	// Count time spent here
	BEESCOUNTADD(tmp_resize_ms, resize_timer.age() * 1000);
}

void
BeesTempFile::reset()
{
	// Always leave first block empty to avoid creating a file with an inline extent
	resize(BLOCK_SIZE_CLONE);
}


BeesTempFile::~BeesTempFile()
{
	BEESLOGDEBUG("destroying temporary file " << this << " in " << m_ctx->root_path() << " fd " << name_fd(m_fd));

	// Remove this file from open_root_ino lookup table
	m_roots->erase_tmpfile(m_fd);

	// Remove from blacklist
	m_ctx->blacklist_erase(BeesFileId(m_fd));
}

BeesTempFile::BeesTempFile(shared_ptr<BeesContext> ctx) :
	m_ctx(ctx),
	m_roots(ctx->roots()),
	m_end_offset(0)
{
	BEESLOGDEBUG("creating temporary file " << this << " in " << m_ctx->root_path());
	BEESNOTE("creating temporary file in " << m_ctx->root_path());
	BEESTOOLONG("creating temporary file in " << m_ctx->root_path());

	Timer create_timer;
	DIE_IF_MINUS_ONE(m_fd = openat(m_ctx->root_fd(), ".", FLAGS_OPEN_TMPFILE, S_IRUSR | S_IWUSR));
	BEESCOUNT(tmp_create);

	// Don't include this file in new extent scans
	m_ctx->blacklist_insert(BeesFileId(m_fd));

	// Add this file to open_root_ino lookup table
	m_roots->insert_tmpfile(m_fd);

	// Set compression attribute
	BEESTRACE("Getting FS_COMPR_FL on m_fd " << name_fd(m_fd));
	int flags = ioctl_iflags_get(m_fd);
	flags |= FS_COMPR_FL;
	BEESTRACE("Setting FS_COMPR_FL on m_fd " << name_fd(m_fd) << " flags " << to_hex(flags));
	ioctl_iflags_set(m_fd, flags);

	// Count time spent here
	BEESCOUNTADD(tmp_create_ms, create_timer.age() * 1000);

	// Set initial size
	reset();
}

void
BeesTempFile::realign()
{
	if (m_end_offset & BLOCK_MASK_CLONE) {
		// BEESTRACE("temporary file size " << to_hex(m_end_offset) << " not aligned");
		BEESCOUNT(tmp_realign);
		reset();
		return;
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
			BEESCOUNT(tmp_block);
			BEESCOUNTADD(tmp_bytes, len);
		}
		src_p += len;
		dst_p += len;
	}
	BEESCOUNTADD(tmp_copy_ms, copy_timer.age() * 1000);

	BEESCOUNT(tmp_copy);
	return rv;
}

static
ostream &
operator<<(ostream &os, const siginfo_t &si)
{
	return os << "siginfo_t { "
		<< "signo = " << si.si_signo << " (" << signal_ntoa(si.si_signo) << "), "
		<< "errno = " << si.si_errno << ", "
		<< "code = " << si.si_code << ", "
		// << "trapno = " << si.si_trapno << ", "
		<< "pid = " << si.si_pid << ", "
		<< "uid = " << si.si_uid << ", "
		<< "status = " << si.si_status << ", "
		<< "utime = " << si.si_utime << ", "
		<< "stime = " << si.si_stime << ", "
		// << "value = " << si.si_value << ", "
		<< "int = " << si.si_int << ", "
		<< "ptr = " << si.si_ptr << ", "
		<< "overrun = " << si.si_overrun << ", "
		<< "timerid = " << si.si_timerid << ", "
		<< "addr = " << si.si_addr << ", "
		<< "band = " << si.si_band << ", "
		<< "fd = " << si.si_fd << ", "
		// << "addr_lsb = " << si.si_addr_lsb << ", "
		// << "lower = " << si.si_lower << ", "
		// << "upper = " << si.si_upper << ", "
		// << "pkey = " << si.si_pkey << ", "
		<< "call_addr = " << si.si_call_addr << ", "
		<< "syscall = " << si.si_syscall << ", "
		<< "arch = " << si.si_arch
		<< " }";
}

static sigset_t new_sigset, old_sigset;

void
block_term_signal()
{
	BEESLOGDEBUG("Masking signals");

	DIE_IF_NON_ZERO(sigemptyset(&new_sigset));
	DIE_IF_NON_ZERO(sigaddset(&new_sigset, SIGTERM));
	DIE_IF_NON_ZERO(sigaddset(&new_sigset, SIGINT));
	DIE_IF_NON_ZERO(sigprocmask(SIG_BLOCK, &new_sigset, &old_sigset));
}

void
wait_for_term_signal()
{
	BEESNOTE("waiting for signals");
	BEESLOGDEBUG("Waiting for signals...");
	siginfo_t info;

	// Ironically, sigwaitinfo can be interrupted by a signal.
	while (true) {
		const int rv = sigwaitinfo(&new_sigset, &info);
		if (rv == -1) {
			if (errno == EINTR) {
				BEESLOGDEBUG("Restarting sigwaitinfo");
				continue;
			}
			THROW_ERRNO("sigwaitinfo errno = " << errno);
		} else {
			BEESLOGNOTICE("Received signal " << rv << " info " << info);
			// Unblock so we die immediately if signalled again
			DIE_IF_NON_ZERO(sigprocmask(SIG_BLOCK, &old_sigset, &new_sigset));
			break;
		}
	}
	BEESLOGDEBUG("Signal catcher exiting");
}

int
bees_main(int argc, char *argv[])
{
	set_catch_explainer([&](string s) {
		if (BeesTracer::get_silent()) {
			BEESLOGDEBUG("exception (ignored): " << s);
			BEESCOUNT(exception_caught_silent);
		} else {
			BEESLOGNOTICE("\n\n*** EXCEPTION ***\n\t" << s << "\n***\n");
			BEESCOUNT(exception_caught);
		}
	});

	// The thread name for the main function is also what the kernel
	// Oops messages call the entire process.  So even though this
	// thread's proper title is "main", let's call it "bees".
	BeesNote::set_name("bees");
	BEESNOTE("main");

	THROW_CHECK1(invalid_argument, argc, argc >= 0);

	// Have to block signals now before we create a bunch of threads
	// so the threads will also have the signals blocked.
	block_term_signal();

	// Create a context so we can apply configuration to it
	shared_ptr<BeesContext> bc = make_shared<BeesContext>();
	BEESLOGDEBUG("context constructed");

	string cwd(readlink_or_die("/proc/self/cwd"));

	// Defaults
	bool chatter_prefix_timestamp = true;
	double thread_factor = 0;
	unsigned thread_count = 0;
	unsigned thread_min = 0;
	double load_target = 0;
	bool workaround_btrfs_send = false;
	BeesRoots::ScanMode root_scan_mode = BeesRoots::SCAN_MODE_EXTENT;

	// Configure getopt_long
	static const struct option long_options[] = {
		{ "thread-factor",         required_argument, NULL, 'C' },
		{ "thread-min",            required_argument, NULL, 'G' },
		{ "strip-paths",           no_argument,       NULL, 'P' },
		{ "no-timestamps",         no_argument,       NULL, 'T' },
		{ "workaround-btrfs-send", no_argument,       NULL, 'a' },
		{ "thread-count",          required_argument, NULL, 'c' },
		{ "loadavg-target",        required_argument, NULL, 'g' },
		{ "help",                  no_argument,       NULL, 'h' },
		{ "scan-mode",             required_argument, NULL, 'm' },
		{ "absolute-paths",        no_argument,       NULL, 'p' },
		{ "timestamps",            no_argument,       NULL, 't' },
		{ "verbose",               required_argument, NULL, 'v' },
		{ 0, 0, 0, 0 },
	};

	// Build getopt_long's short option list from the long_options table.
	// While we're at it, make sure we didn't duplicate any options.
	string getopt_list;
	set<decltype(option::val)> option_vals;
	for (const struct option *op = long_options; op->val; ++op) {
		THROW_CHECK1(runtime_error, op->val, !option_vals.count(op->val));
		option_vals.insert(op->val);
		if ((op->val & 0xff) != op->val) {
			continue;
		}
		getopt_list += op->val;
		if (op->has_arg == required_argument) {
			getopt_list += ':';
		}
	}

	// Parse options
	int c;
	while (true) {
		int option_index = 0;

		c = getopt_long(argc, argv, getopt_list.c_str(), long_options, &option_index);
		if (-1 == c) {
			break;
		}

		BEESLOGDEBUG("Parsing option '" << static_cast<char>(c) << "'");

		switch (c) {

			case 'C':
				thread_factor = stod(optarg);
				break;
			case 'G':
				thread_min = stoul(optarg);
				break;
			case 'P':
				crucible::set_relative_path(cwd);
				break;
			case 'T':
				chatter_prefix_timestamp = false;
				break;
			case 'a':
				workaround_btrfs_send = true;
				break;
			case 'c':
				thread_count = stoul(optarg);
				break;
			case 'g':
				load_target = stod(optarg);
				break;
			case 'm':
				root_scan_mode = static_cast<BeesRoots::ScanMode>(stoul(optarg));
				break;
			case 'p':
				crucible::set_relative_path("");
				break;
			case 't':
				chatter_prefix_timestamp = true;
				break;
			case 'v':
				{
					int new_log_level = stoul(optarg);
					THROW_CHECK1(out_of_range, new_log_level, new_log_level <= 8);
					THROW_CHECK1(out_of_range, new_log_level, new_log_level >= 0);
					bees_log_level = new_log_level;
					BEESLOGNOTICE("log level set to " << bees_log_level);
				}
				break;

			case 'h':
			default:
				do_cmd_help(argv);
				return EXIT_FAILURE;
		}
	}

	if (optind + 1 != argc) {
		BEESLOGERR("Only one filesystem path per bees process");
		return EXIT_FAILURE;
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

	if (load_target != 0) {
		BEESLOGNOTICE("setting load average target to " << load_target);
		BEESLOGNOTICE("setting worker thread pool minimum size to " << thread_min);
		TaskMaster::set_thread_min_count(thread_min);
	}
	TaskMaster::set_loadavg_target(load_target);

	BEESLOGNOTICE("setting worker thread pool maximum size to " << thread_count);
	TaskMaster::set_thread_count(thread_count);

	// Set root path
	string root_path = argv[optind++];
	BEESLOGNOTICE("setting root path to '" << root_path << "'");
	bc->set_root_path(root_path);

	// Workaround for btrfs send
	bc->roots()->set_workaround_btrfs_send(workaround_btrfs_send);

	// Set root scan mode
	bc->roots()->set_scan_mode(root_scan_mode);

	if (root_scan_mode == BeesRoots::SCAN_MODE_EXTENT) {
		MultiLocker::enable_locking(false);
	} else {
		// Workaround for a kernel bug that the subvol-based crawlers keep triggering
		MultiLocker::enable_locking(true);
	}

	// Start crawlers
	bc->start();

	// Now we just wait forever
	wait_for_term_signal();

	// Shut it down
	bc->stop();

	// That is all.
	return EXIT_SUCCESS;
}

int
main(int argc, char *argv[])
{
	cerr << "bees version " << BEES_VERSION << endl;

	if (argc < 2) {
		do_cmd_help(argv);
		return EXIT_FAILURE;
	}

	int rv = EXIT_FAILURE;
	catch_all([&]() {
		rv = bees_main(argc, argv);
	});
	BEESLOGNOTICE("Exiting with status " << rv << " " << (rv ? "(failure)" : "(success)"));
	return rv;
}

// instantiate templates for linkage ----------------------------------------

template class BeesStatTmpl<uint64_t>;
template ostream & operator<<(ostream &os, const BeesStatTmpl<uint64_t> &bs);

template class BeesStatTmpl<double>;
template ostream & operator<<(ostream &os, const BeesStatTmpl<double> &bs);
