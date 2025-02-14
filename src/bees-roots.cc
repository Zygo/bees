#include "bees.h"

#include "crucible/btrfs-tree.h"
#include "crucible/cache.h"
#include "crucible/cleanup.h"
#include "crucible/ntoa.h"
#include "crucible/openat2.h"
#include "crucible/string.h"
#include "crucible/table.h"
#include "crucible/task.h"

#include <algorithm>
#include <fstream>
#include <tuple>

using namespace crucible;
using namespace std;

static
string
pretty_seconds(uint64_t t)
{
	struct unit {
		uint64_t m_count;
		const char *m_suffix;
	};
	static const unit units[] = {
		{ .m_count = 365 * 24 * 60 * 60, .m_suffix = "y" },
		{ .m_count =   7 * 24 * 60 * 60, .m_suffix = "w" },
		{ .m_count =   1 * 24 * 60 * 60, .m_suffix = "d" },
		{ .m_count =   1 *  1 * 60 * 60, .m_suffix = "h" },
		{ .m_count =   1 *  1 *  1 * 60, .m_suffix = "m" },
		{ .m_count =   1 *  1 *  1 *  1, .m_suffix = "s" },
	};

	ostringstream oss;
	uint64_t prev_unit_limit = numeric_limits<uint64_t>::max();
	size_t precision_count = 0;
	for (const auto &i : units) {
		const auto this_t = t % prev_unit_limit;
		if (this_t >= i.m_count) {
			if (precision_count) {
				oss << " ";
			}
			++precision_count;
			oss << this_t / i.m_count << i.m_suffix;
		}
		prev_unit_limit = i.m_count;
		if (precision_count > 1) {
			break;
		}
	}
	return oss.str();
}

string
format_time(time_t t)
{
	const struct tm *const tmp = localtime(&t);
	char buf[1024];
	strftime(buf, sizeof(buf), "%Y-%m-%d-%H-%M-%S", tmp);
	return buf;
}

ostream &
operator<<(ostream &os, const BeesCrawlState &bcs)
{
	const time_t now = time(NULL);
	const auto age = now - bcs.m_started;
	return os << "BeesCrawlState "
		<< bcs.m_root << ":" << bcs.m_objectid << " offset " << to_hex(bcs.m_offset)
		<< " transid " << bcs.m_min_transid << ".." << bcs.m_max_transid
		<< " started " << format_time(bcs.m_started) << " (" << age << "s ago)";
}

BeesCrawlState::BeesCrawlState() :
	m_root(0),
	m_objectid(0),
	m_offset(0),
	m_min_transid(0),
	m_max_transid(0),
	m_started(time(NULL))
{
}

bool
BeesCrawlState::operator<(const BeesCrawlState &that) const
{
	return tie(m_min_transid, m_max_transid, m_objectid, m_offset, m_root)
		< tie(that.m_min_transid, that.m_max_transid, that.m_objectid, that.m_offset, that.m_root);
}

static
bool
is_subvol_tree(uint64_t objectid)
{
	return (objectid == BTRFS_FS_TREE_OBJECTID) || (objectid >= BTRFS_FIRST_FREE_OBJECTID);
}

class BeesScanMode : public enable_shared_from_this<BeesScanMode> {
protected:
	shared_ptr<BeesContext>	m_ctx;
	shared_ptr<BeesRoots>	m_roots;
	mutex m_scan_task_mutex;
	Task m_scan_task;
	bool crawl_one_inode(const shared_ptr<BeesCrawl>& crawl);
	/// Start a task and run the scan() method in it.  Should be called from next_transid().
	void start_scan();
	/// Scan one extent from a Task.  Should restart its task when more data available.
	virtual void scan() = 0;
public:
	virtual ~BeesScanMode() {}
	BeesScanMode(const shared_ptr<BeesRoots>& roots, const shared_ptr<BeesContext>& ctx);
	virtual const char *ntoa() const = 0;
	using CrawlMap = decltype(BeesRoots::m_root_crawl_map);
	/// Called by crawl_transid thread to indicate a change in filesystem transid.
	/// Should resume scan tasks by calling start_scan().
	virtual void next_transid() = 0;
};

BeesScanMode::BeesScanMode(const shared_ptr<BeesRoots>& roots, const shared_ptr<BeesContext>& ctx) :
	m_ctx(ctx),
	m_roots(roots)
{
}

void
BeesScanMode::start_scan()
{
	unique_lock<mutex> lock(m_scan_task_mutex);
	if (!m_scan_task) {
		const auto st = shared_from_this();
		ostringstream oss;
		oss << "scan_" << ntoa();
		m_scan_task = Task(oss.str(), [st] {
			st->scan();
		});
	}
	m_scan_task.idle();
}

struct BeesFileCrawl {
	shared_ptr<BeesContext>				m_ctx;
	shared_ptr<BeesCrawl>				m_crawl;
	shared_ptr<BeesRoots>				m_roots;
	/// Progress tracker hold object
	ProgressTracker<BeesCrawlState>::ProgressHolder m_hold;
	/// Crawl state snapshot when created
	BeesCrawlState					m_state;
	/// Currently processed offset in file
	off_t						m_offset;

	/// Method that does one unit of work for the Task
	bool scan_one_ref();
};

class BeesScanModeSubvol : public BeesScanMode {
protected:
	using CrawlMap = BeesRoots::CrawlMap;
	CrawlMap get_crawl_map();
	bool crawl_one_inode(const shared_ptr<BeesCrawl>& crawl);
public:
	virtual ~BeesScanModeSubvol() {}
	using BeesScanMode::BeesScanMode;
};

BeesScanModeSubvol::CrawlMap
BeesScanModeSubvol::get_crawl_map()
{
	return m_roots->insert_new_crawl();
}

bool
BeesScanModeSubvol::crawl_one_inode(const shared_ptr<BeesCrawl>& this_crawl)
{
	const auto this_state = this_crawl->get_state_end();
	BEESNOTE("Crawling inode " << this_state);
	BEESTRACE("Crawling inode " << this_state);
	const auto this_range = this_crawl->pop_front();
	if (!this_range) {
		return false;
	}
	const auto subvol = this_range.fid().root();
	const auto inode = this_range.fid().ino();
	ostringstream oss;
	oss << "crawl_" << subvol << "_" << inode;
	const auto task_title = oss.str();
	const auto bfc = make_shared<BeesFileCrawl>((BeesFileCrawl) {
		.m_ctx = m_ctx,
		.m_crawl = this_crawl,
		.m_roots = m_roots,
		.m_hold = this_crawl->hold_state(this_state),
		.m_state = this_state,
		.m_offset = this_range.begin(),
	});
	BEESNOTE("Starting task " << this_range);
	Task(task_title, [bfc]() {
		BEESNOTE("crawl_one_inode " << bfc->m_hold->get());
		if (bfc->scan_one_ref()) {
			// Append the current task to itself to make
			// sure we keep a worker processing this file
			Task::current_task().append(Task::current_task());
		}
	}).run();
	auto next_state = this_state;
	// Skip to EOF.  Will repeat up to 16 times if there happens to be an extent at 16EB,
	// which would be a neat trick given that off64_t is signed.
	next_state.m_offset = max(next_state.m_offset, numeric_limits<uint64_t>::max() - 65536 + 1);
	this_crawl->set_state(next_state);
	BEESCOUNT(crawl_scan);
	return true;
}

/// Scan the same inode/offset tuple in each subvol.  Good for caching and space saving,
/// bad for filesystems with rotating snapshots.
class BeesScanModeLockstep : public BeesScanModeSubvol {
	using SortKey = tuple<uint64_t, uint64_t, uint64_t>;
	using Map = map<SortKey, CrawlMap::mapped_type>;
	mutex m_mutex;
	shared_ptr<Map> m_sorted;
	void scan() override;
public:
	using BeesScanModeSubvol::BeesScanModeSubvol;
	~BeesScanModeLockstep() override {}
	void next_transid() override;
	const char *ntoa() const override;
};

const char *
BeesScanModeLockstep::ntoa() const
{
	return "LOCKSTEP";
}

void
BeesScanModeLockstep::scan()
{
	unique_lock<mutex> lock(m_mutex);
	const auto hold_sorted = m_sorted;
	lock.unlock();
	if (!hold_sorted) {
		BEESLOGINFO("called Lockstep scan without a sorted map");
		return;
	}
	auto &sorted = *hold_sorted;
	while (!sorted.empty()) {
		const auto this_crawl = sorted.begin()->second;
		sorted.erase(sorted.begin());
		const bool rv = crawl_one_inode(this_crawl);
		if (rv) {
			const auto this_range = this_crawl->peek_front();
			if (this_range) {
				const auto new_key = SortKey(this_range.fid().ino(), this_range.begin(), this_range.fid().root());
				const auto new_value = make_pair(new_key, this_crawl);
				const auto insert_rv = sorted.insert(new_value);
				THROW_CHECK0(runtime_error, insert_rv.second);
			}
			Task::current_task().idle();
			return;
		}
	}
}

void
BeesScanModeLockstep::next_transid()
{
	const auto crawl_map = get_crawl_map();
	auto new_map = make_shared<Map>();
	for (const auto &i : crawl_map) {
		if (!is_subvol_tree(i.first)) continue;
		const auto this_crawl = i.second;
		const auto this_range = this_crawl->peek_front();
		if (this_range) {
			const auto new_key = SortKey(this_range.fid().ino(), this_range.begin(), this_range.fid().root());
			const auto new_value = make_pair(new_key, this_crawl);
			const auto insert_rv = new_map->insert(new_value);
			THROW_CHECK0(runtime_error, insert_rv.second);
		}
	}
	unique_lock<mutex> lock(m_mutex);
	swap(m_sorted, new_map);
	lock.unlock();
	start_scan();
}

/// Scan each subvol in round-robin with no synchronization.
/// Good for continuous forward progress while avoiding lock contention.
class BeesScanModeIndependent : public BeesScanModeSubvol {
	using List = list<CrawlMap::mapped_type>;
	mutex m_mutex;
	shared_ptr<List> m_subvols;
	void scan() override;
public:
	using BeesScanModeSubvol::BeesScanModeSubvol;
	~BeesScanModeIndependent() override {}
	void next_transid() override;
	const char *ntoa() const override;
};

const char *
BeesScanModeIndependent::ntoa() const
{
	return "INDEPENDENT";
}

void
BeesScanModeIndependent::scan()
{
	unique_lock<mutex> lock(m_mutex);
	const auto hold_subvols = m_subvols;
	lock.unlock();
	if (!hold_subvols) {
		BEESLOGINFO("called Independent scan without a subvol list");
		return;
	}
	auto &subvols = *hold_subvols;
	while (!subvols.empty()) {
		const auto this_crawl = *subvols.begin();
		subvols.erase(subvols.begin());
		const bool rv = crawl_one_inode(this_crawl);
		if (rv) {
			subvols.push_back(this_crawl);
			Task::current_task().idle();
			return;
		}
	}
}

void
BeesScanModeIndependent::next_transid()
{
	const auto crawl_map = get_crawl_map();
	auto new_subvols = make_shared<List>();
	for (const auto &i : crawl_map) {
		if (!is_subvol_tree(i.first)) continue;
		const auto this_crawl = i.second;
		const auto this_range = this_crawl->peek_front();
		if (this_range) {
			new_subvols->push_back(this_crawl);
		}
	}
	unique_lock<mutex> lock(m_mutex);
	swap(m_subvols, new_subvols);
	lock.unlock();
	start_scan();
}

/// Scan each subvol completely, in numerical order, before moving on to the next.
/// This was an experimental mode that requires large amounts of temporary space
/// and has the lowest hit rate.
class BeesScanModeSequential : public BeesScanModeSubvol {
	using SortKey = uint64_t;
	using Map = map<SortKey, CrawlMap::mapped_type>;
	mutex m_mutex;
	shared_ptr<Map> m_sorted;
	void scan() override;
public:
	using BeesScanModeSubvol::BeesScanModeSubvol;
	~BeesScanModeSequential() override {}
	void next_transid() override;
	const char *ntoa() const override;
};

const char *
BeesScanModeSequential::ntoa() const
{
	return "SEQUENTIAL";
}

void
BeesScanModeSequential::scan()
{
	unique_lock<mutex> lock(m_mutex);
	const auto hold_sorted = m_sorted;
	lock.unlock();
	if (!hold_sorted) {
		BEESLOGINFO("called Sequential scan without a sorted map");
		return;
	}
	auto &sorted = *hold_sorted;
	while (!sorted.empty()) {
		const auto this_crawl = sorted.begin()->second;
		const bool rv = crawl_one_inode(this_crawl);
		if (rv) {
			Task::current_task().idle();
			return;
		} else {
			sorted.erase(sorted.begin());
		}
	}
}

void
BeesScanModeSequential::next_transid()
{
	const auto crawl_map = get_crawl_map();
	auto new_map = make_shared<Map>();
	for (const auto &i : crawl_map) {
		if (!is_subvol_tree(i.first)) continue;
		const auto this_crawl = i.second;
		const auto this_range = this_crawl->peek_front();
		if (this_range) {
			const auto new_key = this_range.fid().root();
			const auto new_value = make_pair(new_key, this_crawl);
			const auto insert_rv = new_map->insert(new_value);
			THROW_CHECK0(runtime_error, insert_rv.second);
		}
	}
	unique_lock<mutex> lock(m_mutex);
	swap(m_sorted, new_map);
	lock.unlock();
	start_scan();
}

/// Scan the most recently completely scanned subvols first.  Keeps recently added data
/// from accumulating in small subvols while large subvols are still undergoing their first scan.
class BeesScanModeRecent : public BeesScanModeSubvol {
	struct SortKey {
		uint64_t min_transid, max_transid;
		bool operator<(const SortKey &that) const {
			return tie(that.min_transid, that.max_transid) < tie(min_transid, max_transid);
		}
	};
	using Map = map<SortKey, list<CrawlMap::mapped_type>>;
	mutex m_mutex;
	shared_ptr<Map> m_sorted;
	void scan() override;
public:
	using BeesScanModeSubvol::BeesScanModeSubvol;
	~BeesScanModeRecent() override {}
	void next_transid() override;
	const char *ntoa() const override;
};

const char *
BeesScanModeRecent::ntoa() const
{
	return "RECENT";
}

void
BeesScanModeRecent::scan()
{
	unique_lock<mutex> lock(m_mutex);
	const auto hold_sorted = m_sorted;
	lock.unlock();
	if (!hold_sorted) {
		BEESLOGINFO("called Recent scan without a sorted map");
		return;
	}
	auto &sorted = *hold_sorted;
	while (!sorted.empty()) {
		auto &this_list = sorted.begin()->second;
		if (this_list.empty()) {
			sorted.erase(sorted.begin());
		} else {
			const auto this_crawl = *this_list.begin();
			this_list.erase(this_list.begin());
			const bool rv = crawl_one_inode(this_crawl);
			if (rv) {
				this_list.push_back(this_crawl);
				Task::current_task().idle();
				return;
			}
		}
	}
}

void
BeesScanModeRecent::next_transid()
{
	const auto crawl_map = get_crawl_map();
	auto new_map = make_shared<Map>();
	auto &sorted = *new_map;
	for (const auto &i : crawl_map) {
		if (!is_subvol_tree(i.first)) continue;
		const auto this_crawl = i.second;
		const auto this_range = this_crawl->peek_front();
		if (this_range) {
			const auto state_end = this_crawl->get_state_end();
			const auto min_transid = state_end.m_min_transid;
			// Should we use max_transid or only min_transid?
			// Using max_transid here would make it more like sequential,
			// and sequential is bad.
			// const auto max_transid = state_end.m_max_transid;
			const auto max_transid = 0;
			const SortKey key { .min_transid = min_transid, .max_transid = max_transid };
			sorted[key].push_back(this_crawl);
		}
	}
	unique_lock<mutex> lock(m_mutex);
	swap(m_sorted, new_map);
	start_scan();
}

/// Scan the extent tree and submit each extent's references in a single batch.
class BeesScanModeExtent : public BeesScanMode {
	mutex m_mutex;
	mutex m_insert_root_mutex;

	struct MagicCrawl {
		uint64_t m_min_size;
		uint64_t m_max_size;
	};
friend ostream& operator<<(ostream &os, const BeesScanModeExtent::MagicCrawl& magic);

	using MagicCrawlMap = map<uint64_t, BeesScanModeExtent::MagicCrawl>;
	static MagicCrawlMap s_magic_crawl_map;

	struct ExtentRef {
		uint64_t m_root;
		uint64_t m_inum;
		uint64_t m_offset;
		uint64_t m_length;
		ProgressTracker<BeesCrawlState>::ProgressHolder m_hold;
		Timer m_age;
	};
friend ostream& operator<<(ostream &os, const BeesScanModeExtent::ExtentRef& todo);

	struct ExtentSizeCount {
		uint64_t m_bytes = 0;
	};
	struct ExtentSizeMap {
		map<uint64_t, ExtentSizeCount> m_map;
		uint64_t m_total = 0;
	} m_extent_size;

	class SizeTier : public enable_shared_from_this<SizeTier> {
		shared_ptr<BeesScanModeExtent> m_bsme;
		shared_ptr<BeesCrawl> m_crawl;
		shared_ptr<BeesContext> m_ctx;
		uint64_t m_subvol;
		MagicCrawl m_size_range;
		Task m_scan_task;
		BtrfsDataExtentTreeFetcher m_fetcher;

	public:
		SizeTier(const shared_ptr<BeesScanModeExtent> &bsme, const uint64_t subvol, const MagicCrawl &size_range);
		void set_crawl_and_task(const shared_ptr<BeesCrawl> &crawl);
		void run_task();
		bool find_next_extent();
		void create_extent_map(const uint64_t bytenr, const ProgressTracker<BeesCrawlState>::ProgressHolder& m_hold, uint64_t len, const Task &again);
		bool scan_one_ref(const ExtentRef &bior);
		shared_ptr<BeesCrawl> crawl() const;
	};

	map<uint64_t, shared_ptr<SizeTier>> m_size_tiers;

	void init_tasks();
	void scan() override;

public:
	BeesScanModeExtent(const shared_ptr<BeesRoots>& roots, const shared_ptr<BeesContext>& ctx);
	~BeesScanModeExtent() override {}
	void next_transid() override;
	const char *ntoa() const override;
	void count_extent_size(const uint64_t size);
};

BeesScanModeExtent::SizeTier::SizeTier(const shared_ptr<BeesScanModeExtent> &bsme, const uint64_t subvol, const MagicCrawl &size_range) :
	m_bsme(bsme),
	m_ctx(m_bsme->m_ctx),
	m_subvol(subvol),
	m_size_range(size_range),
	m_fetcher(m_ctx->root_fd())
{
}

void
BeesScanModeExtent::SizeTier::set_crawl_and_task(const shared_ptr<BeesCrawl> &crawl)
{
	m_crawl = crawl;
	ostringstream oss;
	oss << "extent_" << m_subvol << "_" << pretty(m_size_range.m_min_size & ~BLOCK_MASK_CLONE)
		<< "_" << pretty(m_size_range.m_max_size);
	const auto st = shared_from_this();
	m_scan_task = Task(oss.str(), [st]() {
		st->find_next_extent();
	});
}

void
BeesScanModeExtent::SizeTier::run_task()
{
	m_scan_task.idle();
}

shared_ptr<BeesCrawl>
BeesScanModeExtent::SizeTier::crawl() const
{
	return m_crawl;
}

ostream &
operator<<(ostream &os, const BeesScanModeExtent::MagicCrawl& magic)
{
	return os << "[" << pretty(magic.m_min_size) << ".." << pretty(magic.m_max_size) << "]";
}

ostream &
operator<<(ostream &os, const BeesScanModeExtent::ExtentRef& todo)
{
	return os << todo.m_root << ":" << todo.m_inum << " " << to_hex(todo.m_offset) << "+" << pretty(todo.m_length) << " age " << todo.m_age;
}

map<uint64_t, BeesScanModeExtent::MagicCrawl> BeesScanModeExtent::s_magic_crawl_map {
	{
		BTRFS_FIRST_FREE_OBJECTID - 6,
		(BeesScanModeExtent::MagicCrawl) {
			.m_min_size = 32 * 1024 * 1024 + 1,
			.m_max_size = numeric_limits<uint64_t>::max(),
		},
	},
	{
		BTRFS_FIRST_FREE_OBJECTID - 5,
		(BeesScanModeExtent::MagicCrawl) {
			.m_min_size = 8 * 1024 * 1024 + 1,
			.m_max_size = 32 * 1024 * 1024,
		},
	},
	{
		BTRFS_FIRST_FREE_OBJECTID - 4,
		(BeesScanModeExtent::MagicCrawl) {
			.m_min_size = 2 * 1024 * 1024 + 1,
			.m_max_size = 8 * 1024 * 1024,
		},
	},
	{
		BTRFS_FIRST_FREE_OBJECTID - 3,
		(BeesScanModeExtent::MagicCrawl) {
			.m_min_size = 512 * 1024 + 1,
			.m_max_size = 2 * 1024 * 1024,
		},
	},
	{
		BTRFS_FIRST_FREE_OBJECTID - 2,
		(BeesScanModeExtent::MagicCrawl) {
			.m_min_size = 128 * 1024 + 1,
			.m_max_size = 512 * 1024,
		},
	},
	{
		BTRFS_FIRST_FREE_OBJECTID - 1,
		(BeesScanModeExtent::MagicCrawl) {
			.m_min_size = 0,
			.m_max_size = 128 * 1024,
		},
	},
};

BeesScanModeExtent::BeesScanModeExtent(const shared_ptr<BeesRoots>& roots, const shared_ptr<BeesContext>& ctx) :
	BeesScanMode(roots, ctx)
{
}

const char *
BeesScanModeExtent::ntoa() const
{
	return "EXTENT";
}

void
BeesScanModeExtent::count_extent_size(const uint64_t size)
{
	unique_lock<mutex> lock(m_mutex);
	for (auto &i : m_extent_size.m_map) {
		const auto &svm = s_magic_crawl_map.at(i.first);
		if (size >= svm.m_min_size && size <= svm.m_max_size) {
			i.second.m_bytes += size;
			break;
		}
	}
	m_extent_size.m_total += size;
}

static
bool
should_throttle()
{
	static bool s_throttled = false;
	// If there's too many entries in the queue, stop adding new ones until workers catch up
	// If there's not too many entries in the queue, restart the scan task
	const auto instance_count = Task::instance_count();
	const auto instance_limit = BEES_MAX_EXTENT_REF_COUNT;
	// Add some hysteresis so that we aren't constantly flipping throttle on and off
	const bool queue_empty = s_throttled && instance_count < instance_limit * .90;
	const bool queue_full = !s_throttled && instance_count > instance_limit * .99;
	if (queue_full) {
		BEESLOGDEBUG("Throttling crawl at " << instance_count << " tasks");
		s_throttled = true;
		BEESCOUNT(crawl_throttled);
	}
	if (queue_empty) {
		BEESLOGDEBUG("Unthrottling crawl at " << instance_count << " tasks");
		s_throttled = false;
		BEESCOUNT(crawl_unthrottled);
	}
	return s_throttled;
}

bool
BeesScanModeExtent::SizeTier::scan_one_ref(const BeesScanModeExtent::ExtentRef &bior)
{
	BEESTRACE("scan_one_ref " << bior);

	auto inode_mutex = m_ctx->get_inode_mutex(bior.m_inum);
	auto inode_lock = inode_mutex->try_lock(Task::current_task());
	if (!inode_lock) {
		BEESCOUNT(extent_deferred_inode);
		return true;
	}

	BEESNOTE("scanning root " << bior.m_root << " ino " << bior.m_inum << " offset " << to_hex(bior.m_offset) << " length " << pretty(bior.m_length));
	BEESTRACE("scanning root " << bior.m_root << " ino " << bior.m_inum << " offset " << to_hex(bior.m_offset) << " length " << pretty(bior.m_length));

	BeesFileRange bfr(
		BeesFileId(bior.m_root, bior.m_inum),
		bior.m_offset,
		bior.m_offset + bior.m_length
	);

	BEESCOUNT(extent_forward);
	const bool restart_task = m_ctx->scan_forward(bfr);
	if (restart_task) {
		BEESCOUNT(extent_restart);
	}
	return restart_task;
}

void
BeesScanModeExtent::SizeTier::create_extent_map(const uint64_t bytenr, const ProgressTracker<BeesCrawlState>::ProgressHolder& hold, const uint64_t len, const Task &next_task)
{
	BEESNOTE("Creating extent map for " << to_hex(bytenr) << " with LOGICAL_INO");
	BEESTRACE("Creating extent map for " << to_hex(bytenr) << " with LOGICAL_INO");

	const auto log_ino_ptr = m_ctx->logical_ino(bytenr, true);
	auto &log_ino = *log_ino_ptr;

	{
		BEESNOTE("waiting to create extent map for " << to_hex(bytenr) << " with LOGICAL_INO");
		auto lock = MultiLocker::get_lock("logical_ino");

		BEESNOTE("Resolving bytenr " << to_hex(bytenr) << " refs " << log_ino.m_iors.size());
		BEESTOOLONG("Resolving bytenr " << to_hex(bytenr) << " refs " << log_ino.m_iors.size());

		// Time how long this takes
		Timer resolve_timer;

		if (log_ino.do_ioctl_nothrow(m_ctx->root_fd())) {
			BEESCOUNT(extent_ok);
		} else {
			BEESCOUNT(extent_fail);
		}
		const auto resolve_age = resolve_timer.age();

		BEESCOUNTADD(extent_ms, resolve_age * 1000);
		lock.reset();
		bees_throttle(resolve_age, "extent_map");
	}

        const size_t rv_count = log_ino.m_iors.size();

	// BEESLOGDEBUG("Inserting " << rv_count << " extent refs from " << to_hex(bytenr) << ", " << Task::instance_count() << " tasks");
	BEESNOTE("Inserting " << rv_count << " extent refs from " << to_hex(bytenr));
	BEESTRACE("Inserting " << rv_count << " extent refs from " << to_hex(bytenr));

	// Avoid performance problems - pretend resolve failed if there are too many refs
        if (rv_count == 0) {
		BEESLOGDEBUG("LOGICAL_INO returned 0 refs for " << len << " bytes (" << pretty(len) << ") at " << to_hex(bytenr));
		BEESCOUNT(extent_zero);
		return;
        } else if (rv_count >= BEES_MAX_EXTENT_REF_COUNT) {
		// If we find any duplicates when there are BEES_MAX_EXTENT_REF_COUNT references, then
		// we'll end up with some extent with at least BEES_MAX_EXTENT_REF_COUNT + 1 references.
		// That's too many, so don't let that happen.
                BEESLOGINFO("bytenr " << to_hex(bytenr) << " refs " << rv_count << " overflows configured ref limit " << BEES_MAX_EXTENT_REF_COUNT);
                BEESCOUNT(extent_overflow);
		return;
	}

	BtrfsExtentDataFetcher bedf(m_ctx->root_fd());

	// Collect extent ref tasks as a series of stand-alone events
	// chained after the first task created, then run the first one.
	// This prevents other threads from starting to process an
	// extent until we have all of its refs in the queue.
	const auto refs_list = make_shared<list<ExtentRef>>();
	for (const auto &i : log_ino.m_iors) {
		catch_all([&](){
			BEESTRACE("mapping extent " << to_hex(bytenr) << " ref at root " << i.m_root << " ino " << i.m_inum << " offset " << to_hex(i.m_offset));
			BEESNOTE("mapping extent " << to_hex(bytenr) << " ref at root " << i.m_root << " ino " << i.m_inum << " offset " << to_hex(i.m_offset));

			bedf.tree(i.m_root);
			bedf.objectid(i.m_inum);
			const auto bti = bedf.at(i.m_offset);
			if (!bti) {
				// BEESLOGDEBUG("No ref for extent " << to_hex(bytenr) << " at root " << i.m_root << " ino " << i.m_inum << " offset " << to_hex(i.m_offset));
				BEESCOUNT(extent_ref_missing);
				return;
			}

			const auto length = bti.file_extent_logical_bytes();
			const ExtentRef extref = {
				.m_root = i.m_root,
				.m_inum = i.m_inum,
				.m_offset = i.m_offset,
				.m_length = length,
				.m_hold = hold,
			};

			refs_list->push_back(extref);
			BEESCOUNT(extent_ref_ok);
		});
	}
	BEESCOUNT(extent_mapped);

	// Don't need to do anything if we found no refs
	if (refs_list->empty()) {
		BEESCOUNT(extent_empty);
		return;
	}

	// Sort list in sane read/search order
	refs_list->sort([](const ExtentRef &a, const ExtentRef &b) {
		// Sort by inode first to avoid thrashing inode locks
		if (a.m_inum < b.m_inum) return -1;
		if (a.m_inum > b.m_inum) return 1;
		// Do all the reflinks on one subvol at a time so that we don't have a lot of open trees
		if (a.m_root < b.m_root) return -1;
		if (a.m_root > b.m_root) return 1;
		// Logically sequential read order because that's what both scan_one_extent and btrfs are optimized for
		if (a.m_offset < b.m_offset) return -1;
		if (a.m_offset > b.m_offset) return 1;
		return 0;
	});

	// Create task to scan all refs to this extent
	ostringstream oss;
	oss << "ref_" << hex << bytenr << "_" << pretty(len) << "_" << dec << refs_list->size();
	const auto bec = shared_from_this();
	Task crawl_one(oss.str(), [bec, refs_list, next_task]() {
		if (!refs_list->empty()) {
			const auto extref = *(refs_list->begin());
			refs_list->pop_front();
			catch_all([&]() {
				// Exceptions in scan_one_ref make us forget about this ref
				const bool restart_ref = bec->scan_one_ref(extref);
				if (restart_ref) {
					refs_list->push_front(extref);
				}
			});
		}
		if (refs_list->empty()) {
			// We might be throttled and we're about to exit a task, so restart our map task
			if (!should_throttle()) {
				next_task.idle();
			}
		} else {
			// Run the next ref
			Task::current_task().run();
		}
	});
	crawl_one.run();
}

void
BeesScanModeExtent::init_tasks()
{
	BEESTRACE("init_tasks");

	// Make sure all the magic crawlers are inserted in m_size_tiers,
	// and each one has a Task
	unique_lock<mutex> lock_insert_root(m_insert_root_mutex);
	unique_lock<mutex> lock(m_mutex);
	for (const auto &i : s_magic_crawl_map) {
		const auto subvol = i.first;
		const auto &magic = i.second;
		const auto found = m_size_tiers.find(subvol);
		if (found == m_size_tiers.end()) {
			lock.unlock();
			BeesCrawlState new_bcs;
			new_bcs.m_root = subvol;
			new_bcs.m_min_transid = m_roots->transid_min();
			new_bcs.m_max_transid = m_roots->transid_max();
			const auto this_crawl = m_roots->insert_root(new_bcs);
			lock.lock();
			const auto new_size_tier = make_shared<SizeTier>(dynamic_pointer_cast<BeesScanModeExtent>(shared_from_this()), subvol, magic);
			new_size_tier->set_crawl_and_task(this_crawl);
			m_size_tiers.insert(make_pair(subvol, new_size_tier));
		}
		m_extent_size.m_map.insert(make_pair(subvol, ExtentSizeCount {}));
	}
}

static bool bees_ordered_scan = false;

void
BeesScanModeExtent::scan()
{
	BEESTRACE("bsm scan");

	unique_lock<mutex> lock(m_mutex);
	const auto size_tiers_copy = m_size_tiers;
	lock.unlock();

	if (bees_ordered_scan) {
		// Ordered scan: Run size tier scan tasks in order until one of them finds a new extent
		for (const auto &i : size_tiers_copy) {
			if (i.second->find_next_extent()) break;
		}
	} else {
		// Parallel scan: Run all the scan tasks at once
		for (const auto &i : size_tiers_copy) {
			i.second->run_task();
		}
	}
}

bool
BeesScanModeExtent::SizeTier::find_next_extent()
{
	BEESTRACE("find_next_extent " << m_subvol);

	size_t size_low_count = 0;
	size_t size_high_count = 0;
	size_t gen_low_count = 0;
	size_t gen_high_count = 0;
	size_t loop_count = 0;
	size_t skip_count = 0;
	size_t flop_count = 0;
	size_t init_s_calls = BtrfsIoctlSearchKey::s_calls;
	size_t init_s_loops = BtrfsIoctlSearchKey::s_loops;
	Timer crawl_time;

	// Low-level extent search debugging
	shared_ptr<ostringstream> debug_oss;
#if 0
	// Enable a _lot_ of debugging output
	debug_oss = make_shared<ostringstream>();
#endif
	if (debug_oss) {
		BtrfsIoctlSearchKey::s_debug_ostream = debug_oss;
	}

	// Write out the stats no matter how we exit
	Cleanup write_stats([&]() {
		// Count stats first so we don't pollute the crawl stats with the map stats
		const auto search_calls = BtrfsIoctlSearchKey::s_calls - init_s_calls;
		const auto search_loops = BtrfsIoctlSearchKey::s_loops - init_s_loops;
		if (crawl_time.age() > 1) {
			if (loop_count) {
				BEESLOGDEBUG(
					"loop_count " << loop_count
					<< " size_low_count " << size_low_count
					<< " size_high_count " << size_high_count
					<< " gen_low_count " << gen_low_count
					<< " gen_high_count " << gen_high_count
					<< " search_calls " << search_calls
					<< " search_loops " << search_loops
					<< " skips " << skip_count
					<< " flops " << flop_count
					<< " time " << crawl_time
					<< " subvol " << m_subvol
					<< " search/loop " << pretty(search_calls / loop_count)
					<< " skip/loop " << (100 * skip_count / loop_count) << "%"
					<< " flop/loop " << (100 * flop_count / loop_count) << "%"
				);
			}
			if (debug_oss) {
				BEESLOGDEBUG("debug oss trace:\n" << debug_oss->str());
			}
		}
		BtrfsIoctlSearchKey::s_debug_ostream.reset();
	});

#define MNE_DEBUG(x) do { \
	if (debug_oss) { \
		(*debug_oss) << x << "\n"; \
	} \
} while(false)

	// Find current position
	BEESTRACE("get_state_end");
	const BeesCrawlState old_state = m_crawl->get_state_end();
	BEESNOTE("Crawling extent " << old_state);
	BEESTRACE("Crawling extent " << old_state);
	auto current_bytenr = old_state.m_objectid;
	MNE_DEBUG("current_bytenr = " << current_bytenr);

	// Hold current position until we find a new one
	auto current_hold = m_crawl->hold_state(old_state);

	// Get transid range out of the old state
	const auto min_transid = old_state.m_min_transid;
	const auto max_transid = old_state.m_max_transid;

	// Set fetcher to current transid
	m_fetcher.transid(min_transid);

	// Get the next item from the crawler
	while (!m_crawl->deferred()) {
		if (debug_oss) {
			// There is a lot of debug output.  Dump it if it gets too long
			if (!debug_oss->str().empty()) {
				if (crawl_time.age() > 1) {
					BEESLOGDEBUG("debug oss trace (so far):\n" << debug_oss->str());
					debug_oss->str("");
				}
			}
		}

		++loop_count;
		MNE_DEBUG("Loop #" << loop_count << " current_bytenr " << to_hex(current_bytenr));

		const auto bti = m_fetcher.lower_bound(current_bytenr);
		if (!bti) {
			// Ran out of data in this scan cycle
			MNE_DEBUG("Crawl finished after " << to_hex(current_bytenr));
			m_crawl->restart_crawl();
			// All of our local state is now invalid.  Restart the whole Task.
			Task::current_task().idle();
			// In ordered mode, just have the task call us again.
			return true;
		}

		// Get the position of both ends of the extent
		const auto this_bytenr = bti.objectid();
		const auto this_length = bti.offset();

		const auto next_bytenr = this_bytenr + this_length;
		// Position must not overflow
		THROW_CHECK3(runtime_error, this_bytenr, this_length, next_bytenr, next_bytenr > this_bytenr);
		// Position must advance
		THROW_CHECK2(runtime_error, current_bytenr, next_bytenr, next_bytenr > current_bytenr);

		// We are now committed to this extent, and will not retry it after an exception.
		// Move the crawl position to the next item.
		current_bytenr = next_bytenr;

		// We need BeesCrawlState objects to talk to BeesCrawl's state-tracking code
		auto this_state = old_state;
		this_state.m_objectid = this_bytenr;

		auto next_state = this_state;
		next_state.m_objectid = next_bytenr;

		// Create a holder for the next bytenr state and swap it out of the loop
		auto next_hold = m_crawl->hold_state(next_state);
		swap(current_hold, next_hold);

		// The pointer formerly stored in current_hold will persist until the end of this loop iteration.
		// The pointer formerly stored in next_hold will persist until this point in the next loop iteration.

		MNE_DEBUG("this_bytenr = " << to_hex(this_bytenr) << ", this_length = " << pretty(this_length) << ", next_bytenr = " << to_hex(next_bytenr));

		// In mixed-bg filesystems there are metadata objects mixed in with data objects.
		// Skip over TREE_BLOCK extent items, they don't have files.
		if (bti.extent_flags() & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
			MNE_DEBUG("BTRFS_EXTENT_FLAG_TREE_BLOCK");
			BEESCOUNT(crawl_tree_block);
			continue;
		}

		// Calculate average proportion of each extent size
		m_bsme->count_extent_size(this_length);

		// Check extent length against size range
		const uint64_t lower_size_bound = m_size_range.m_min_size;
		const uint64_t upper_size_bound = m_size_range.m_max_size;

		// If this extent is out of range, move on to the next
		if (this_length < lower_size_bound || this_length > upper_size_bound) {
			MNE_DEBUG("discard: this_length = " << pretty(this_length) << " lower_size_bound = " << pretty(lower_size_bound) << " upper_size_bound " << pretty(upper_size_bound));

			// If extent is longer than max_size, the extent will advance faster than the skip
			if (this_length > upper_size_bound) {
				++size_high_count;
				BEESCOUNT(crawl_discard_high);
				continue;
			}

			// Must be small then
			BEESCOUNT(crawl_discard_low);
			++size_low_count;

			// Skip ahead over any below-min-size extents
			BEESTRACE("min_size " << pretty(lower_size_bound) << " > scale_size " << pretty(m_fetcher.scale_size()));
			const auto lsb_rounded = lower_size_bound & ~(m_fetcher.scale_size() - 1);
			// Don't bother doing backward searches when skipping 128K or less.
			// The search will cost more than reading 32 consecutive extent records.
			if (lsb_rounded <= 128 * 1024) {
				continue;
			}

			Timer skip_timer;

			// Choose a point that would be inside a min-size extent which starts here
			const auto objectid = next_bytenr + lsb_rounded - m_fetcher.scale_size();
			BEESTRACE("objectid = " << next_bytenr << ", adjusted to " << objectid);
			MNE_DEBUG("objectid = " << next_bytenr << ", adjusted to " << objectid);
			BEESTOOLONG("subvol " << m_subvol << " skipping forward " << pretty(lsb_rounded) << " from " << to_hex(this_state.m_objectid) << " to " << to_hex(objectid));
			BEESNOTE("subvol " << m_subvol << " skipping forward " << pretty(lsb_rounded) << " from " << to_hex(this_state.m_objectid) << " to " << to_hex(objectid));
			MNE_DEBUG("subvol " << m_subvol << " skipping forward " << pretty(lsb_rounded) << " from " << to_hex(this_state.m_objectid) << " to " << to_hex(objectid));

			// Find an extent which starts before the chosen point
			const auto bti = m_fetcher.rlower_bound(objectid);
			if (!bti) {
				MNE_DEBUG("Crawl finished: skip found no extent after " << to_hex(objectid));
				// Keep going without skipping
				continue;
			}
			const auto new_bytenr = bti.objectid();
			if (new_bytenr <= next_bytenr) {
				MNE_DEBUG("flopped: new_bytenr " << to_hex(new_bytenr) << " < next_bytenr " << to_hex(next_bytenr));
				// No extent found between end of current extent and chosen point, skip ahead to end of current extent
				// ...which is where current_bytenr already is, so no need to change anything
				BEESCOUNT(crawl_flop);
				++flop_count;
			} else {
				MNE_DEBUG("skipped: new_bytenr " << to_hex(new_bytenr) << " > next_bytenr " << to_hex(next_bytenr));
				// We skipped over some extents, start with the one we landed in
				current_bytenr = new_bytenr;
				BEESCOUNT(crawl_skip);
				++skip_count;
			}
			BEESCOUNTADD(crawl_skip_ms, skip_timer.age() * 1000);
			continue;
		}

		// Check extent item generation is in range
		const auto gen = bti.extent_generation();
		if (gen < min_transid) {
			MNE_DEBUG("generation " << gen << " < " << min_transid);
			BEESCOUNT(crawl_gen_low);
			++gen_low_count;
			continue;
		}
		if (gen > max_transid) {
			MNE_DEBUG("generation " << gen << " > " << max_transid);
			BEESCOUNT(crawl_gen_high);
			++gen_high_count;
			continue;
		}

		const auto find_next_task = Task::current_task();
		// Note next_hold >= this_hold > current_hold after the swap above,
		// so the extent map may or may not advance the BeesCrawlState position.
		const auto hold_state = m_crawl->hold_state(this_state);
		const auto sft = shared_from_this();
		ostringstream oss;
		oss << "map_" << to_hex(this_bytenr) << "_" << pretty(this_length);
		Task create_map_task(oss.str(), [sft, this_bytenr, hold_state, this_length, find_next_task]() {
			sft->create_extent_map(this_bytenr, hold_state, this_length, find_next_task);
			BEESCOUNT(crawl_extent);
		});
		create_map_task.run();

		// We did something!  Get in line to run again...unless we're throttled
		if (!should_throttle()) {
			Task::current_task().idle();
		}
		return true;
	}

	// Crawl state is updated by holder destructors

	// All crawls done
	BEESCOUNT(crawl_done);
	return false;
}

static
string
strf_localtime(const time_t &when)
{
	struct tm ltm = { 0 };
	DIE_IF_ZERO(localtime_r(&when, &ltm));

	char buf[100] = { 0 };
	DIE_IF_ZERO(strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &ltm));
	return buf;
}

void
BeesScanModeExtent::next_transid()
{
	BEESTRACE("Extent next_transid");

	// Do the important parts first, the rest can return early or die with an exception

	// Can't set this up in the constructor because shared_from_this is a method on a
	// virtual base.  So we do it here.
	init_tasks();

	// insert_root does this for non-magic subvols, we have to do it ourselves
	map<uint64_t,pair<bool,bool>> deferred_map;
	{
		unique_lock<mutex> lock(m_mutex);
		for (const auto &i : s_magic_crawl_map) {
			const auto subvol = i.first;
			const auto found = m_size_tiers.find(subvol);
			if (found != m_size_tiers.end()) {
				const auto crawl = found->second->crawl();
				// Have to save these for the progress table
				deferred_map.insert(make_pair(subvol, make_pair(crawl->deferred(), crawl->finished())));

				// If the crawl has stopped (e.g. due to running out of data), then restart it here
				crawl->deferred(false);
			}
		}
	}

	// Kick off tasks if they aren't already running
	start_scan();

	// Estimate progress by building a map of where the extent bytenrs are (they are sparse,
	// no extents exist between block groups), and report the position within that map.

	BtrfsTreeOffsetFetcher btf(m_ctx->root_fd());
        btf.tree(BTRFS_CHUNK_TREE_OBJECTID);
        btf.objectid(BTRFS_FIRST_CHUNK_TREE_OBJECTID);
        btf.type(BTRFS_CHUNK_ITEM_KEY);
	uint64_t fs_size = 0;
	uint64_t last_bgaddr = 0;
	struct bg_info {
		uint64_t first_bytenr;
		uint64_t first_total;
	};
	map<uint64_t, bg_info> bg_info_map;
	while (true) {
		const auto bti = btf.lower_bound(last_bgaddr);
		if (!bti) {
			break;
		}
		const auto offset = bti.offset();
		const auto chunk_length = bti.chunk_length();
		THROW_CHECK0(runtime_error, offset > 0);
		THROW_CHECK0(runtime_error, chunk_length > 0);
		last_bgaddr = offset + chunk_length;
		// Mixed-bg filesystems have block groups that are data _and_ metadata.
		// Block groups that are _only_ metadata should be filtered out.
		if (0 == (bti.chunk_type() & BTRFS_BLOCK_GROUP_DATA)) {
			continue;
		}
		bg_info_map[last_bgaddr] = (bg_info) {
			.first_bytenr = offset,
			.first_total = fs_size,
		};
		fs_size += chunk_length;
	}

	if (!fs_size) {
		BEESLOGDEBUG("PROGRESS: no data block groups found in filesystem");
		BEESCOUNT(progress_no_data_bg);
		return;
	}

	// Grab a copy of members
	unique_lock<mutex> lock(m_mutex);
	const auto mes = m_extent_size;
	const auto cmc = m_size_tiers;

	// Decay the extent size map averages
	static const double decay = .99;
	for (auto &i : m_extent_size.m_map) {
		i.second.m_bytes *= decay;
	}
	m_extent_size.m_total *= decay;
	lock.unlock();
	const bool mes_sample_size_ok = mes.m_total > fs_size * .001;

	// Report on progress using extent bytenr map
	Table::Table eta;
	eta.insert_row(0, vector<Table::Content> {
		Table::Text("extsz"),
		Table::Text("datasz"),
		Table::Text("point"),
		Table::Text("gen_min"),
		Table::Text("gen_max"),
		Table::Text("this cycle start"),
		Table::Text("tm_left"),
		Table::Text("next cycle ETA"),
	});
	const auto dash_fill = Table::Fill('-');
	eta.insert_row(1, vector<Table::Content>(eta.cols().size(), dash_fill));
	const auto now = time(NULL);
	for (const auto &i : s_magic_crawl_map) {
		const auto &subvol = i.first;
		const auto &magic = i.second;

		const auto found = cmc.find(subvol);
		THROW_CHECK1(runtime_error, subvol, found != cmc.end());
		if (found == cmc.end()) {
			// BEESLOGDEBUG("PROGRESS: crawler not yet created for " << magic);
			BEESCOUNT(progress_not_created);
			continue;
		}

		const auto this_crawl = found->second->crawl();
		THROW_CHECK1(runtime_error, subvol, this_crawl);

		// Get the last _completed_ state
		const auto this_state = this_crawl->get_state_begin();

		auto bytenr = this_state.m_objectid;
		const auto bg_found = bg_info_map.lower_bound(bytenr);
		if (bg_found == bg_info_map.end()) {
			BEESLOGDEBUG("PROGRESS: bytenr " << to_hex(bytenr) << " not found in a block group for " << magic);
			BEESCOUNT(progress_not_found);
			continue;
		}
		const auto &bi = bg_found->second;
		if (!bytenr) {
			// Zero bytenr means we have just started a crawl.  Point to the first defined bytenr instead
			bytenr = bi.first_bytenr;
		}
		const auto bi_last_bytenr = bg_found->first;
		if (bytenr > bi_last_bytenr || bytenr < bi.first_bytenr) {
			// This can happen if the crawler happens to be in a metadata block group,
			// or if a block group was deleted under us.
			BEESLOGDEBUG("PROGRESS: bytenr " << to_hex(bytenr) << " out of range for block group " << to_hex(bi.first_bytenr) << ".." << to_hex(bg_found->first) << " (block group deleted?) for " << magic);
			BEESCOUNT(progress_out_of_bg);
		}
		const auto bytenr_offset = min(bi_last_bytenr, max(bytenr, bi.first_bytenr)) - bi.first_bytenr + bi.first_total;
		const auto bytenr_norm = bytenr_offset / double(fs_size);
		const auto time_so_far = now - min(now, this_state.m_started);
		const string start_stamp = strf_localtime(this_state.m_started);
		string eta_stamp = "-";
		string eta_pretty = "-";
		const auto &deferred_finished = deferred_map.at(subvol);
		const bool finished = deferred_finished.second;
		if (finished) {
			// eta_stamp = "idle";
		} else if (time_so_far > 10 && bytenr_offset > 1024 * 1024 * 1024) {
			const time_t eta_duration = time_so_far / bytenr_norm;
			const time_t eta_time = eta_duration + now;
			eta_stamp = strf_localtime(eta_time);
			eta_pretty = pretty_seconds(eta_duration);
		}
		const auto &mma = mes.m_map.at(subvol);
		const auto mma_ratio = mes_sample_size_ok ? (mma.m_bytes / double(mes.m_total)) : 1.0;
		const auto posn_text = Table::Text(finished ? "idle" : astringprintf("%06d", int(floor(bytenr_norm * 1000000))));
		const auto size_text = Table::Text( mes_sample_size_ok ? pretty(fs_size * mma_ratio) : "-");
		eta.insert_row(Table::endpos, vector<Table::Content> {
			Table::Text(magic.m_max_size == numeric_limits<uint64_t>::max() ? "max" : pretty(magic.m_max_size)),
			size_text,
			posn_text,
			Table::Number(this_state.m_min_transid),
			Table::Number(this_state.m_max_transid),
			Table::Text(start_stamp),
			Table::Text(eta_pretty),
			Table::Text(eta_stamp),
		});
		BEESCOUNT(progress_ok);
	}
	eta.insert_row(Table::endpos, vector<Table::Content> {
		Table::Text("total"),
		Table::Text(pretty(fs_size)),
		Table::Text(""),
		Table::Text("gen_now"),
		Table::Number(m_roots->transid_max()),
		Table::Text(""),
		Table::Text("updated"),
		Table::Text(strf_localtime(now)),
	});
	eta.left("");
	eta.mid(" ");
	eta.right("");

	// One for publication through beesstats.txt and $BEESSTATUS
	{
		ostringstream progress_oss;
		progress_oss << eta;
		m_ctx->set_progress(progress_oss.str());
	}

	// One for the debug log
	{
		eta.left("PROGRESS: ");
		ostringstream log_oss;
		log_oss << eta;
		BEESLOGINFO(log_oss.str());
	}
}

void
BeesRoots::set_scan_mode(ScanMode mode)
{
	THROW_CHECK1(invalid_argument, mode, mode < SCAN_MODE_COUNT);
	unique_lock<mutex> lock(m_mutex);
	switch (mode) {
		case SCAN_MODE_LOCKSTEP: {
			m_scanner = make_shared<BeesScanModeLockstep>(shared_from_this(), m_ctx);
			break;
		}
		case SCAN_MODE_INDEPENDENT: {
			m_scanner = make_shared<BeesScanModeIndependent>(shared_from_this(), m_ctx);
			break;
		}
		case SCAN_MODE_SEQUENTIAL: {
			m_scanner = make_shared<BeesScanModeSequential>(shared_from_this(), m_ctx);
			break;
		}
		case SCAN_MODE_RECENT: {
			m_scanner = make_shared<BeesScanModeRecent>(shared_from_this(), m_ctx);
			break;
		}
		case SCAN_MODE_EXTENT: {
			m_scanner = make_shared<BeesScanModeExtent>(shared_from_this(), m_ctx);
			break;
		}
		case SCAN_MODE_COUNT:
		default:
			assert(false);
			break;
	}
	BEESLOGINFO("Scan mode set to " << mode << " (" << m_scanner->ntoa() << ")");
}

void
BeesRoots::set_workaround_btrfs_send(bool do_avoid)
{
	m_workaround_btrfs_send = do_avoid;
	if (m_workaround_btrfs_send) {
		BEESLOGINFO("WORKAROUND: btrfs send workaround enabled");
	} else {
		BEESLOGINFO("btrfs send workaround disabled");
	}
}

string
BeesRoots::crawl_state_filename() const
{
	// Legacy filename included UUID.  That feature was removed in 2016.
	return "beescrawl.dat";
}

ostream &
BeesRoots::state_to_stream(ostream &ofs)
{
	for (auto i : m_root_crawl_map) {
		auto ibcs = i.second->get_state_begin();
		if (ibcs.m_max_transid) {
			ofs << "root "        << ibcs.m_root                 << " ";
			ofs << "objectid "    << ibcs.m_objectid             << " ";
			ofs << "offset "      << ibcs.m_offset               << " ";
			ofs << "min_transid " << ibcs.m_min_transid          << " ";
			ofs << "max_transid " << ibcs.m_max_transid          << " ";
			ofs << "started "     << ibcs.m_started              << " ";
			ofs << "start_ts "    << format_time(ibcs.m_started) << "\n";
		}
	}
	return ofs;
}

void
BeesRoots::state_save()
{
	BEESNOTE("saving crawl state");
	BEESLOGINFO("Saving crawl state");
	BEESTOOLONG("Saving crawl state");

	Timer save_time;

	unique_lock<mutex> lock(m_mutex);

	// We don't have ofstreamat or ofdstream in C++11, so we're building a string and writing it with raw syscalls.
	ostringstream ofs;

	if (m_crawl_clean == m_crawl_dirty) {
		BEESLOGINFO("Nothing to save");
		return;
	}

	state_to_stream(ofs);
	const auto crawl_saved = m_crawl_dirty;

	if (ofs.str().empty()) {
		BEESLOGWARN("Crawl state empty!");
		m_crawl_clean = crawl_saved;
		return;
	}

	lock.unlock();

	// This may throw an exception, so we didn't save the state we thought we did.
	m_crawl_state_file.write(ofs.str());

	BEESNOTE("relocking crawl state to update dirty/clean state");
	lock.lock();
	// This records the version of the crawl state we saved, which is not necessarily the current state
	m_crawl_clean = crawl_saved;
	BEESLOGINFO("Saved crawl state in " << save_time << "s");
}

void
BeesRoots::crawl_state_set_dirty()
{
	unique_lock<mutex> lock(m_mutex);
	++m_crawl_dirty;
}

void
BeesRoots::crawl_state_erase(const BeesCrawlState &bcs)
{
	unique_lock<mutex> lock(m_mutex);

	// Do not delete the last entry, it holds our max_transid
	if (m_root_crawl_map.size() < 2) {
		BEESCOUNT(crawl_no_empty);
		return;
	}

	if (m_root_crawl_map.count(bcs.m_root)) {
		m_root_crawl_map.erase(bcs.m_root);
		++m_crawl_dirty;
	}
}

uint64_t
BeesRoots::transid_min()
{
	uint64_t rv = numeric_limits<uint64_t>::max();
	uint64_t last_root = 0;
	BEESNOTE("Calculating transid_min (" << rv << " so far, last_root " << last_root << ")");
	unique_lock<mutex> lock(m_mutex);
	if (m_root_crawl_map.empty()) {
		return 0;
	}
	const uint64_t max_rv = rv;
	for (auto i : m_root_crawl_map) {
		// Do not count subvols that are isolated by btrfs send workaround.
		// They will not advance until the workaround is removed or they are set read-write.
		catch_all([&](){
			if (!is_root_ro(i.first)) {
				rv = min(rv, i.second->get_state_end().m_min_transid);
			}
		});
		last_root = i.first;
	}
	// If we get through this loop without setting rv, we'll create broken crawlers due to integer overflow.
	THROW_CHECK2(runtime_error, rv, max_rv, max_rv > rv);
	return rv;
}

uint64_t
BeesRoots::transid_max_nocache()
{
	BEESNOTE("Calculating transid_max");
	BEESTRACE("Calculating transid_max");

	// We look for the root of the extent tree and read its transid.
	// Should run in O(1) time and be fairly reliable.
	BtrfsRootFetcher root_fetcher(m_ctx->root_fd());
	const auto bti = root_fetcher.root(BTRFS_EXTENT_TREE_OBJECTID);
	BEESTRACE("extracting transid from " << bti);
	const auto rv = bti.transid();

	// transid must be greater than zero, or we did something very wrong
	THROW_CHECK1(runtime_error, rv, rv > 0);
	// transid must be less than max, or we did something very wrong
	THROW_CHECK1(runtime_error, rv, rv < numeric_limits<uint64_t>::max());

	// Update the rate estimator
	m_transid_re.update(rv);
	return rv;
}

uint64_t
BeesRoots::transid_max()
{
	const auto rv = m_transid_re.count();
	// transid must be greater than zero, or we did something very wrong
	THROW_CHECK1(runtime_error, rv, rv > 0);
	// transid must be less than max, or we did something very wrong
	THROW_CHECK1(runtime_error, rv, rv < numeric_limits<uint64_t>::max());
	return rv;
}

bool
BeesFileCrawl::scan_one_ref()
{
	BEESNOTE("scan_one_ref m_offset " << to_hex(m_offset) << " state " << m_state);
	BEESTRACE("scan_one_ref m_offset " << to_hex(m_offset) << " state " << m_state);

	BtrfsExtentDataFetcher bedf(m_ctx->root_fd());
	bedf.tree(m_state.m_root);
	bedf.objectid(m_state.m_objectid);
	bedf.transid(m_state.m_min_transid);

	// Only one thread can dedupe a file.  btrfs will lock others out.
	// Inodes are usually full of shared extents, especially in the case of snapshots,
	// so when we lock an inode, we'll lock the same inode number in all subvols at once.
	auto inode_mutex = m_ctx->get_inode_mutex(bedf.objectid());
	auto inode_lock = inode_mutex->try_lock(Task::current_task());
	if (!inode_lock) {
		BEESCOUNT(crawl_deferred_inode);
		// Returning false here means we won't reschedule ourselves, but inode_mutex will do that
		return false;
	}

	// If we hit an exception here we don't try to catch it.
	// It will mean the file or subvol was deleted or there's metadata corruption,
	// and we should stop trying to scan the inode in that case.
	// The calling Task will be aborted.
	const auto bti = bedf.lower_bound(m_offset);
	if (!bti) {
		return false;
	}
	// Make sure we advance
	m_offset = max(bti.offset() + bedf.block_size(), bti.offset());
	// Check extent item generation is in range
	const auto gen = bti.file_extent_generation();
	if (gen < m_state.m_min_transid) {
		BEESCOUNT(crawl_gen_low);
		// The header generation refers to the transid
		// of the metadata page holding the current ref.
		// This includes anything else in that page that
		// happened to be modified, regardless of how
		// old it is.
		// The file_extent_generation refers to the
		// transid of the extent item's page, which is
		// what we really want when we are slicing up
		// the extent data by transid.
		return true;
	}
	if (gen > m_state.m_max_transid) {
		BEESCOUNT(crawl_gen_high);
		// We want to see old extents with references in
		// new pages, which means we have to get extent
		// refs from every page older than min_transid,
		// not every page between min_transid and
		// max_transid.  This means that we will get
		// refs to new extent data that we don't want to
		// process yet, because we'll process it again
		// on the next crawl cycle.  We filter out refs
		// to new extents here.
		return true;
	}

	const auto type = bti.file_extent_type();
	switch (type) {
		default:
			BEESLOGDEBUG("Unhandled file extent type " << btrfs_search_type_ntoa(type) << " in root " << m_state.m_root << " " << bti);
			BEESCOUNT(crawl_unknown);
			break;
		case BTRFS_FILE_EXTENT_INLINE:
			// Ignore these for now.
			// TODO:  replace with out-of-line dup extents
			BEESCOUNT(crawl_inline);
			break;
		case BTRFS_FILE_EXTENT_PREALLOC:
			BEESCOUNT(crawl_prealloc);
			// fallthrough
		case BTRFS_FILE_EXTENT_REG: {
			const auto physical = bti.file_extent_bytenr();
			const auto len = bti.file_extent_logical_bytes();
			BEESTRACE("Root " << m_state.m_root << " ino " << bti.objectid() << " physical " << to_hex(physical)
				<< " logical " << to_hex(bti.offset()) << ".." << to_hex(bti.offset() + len)
				<< " gen " << gen);
			if (physical) {
				THROW_CHECK1(runtime_error, len, len > 0);
				BeesFileId bfi(m_state.m_root, bti.objectid());
				if (m_ctx->is_blacklisted(bfi)) {
					BEESCOUNT(crawl_blacklisted);
				} else {
					BeesFileRange bfr(bfi, bti.offset(), bti.offset() + len);
					BEESCOUNT(crawl_push);
					auto bcs = m_state;
					bcs.m_objectid = bfr.fid().ino();
					bcs.m_offset = bfr.begin();
					const auto new_holder = m_crawl->hold_state(bcs);
					// If we hit an exception here, ignore it.
					// It might be corrupted data, the file might have been deleted or truncated,
					// or we might hit some other recoverable error.  We'll try again with
					// the next extent.
					bool scan_again = false;
					catch_all([&]() {
						BEESNOTE("scan_forward " << bfr);
						// BEESLOGDEBUG("scan_forward #" << Task::current_task().id() << " " << bfr);
						scan_again = m_ctx->scan_forward(bfr);
						// BEESLOGDEBUG("done_forward #" << Task::current_task().id() << " " << bfr);
					} );
					if (!scan_again) {
						m_hold = new_holder;
					} else {
						BEESCOUNT(crawl_again);
					}
				}
			} else {
				BEESCOUNT(crawl_hole);
			}
			break;
		}
	}
	return true;
}

void
BeesRoots::clear_caches()
{
	m_ctx->fd_cache()->clear();
	m_ctx->resolve_cache_clear();
}

void
BeesRoots::wait_for_transid(const uint64_t count)
{
	const auto now_transid = transid_max_nocache();
	const auto target_transid = now_transid + count;
	BEESLOGDEBUG("Waiting for transid " << target_transid << ", current transid is " << now_transid);
	m_transid_re.wait_until(target_transid);
}

void
BeesRoots::crawl_thread()
{
	BEESNOTE("creating crawl task");

	// Create the Task that does the crawling
	const auto shared_this = shared_from_this();
	const auto crawl_new = Task("crawl_new", [shared_this]() {
		BEESTRACE("crawl_new " << shared_this);
		catch_all([&]() {
			shared_this->m_scanner->next_transid();
		});
	});

	// Monitor transid_max and wake up roots when it changes
	BEESNOTE("tracking transid");
	uint64_t last_transid = 0;
	while (!m_stop_requested) {
		BEESTRACE("Measure current transid");
		catch_all([&]() {
			BEESTRACE("calling transid_max_nocache");
			// Will update m_transid_re as side effect
			transid_max_nocache();
		});

		const auto new_transid = m_transid_re.count();
		if (new_transid != last_transid) {
			// Don't hold root FDs open too long.
			// The open FDs prevent snapshots from being deleted.
			// cleaner_kthread just keeps skipping over the open dir and all its children.
			// Even open files are a problem if they're big enough.
			// Always run this even if we have no worker threads.
			clear_caches();

			// Insert new roots and restart crawl_more.
			crawl_new.run();
		}
		last_transid = new_transid;

		const auto poll_time = max(BEES_TRANSID_POLL_INTERVAL, m_transid_re.seconds_for(1));
		BEESLOGDEBUG("Polling " << poll_time << "s for next transid " << m_transid_re);
		BEESNOTE("waiting " << poll_time << "s for next transid " << m_transid_re);
		unique_lock<mutex> lock(m_stop_mutex);
		if (m_stop_requested) {
			BEESLOGDEBUG("Stop requested in crawl thread");
			break;
		}
		m_stop_condvar.wait_for(lock, chrono::duration<double>(poll_time));
	}
}

void
BeesRoots::writeback_thread()
{
	while (true) {
		BEESNOTE("idle, " << (m_crawl_clean != m_crawl_dirty ? "dirty" : "clean"));

		catch_all([&]() {
			BEESNOTE("saving crawler state");
			state_save();
		});

		unique_lock<mutex> lock(m_stop_mutex);
		if (m_stop_requested) {
			BEESLOGDEBUG("Stop requested in writeback thread");
			catch_all([&]() {
				BEESNOTE("flushing crawler state");
				state_save();
			});
			return;
		}
		m_stop_condvar.wait_for(lock, chrono::duration<double>(BEES_WRITEBACK_INTERVAL));
	}
}

shared_ptr<BeesCrawl>
BeesRoots::insert_root(const BeesCrawlState &new_bcs)
{
	unique_lock<mutex> lock(m_mutex);
	if (!m_root_crawl_map.count(new_bcs.m_root)) {
		auto new_bcp = make_shared<BeesCrawl>(m_ctx, new_bcs);
		auto new_pair = make_pair(new_bcs.m_root, new_bcp);
		m_root_crawl_map.insert(new_pair);
		++m_crawl_dirty;
	}
	const auto found = m_root_crawl_map.find(new_bcs.m_root);
	THROW_CHECK0(runtime_error, found != m_root_crawl_map.end());
	found->second->deferred(false);
	return found->second;
}

BeesRoots::CrawlMap
BeesRoots::insert_new_crawl()
{
	BEESNOTE("adding crawlers for new subvols and removing crawlers for removed subvols");

	BeesCrawlState new_bcs;
	// Avoid a wasted loop iteration by starting from root 5
	new_bcs.m_root = BTRFS_FS_TREE_OBJECTID;
	new_bcs.m_min_transid = transid_min();
	new_bcs.m_max_transid = transid_max();

	unique_lock<mutex> lock(m_mutex);
	set<uint64_t> excess_roots;
	for (const auto &i : m_root_crawl_map) {
		if (is_subvol_tree(i.first)) {
			BEESTRACE("excess_roots.insert(" << i.first << ")");
			excess_roots.insert(i.first);
		}
	}
	lock.unlock();

	while (new_bcs.m_root) {
		BEESTRACE("excess_roots.erase(" << new_bcs.m_root << ")");
		excess_roots.erase(new_bcs.m_root);
		BEESTRACE("insert_root(" << new_bcs << ")");
		insert_root(new_bcs);
		BEESCOUNT(crawl_create);
		BEESTRACE("next_root(" << new_bcs.m_root << ")");
		new_bcs.m_root = next_root(new_bcs.m_root);
	}

	for (const auto &i : excess_roots) {
		new_bcs.m_root = i;
		BEESTRACE("crawl_state_erase(" << new_bcs << ")");
		crawl_state_erase(new_bcs);
	}

	BEESNOTE("rebuilding crawl map");
	BEESTRACE("rebuilding crawl map");

	lock.lock();

	// Work from a copy because BeesCrawl might change the world under us
	const auto crawl_map_copy = m_root_crawl_map;
	lock.unlock();

	// Nothing to crawl?  Seems suspicious...
	if (crawl_map_copy.empty()) {
		BEESLOGINFO("crawl map is empty!");
	}

	return crawl_map_copy;
}

void
BeesRoots::state_load()
{
	BEESNOTE("loading crawl state");
	BEESLOGINFO("loading crawl state");

	string crawl_data = m_crawl_state_file.read();

	for (auto line : split("\n", crawl_data)) {
		BEESLOGDEBUG("Read line: " << line);
		map<string, uint64_t> d;
		auto words = split(" ", line);
		for (auto it = words.begin(); it < words.end(); ++it) {
			auto it1 = it;
			++it;
			THROW_CHECK1(out_of_range, words.size(), it < words.end());
			string key = *it1;
			uint64_t val = from_hex(*it);
			BEESTRACE("key " << key << " val " << val);
			auto result = d.insert(make_pair(key, val));
			THROW_CHECK0(runtime_error, result.second);
		}
		BeesCrawlState loaded_state;
		loaded_state.m_root        = d.at("root");
		loaded_state.m_objectid    = d.at("objectid");
		loaded_state.m_offset      = d.at("offset");
		loaded_state.m_min_transid = d.count("gen_current") ? d.at("gen_current") : d.at("min_transid");
		loaded_state.m_max_transid = d.count("gen_next") ? d.at("gen_next") : d.at("max_transid");
		if (d.count("started")) {
			loaded_state.m_started = d.at("started");
		}
		BEESLOGDEBUG("loaded_state " << loaded_state);
		if (loaded_state.m_min_transid == numeric_limits<uint64_t>::max()) {
			BEESLOGWARN("WARNING: root " << loaded_state.m_root << ": bad min_transid " << loaded_state.m_min_transid << ", resetting to 0");
			loaded_state.m_min_transid = 0;
			BEESCOUNT(bug_bad_min_transid);
		}
		if (loaded_state.m_max_transid == numeric_limits<uint64_t>::max()) {
			BEESLOGWARN("WARNING: root " << loaded_state.m_root << ": bad max_transid " << loaded_state.m_max_transid << ", resetting to " << loaded_state.m_min_transid);
			loaded_state.m_max_transid = loaded_state.m_min_transid;
			BEESCOUNT(bug_bad_max_transid);
		}
		insert_root(loaded_state);
	}
}

BeesRoots::BeesRoots(shared_ptr<BeesContext> ctx) :
	m_ctx(ctx),
	m_crawl_state_file(ctx->home_fd(), crawl_state_filename()),
	m_crawl_thread("crawl_transid"),
	m_writeback_thread("crawl_writeback")
{
}

void
BeesRoots::start()
{
	m_crawl_thread.exec([&]() {
		// Measure current transid before creating any crawlers
		catch_all([&]() {
			transid_max_nocache();
		});

		// Make sure we have a full complement of crawlers
		catch_all([&]() {
			state_load();
		});

		m_writeback_thread.exec([&]() {
			writeback_thread();
		});
		crawl_thread();
	});
}

void
BeesRoots::stop_request()
{
	BEESLOGDEBUG("BeesRoots stop requested");
	BEESNOTE("stopping BeesRoots");
	unique_lock<mutex> lock(m_stop_mutex);
	m_stop_requested = true;
	m_stop_condvar.notify_all();
}

void
BeesRoots::stop_wait()
{
	// Stop crawl writeback first because we will break progress
	// state tracking when we cancel the TaskMaster queue
	BEESLOGDEBUG("Waiting for crawl writeback");
	BEESNOTE("waiting for crawl_writeback thread");
	m_writeback_thread.join();

	BEESLOGDEBUG("Waiting for crawl thread");
	BEESNOTE("waiting for crawl_thread thread");
	m_crawl_thread.join();

	BEESLOGDEBUG("BeesRoots stopped");
}

static
Fd
bees_openat(int const parent_fd, const char *const pathname, uint64_t const flags)
{
	// Never O_CREAT so we don't need a mode argument
	THROW_CHECK1(invalid_argument, flags, (flags & O_CREAT) == 0);

	// Try openat2 if the kernel has it
	static bool can_openat2 = true;
	if (can_openat2) {
		open_how how {
			.flags = flags,
			.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV,
		};
		const auto rv = openat2(parent_fd, pathname, &how, sizeof(open_how));
		if (rv == -1 && errno == ENOSYS) {
			BEESLOGWARN("openat2 returns ENOSYS, falling back to openat");
			can_openat2 = false;
		} else {
			return Fd(rv);
		}
	}

	// No kernel support, use openat instead
	return Fd(openat(parent_fd, pathname, flags));
}

Fd
BeesRoots::open_root_nocache(uint64_t rootid)
{
	BEESTRACE("open_root_nocache " << rootid);
	BEESNOTE("open_root_nocache " << rootid);

	// Stop recursion at the root of the filesystem tree
	if (rootid == BTRFS_FS_TREE_OBJECTID) {
		return m_ctx->root_fd();
	}

	// Find backrefs for this rootid and follow up to root
	BtrfsRootFetcher brf(m_ctx->root_fd());
	const auto bti = brf.root_backref(rootid);
	if (!bti) {
		BEESLOGDEBUG("No path for rootid " << rootid);
		BEESCOUNT(root_notfound);
		return Fd();
	}

	// Get dirid (path from parent subvol's root to this subvol) and basename (in that dirid)
	const auto dirid = bti.root_ref_dirid();
	const auto name = bti.root_ref_name();
	const auto parent_rootid = bti.offset();
	BEESTRACE("parent_rootid " << parent_rootid << " dirid " << dirid << " name " << name);

	// Try to open the parent recursively
	BEESCOUNT(root_parent_open_try);
	Fd parent_fd = open_root(parent_rootid);
	if (!parent_fd) {
		BEESLOGDEBUG("no parent_fd for " << parent_rootid);
		BEESCOUNT(root_parent_open_fail);
		return Fd();
	}
	BEESCOUNT(root_parent_open_ok);

	if (dirid != BTRFS_FIRST_FREE_OBJECTID) {
		BEESTRACE("dirid " << dirid << " root " << rootid << " INO_PATH");
		BtrfsIoctlInoPathArgs ino(dirid);
		if (!ino.do_ioctl_nothrow(parent_fd)) {
			BEESLOGDEBUG("dirid " << dirid << " inode path lookup failed in parent_fd " << name_fd(parent_fd) << ": " << strerror(errno));
			BEESCOUNT(root_parent_path_fail);
			return Fd();
		}
		if (ino.m_paths.empty()) {
			BEESLOGDEBUG("dirid " << dirid << " inode has no paths in parent_fd " << name_fd(parent_fd));
			BEESCOUNT(root_parent_path_empty);
			return Fd();
		}
		// Theoretically there is only one, so don't bother looping.
		BEESTRACE("dirid " << dirid << " path " << ino.m_paths.at(0));
		parent_fd = bees_openat(parent_fd, ino.m_paths.at(0).c_str(), FLAGS_OPEN_DIR);
		if (!parent_fd) {
			BEESLOGDEBUG("no parent_fd from dirid " << dirid << " in parent_rootid " << parent_rootid);
			BEESCOUNT(root_parent_path_open_fail);
			return Fd();
		}
	}
	BEESTRACE("openat(" << name_fd(parent_fd) << ", " << name << ")");
	Fd rv = bees_openat(parent_fd, name.c_str(), FLAGS_OPEN_DIR);
	if (!rv) {
		BEESLOGDEBUG("open failed for name " << name << " in parent_fd " << name_fd(parent_fd) << ": " << strerror(errno));
		BEESCOUNT(root_open_fail);
		return rv;
	}
	BEESCOUNT(root_found);

	// Verify correct root ID
	// Throw exceptions here because these are very rare events
	// and unlike the file open case, we don't have alternatives to try
	const auto new_root_id = btrfs_get_root_id(rv);
	THROW_CHECK2(runtime_error, new_root_id, rootid, new_root_id == rootid);
	Stat st(rv);
	THROW_CHECK1(runtime_error, st.st_ino, st.st_ino == BTRFS_FIRST_FREE_OBJECTID);
	// BEESLOGDEBUG("open_root_nocache " << rootid << ": " << name_fd(rv));

	BEESCOUNT(root_ok);
	return rv;
}

Fd
BeesRoots::open_root(uint64_t rootid)
{
	// Ignore some of the crap that comes out of LOGICAL_INO
	if (rootid == BTRFS_ROOT_TREE_OBJECTID) {
		return Fd();
	}

	return m_ctx->fd_cache()->open_root(rootid);
}

bool
BeesRoots::is_root_ro(uint64_t root)
{
	// If we are not working around btrfs send, all roots are rw to us
	if (!m_workaround_btrfs_send) {
		return false;
	}

	// If it's not a subvol tree, it's rw
	if (!is_subvol_tree(root)) {
		return false;
	}

	BEESTRACE("checking subvol flags on root " << root);

	BtrfsRootFetcher root_fetcher(m_ctx->root_fd());
	const auto item = root_fetcher.root(root);
	// If we can't access the subvol's root item...guess it's ro?
	if (!item || item.root_flags() & BTRFS_ROOT_SUBVOL_RDONLY) {
		return true;
	}
	return false;
}

uint64_t
BeesRoots::next_root(uint64_t root)
{
	BEESNOTE("Next root from " << root);
	BEESTRACE("Next root from " << root);

	// BTRFS_FS_TREE_OBJECTID has no backref keys so we can't find it that way
	if (root < BTRFS_FS_TREE_OBJECTID) {
		// BEESLOGDEBUG("First root is BTRFS_FS_TREE_OBJECTID = " << BTRFS_FS_TREE_OBJECTID);
		return BTRFS_FS_TREE_OBJECTID;
	}

	BtrfsIoctlSearchKey sk;
	sk.tree_id = BTRFS_ROOT_TREE_OBJECTID;
	sk.min_type = sk.max_type = BTRFS_ROOT_BACKREF_KEY;
	sk.min_objectid = root + 1;

	while (true) {
		sk.do_ioctl(m_ctx->root_fd());

		if (sk.m_result.empty()) {
			return 0;
		}

		for (auto i : sk.m_result) {
			sk.next_min(i, BTRFS_ROOT_BACKREF_KEY);
			if (i.type == BTRFS_ROOT_BACKREF_KEY) {
				// BEESLOGDEBUG("Found root " << i.objectid << " parent " << i.offset << " transid " << i.transid);
				return i.objectid;
			}
		}
	}
}

Fd
BeesRoots::open_root_ino_nocache(uint64_t root, uint64_t ino)
{
	BEESTRACE("opening root " << root << " ino " << ino);

	// Check the tmpfiles map first
	{
		unique_lock<mutex> lock(m_tmpfiles_mutex);
		auto found = m_tmpfiles.find(BeesFileId(root, ino));
		if (found != m_tmpfiles.end()) {
			BEESCOUNT(open_tmpfile);
			return found->second;
		}
	}

	Fd root_fd = open_root(root);
	if (!root_fd) {
		BEESCOUNT(open_no_root);
		return root_fd;
	}

	BEESTOOLONG("open_root_ino(root " << root << ", ino " << ino << ")");

	BEESTRACE("looking up ino " << ino);
	BtrfsIoctlInoPathArgs ipa(ino);
	if (!ipa.do_ioctl_nothrow(root_fd)) {
		if (errno == ENOENT) {
			BEESCOUNT(open_lookup_enoent);
		} else {
			BEESLOGINFO("Lookup root " << root << " ino " << ino << " failed: " << strerror(errno));
			BEESCOUNT(open_lookup_error);
		}
		return Fd();
	}

	BEESTRACE("searching paths for root " << root << " ino " << ino);
	Fd rv;
	if (ipa.m_paths.empty()) {
		// BEESLOGDEBUG("No paths for root " << root << " ino " << ino);
		BEESCOUNT(open_lookup_empty);
	}
	BEESCOUNT(open_lookup_ok);

	for (auto file_path : ipa.m_paths) {
		BEESTRACE("Looking up root " << root << " ino " << ino << " in dir " << name_fd(root_fd) << " path " << file_path);
		BEESCOUNT(open_file);
		// Just open file RO.  root can do the dedupe ioctl without
		// opening in write mode, and if we do open in write mode,
		// we can't exec the file while we have it open.
		const char *fp_cstr = file_path.c_str();
		rv = bees_openat(root_fd, fp_cstr, FLAGS_OPEN_FILE);
		if (!rv) {
			// errno == ENOENT is the most common error case.
			// No need to report it.
			if (errno == ENOENT) {
				BEESCOUNT(open_fail_enoent);
			} else {
				BEESLOGWARN("Could not open path '" << file_path << "' at root " << root << " " << name_fd(root_fd) << ": " << strerror(errno));
				BEESCOUNT(open_fail_error);
			}
			continue;
		}

		// Correct inode?
		Stat file_stat(rv);
		if (file_stat.st_ino != ino) {
			BEESLOGWARN("Opening " << name_fd(root_fd) << "/" << file_path << " found wrong inode " << file_stat.st_ino << " instead of " << ino);
			rv = Fd();
			BEESCOUNT(open_wrong_ino);
			break;
		}

		// Correct root?
		auto file_root = btrfs_get_root_id(rv);
		if (file_root != root) {
			BEESLOGWARN("Opening " << name_fd(root_fd) << "/" << file_path << " found wrong root " << file_root << " instead of " << root);
			rv = Fd();
			BEESCOUNT(open_wrong_root);
			break;
		}

		// Same filesystem?
		Stat root_stat(root_fd);
		if (root_stat.st_dev != file_stat.st_dev) {
			BEESLOGWARN("Opening root " << name_fd(root_fd) << " path " << file_path << " found path st_dev " << file_stat.st_dev << " but root st_dev is " << root_stat.st_dev);
			rv = Fd();
			BEESCOUNT(open_wrong_dev);
			break;
		}

		// The kernel rejects dedupe requests with
		// src and dst that have different datasum flags
		// (datasum is a flag in the inode).
		//
		// We can detect the common case where a file is
		// marked with nodatacow (which implies nodatasum).
		// nodatacow files are arguably out of scope for dedupe,
		// since dedupe would just make them datacow again.
		// To handle these we pretend we couldn't open them.
		//
		// A less common case is nodatasum + datacow files.
		// Those are availble for dedupe but we have to solve
		// some other problems before we can dedupe them.  They
		// require a separate hash table namespace from datasum
		// + datacow files, and we have to create nodatasum
		// temporary files when we rewrite extents.
		//
		// FIXME:  the datasum flag is scooped up by
		// TREE_SEARCH_V2 during crawls.  We throw the inode
		// items away when we should be examining them for the
		// nodatasum flag.

		int attr = ioctl_iflags_get(rv);
		if (attr & FS_NOCOW_FL) {
			BEESLOGINFO("Opening " << name_fd(rv) << " found FS_NOCOW_FL flag in " << to_hex(attr));
			rv = Fd();
			BEESCOUNT(open_wrong_flags);
			break;
		}

		BEESCOUNT(open_hit);
		return rv;
	}

	// All of the paths we tried were wrong.
	BEESCOUNT(open_no_path);
	return Fd();
}

Fd
BeesRoots::open_root_ino(uint64_t root, uint64_t ino)
{
	return m_ctx->fd_cache()->open_root_ino(root, ino);
}

void
BeesRoots::insert_tmpfile(Fd fd)
{
	BeesFileId fid(fd);
	unique_lock<mutex> lock(m_tmpfiles_mutex);
	auto rv = m_tmpfiles.insert(make_pair(fid, fd));
	THROW_CHECK1(runtime_error, fd, rv.second);
}

void
BeesRoots::erase_tmpfile(Fd fd)
{
	BeesFileId fid(fd);
	unique_lock<mutex> lock(m_tmpfiles_mutex);
	auto found = m_tmpfiles.find(fid);
	THROW_CHECK1(runtime_error, fd, found != m_tmpfiles.end());
	m_tmpfiles.erase(found);
}

BeesCrawl::BeesCrawl(shared_ptr<BeesContext> ctx, BeesCrawlState initial_state) :
	m_ctx(ctx),
	m_state(initial_state),
	m_btof(ctx->root_fd())
{
	if (is_subvol_tree(initial_state.m_root)) {
		m_btof.tree(initial_state.m_root);
		m_btof.scale_size(1);
		m_btof.type(BTRFS_EXTENT_DATA_KEY);
	} else {
		m_btof.tree(BTRFS_EXTENT_TREE_OBJECTID);
		m_btof.type(BTRFS_EXTENT_ITEM_KEY);
	}
}

bool
BeesCrawl::restart_crawl_unlocked()
{
	const auto roots = m_ctx->roots();
	const auto next_transid = roots->transid_max();

	auto crawl_state = get_state_end();

	// If we are already at transid_max then we are still finished
	m_finished = crawl_state.m_max_transid >= next_transid;

	if (m_finished) {
		m_deferred = true;
		BEESLOGINFO("Crawl finished " << crawl_state);
	} else {
		// Log performance stats from the old crawl
		const auto current_time = time(NULL);

		// Start new crawl
		crawl_state.m_min_transid = crawl_state.m_max_transid;
		crawl_state.m_max_transid = next_transid;
		crawl_state.m_objectid = 0;
		crawl_state.m_offset = 0;
		crawl_state.m_started = current_time;
		BEESCOUNT(crawl_restart);
		set_state(crawl_state);
		m_deferred = false;
		BEESLOGINFO("Crawl started " << crawl_state);
	}

	return !m_finished;
}

bool
BeesCrawl::restart_crawl()
{
	unique_lock<mutex> lock(m_mutex);
	return restart_crawl_unlocked();
}

bool
BeesCrawl::fetch_extents()
{
	BEESTRACE("fetch_extents " << get_state_end());
	BEESNOTE("fetch_extents " << get_state_end());
	// insert_root will undefer us.  Until then, nothing.
	if (m_deferred) {
		return false;
	}

	const auto old_state = get_state_end();

	// We can't scan an empty transid interval.
	if (m_finished || old_state.m_max_transid <= old_state.m_min_transid) {
		// fetch_extents is called from pop_front or peek_front, both of which hold the lock
		return restart_crawl_unlocked();
	}

	// Check for btrfs send workaround: don't scan RO roots at all, pretend
	// they are just empty.  We can't free any space there, and we
	// don't have the necessary analysis logic to be able to use
	// them as dedupe src extents (yet).
	BEESTRACE("is_root_ro(" << old_state.m_root << ")");
	if (m_ctx->is_root_ro(old_state.m_root)) {
		BEESLOGDEBUG("WORKAROUND: skipping scan of RO root " << old_state.m_root);
		BEESCOUNT(root_workaround_btrfs_send);
		// We would call restart_crawl() here, but we want to do a few things differently.
		// We immediately defer further crawling on this subvol.
		// We track max_transid if the subvol scan has never started.
		// We postpone the started timestamp since we haven't started.
		auto crawl_state = old_state;
		if (crawl_state.m_objectid == 0) {
			// This will keep the max_transid up to date so if the root
			// is ever switched back to read-write, it won't trigger big
			// expensive in-kernel searches for ancient transids.
			// If the root is made RO while crawling is in progress, we will
			// have the big expensive in-kernel searches (same as if we have
			// been not running for a long time).
			// Don't allow transid_max to ever move backwards.
			const auto roots = m_ctx->roots();
			const auto next_transid = roots->transid_max();
			const auto current_time = time(NULL);
			crawl_state.m_max_transid = max(next_transid, crawl_state.m_max_transid);
			// Move the start time forward too, since we have not started crawling yet.
			crawl_state.m_started = current_time;
			set_state(crawl_state);
		}
		// Mark this root deferred so we won't see it until the next transid cycle
		m_deferred = true;
		return false;
	}

	BEESNOTE("crawling " << old_state);

	// Find an extent data item in this subvol in the transid range
	BEESTRACE("looking for new objects " << old_state);

	// Don't set max_transid to m_max_transid here.	 See scan_one_ref.
	m_btof.transid(old_state.m_min_transid);

	// Deprecated: don't call this on the extent tree any more
	assert(m_btof.tree() != BTRFS_EXTENT_TREE_OBJECTID);

	if (catch_all([&]() {
		m_next_extent_data = m_btof.lower_bound(old_state.m_objectid);
	})) {
		// Whoops that didn't work.  Stop scanning this subvol, move on to the next.
		m_deferred = true;
		return false;
	}
	if (!m_next_extent_data) {
		// Ran out of data in this subvol and transid.
		// Try to restart immediately if more transids are available.
		return restart_crawl_unlocked();
	}
	auto new_state = old_state;
	new_state.m_objectid = max(m_next_extent_data.objectid() + m_btof.scale_size(), m_next_extent_data.objectid());
	new_state.m_offset = 0;
	set_state(new_state);
	return true;
}

void
BeesCrawl::fetch_extents_harder()
{
	BEESNOTE("fetch_extents_harder " << get_state_end());
	BEESTRACE("fetch_extents_harder " << get_state_end());
	while (!m_next_extent_data) {
		const bool progress_made = fetch_extents();
		if (!progress_made) {
			return;
		}
	}
}

BeesFileRange
BeesCrawl::bti_to_bfr(const BtrfsTreeItem &bti) const
{
	if (!bti) {
		return BeesFileRange();
	}
	// Deprecated: don't call peek_front() or pop_front() with extent tree any more
	assert(bti.type() != BTRFS_EXTENT_ITEM_KEY);
	return BeesFileRange(
		BeesFileId(get_state_end().m_root, bti.objectid()),
		bti.offset(),
		bti.offset() + bti.file_extent_logical_bytes()
	);
}

BeesFileRange
BeesCrawl::peek_front()
{
	unique_lock<mutex> lock(m_mutex);
	fetch_extents_harder();
	return bti_to_bfr(m_next_extent_data);
}

BeesFileRange
BeesCrawl::pop_front()
{
	unique_lock<mutex> lock(m_mutex);
	fetch_extents_harder();
	BtrfsTreeItem rv;
	swap(rv, m_next_extent_data);
	return bti_to_bfr(rv);
}

BeesCrawlState
BeesCrawl::get_state_begin()
{
	return m_state.begin();
}

BeesCrawlState
BeesCrawl::get_state_end() const
{
	return m_state.end();
}

ProgressTracker<BeesCrawlState>::ProgressHolder
BeesCrawl::hold_state(const BeesCrawlState &bcs)
{
	return m_state.hold(bcs);
}

void
BeesCrawl::set_state(const BeesCrawlState &bcs)
{
	m_state.hold(bcs);
	m_ctx->roots()->crawl_state_set_dirty();
}

void
BeesCrawl::deferred(bool def_setting)
{
	unique_lock<mutex> lock(m_state_mutex);
	m_deferred = def_setting;
}

bool
BeesCrawl::deferred() const
{
	return m_deferred;
}

bool
BeesCrawl::finished() const
{
	return m_finished;
}
