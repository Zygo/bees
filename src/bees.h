#ifndef BEES_H
#define BEES_H

#include "crucible/btrfs-tree.h"
#include "crucible/cache.h"
#include "crucible/chatter.h"
#include "crucible/error.h"
#include "crucible/extentwalker.h"
#include "crucible/fd.h"
#include "crucible/fs.h"
#include "crucible/lockset.h"
#include "crucible/multilock.h"
#include "crucible/pool.h"
#include "crucible/progress.h"
#include "crucible/time.h"
#include "crucible/task.h"

#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <random>
#include <thread>

#include <endian.h>
#include <syslog.h>

using namespace crucible;
using namespace std;

// Block size for clone alignment (FIXME: should read this from /sys/fs/btrfs/<FS-UUID>/clone_alignment)
const off_t BLOCK_SIZE_CLONE = 4096;

// Block size for dedupe checksums (arbitrary, but must be a multiple of clone alignment)
const off_t BLOCK_SIZE_SUMS = 4096;

// Block size for memory allocations and file mappings  (FIXME: should be CPU page size)
const off_t BLOCK_SIZE_MMAP = 4096;

// Maximum length parameter to extent-same ioctl (FIXME: hardcoded in kernel)
const off_t BLOCK_SIZE_MAX_EXTENT_SAME = 4096 * 4096;

// Maximum length of a compressed extent in bytes
const off_t BLOCK_SIZE_MAX_COMPRESSED_EXTENT = 128 * 1024;

// Maximum length of any extent in bytes
// except we've seen 1.03G extents...
// ...FIEMAP is slow and full of lies
const off_t BLOCK_SIZE_MAX_EXTENT = 128 * 1024 * 1024;

// Masks, so we don't have to write "(BLOCK_SIZE_CLONE - 1)" everywhere
const off_t BLOCK_MASK_CLONE = BLOCK_SIZE_CLONE - 1;
const off_t BLOCK_MASK_SUMS = BLOCK_SIZE_SUMS - 1;

// Maximum temporary file size (maximum extent size for temporary copy)
const off_t BLOCK_SIZE_MAX_TEMP_FILE = 1024 * 1024 * 1024;

// Bucket size for hash table (size of one hash bucket)
const off_t BLOCK_SIZE_HASHTAB_BUCKET = BLOCK_SIZE_MMAP;

// Extent size for hash table (since the nocow file attribute does not seem to be working today)
const off_t BLOCK_SIZE_HASHTAB_EXTENT = BLOCK_SIZE_MAX_COMPRESSED_EXTENT;

// Bytes per second we want to flush from hash table
// Optimistic sustained write rate for SD cards
const double BEES_FLUSH_RATE = 128 * 1024;

// Interval between writing crawl state to disk
const int BEES_WRITEBACK_INTERVAL = 900;

// Statistics reports while scanning
const int BEES_STATS_INTERVAL = 3600;

// Progress shows instantaneous rates and thread status
const int BEES_PROGRESS_INTERVAL = BEES_STATS_INTERVAL;

// Status is output every freakin second.  Use a ramdisk.
const int BEES_STATUS_INTERVAL = 1;

// Number of file FDs to cache when not in active use
const size_t BEES_FILE_FD_CACHE_SIZE = 32768;

// Number of root FDs to cache when not in active use
const size_t BEES_ROOT_FD_CACHE_SIZE = 4096;

// Number of FDs to open (rlimit)
const size_t BEES_OPEN_FILE_LIMIT = BEES_FILE_FD_CACHE_SIZE + BEES_ROOT_FD_CACHE_SIZE + 100;

// Worker thread factor (multiplied by detected number of CPU cores)
const double BEES_DEFAULT_THREAD_FACTOR = 1.0;

// Log warnings when an operation takes too long
const double BEES_TOO_LONG = 5.0;

// Avoid any extent where LOGICAL_INO takes this much kernel CPU time
const double BEES_TOXIC_SYS_DURATION = 5.0;

// Maximum number of refs to a single extent before we have other problems
// If we have more than 10K refs to an extent, adding another will save 0.01% space
const size_t BEES_MAX_EXTENT_REF_COUNT = 9999; // (16 * 1024 * 1024 / 24);

// How long between hash table histograms
const double BEES_HASH_TABLE_ANALYZE_INTERVAL = BEES_STATS_INTERVAL;

// Wait at least this long for a new transid
const double BEES_TRANSID_POLL_INTERVAL = 30.0;

// Workaround for silly dedupe / ineffective readahead behavior
const size_t BEES_READAHEAD_SIZE = 1024 * 1024;

// Flags
const int FLAGS_OPEN_COMMON   = O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC | O_NOATIME | O_LARGEFILE | O_NOCTTY;
const int FLAGS_OPEN_DIR      = FLAGS_OPEN_COMMON | O_RDONLY | O_DIRECTORY;
const int FLAGS_OPEN_FILE     = FLAGS_OPEN_COMMON | O_RDONLY;
const int FLAGS_OPEN_FILE_RW  = FLAGS_OPEN_COMMON | O_RDWR;
const int FLAGS_OPEN_TMPFILE  = FLAGS_OPEN_FILE_RW | O_TMPFILE | O_TRUNC | O_EXCL;
const int FLAGS_CREATE_FILE   = FLAGS_OPEN_COMMON | O_WRONLY | O_CREAT | O_EXCL;

// Fanotify allows O_APPEND, O_DSYNC, O_NOATIME, O_NONBLOCK, O_CLOEXEC, O_LARGEFILE
const int FLAGS_OPEN_FANOTIFY = O_RDWR | O_NOATIME | O_CLOEXEC | O_LARGEFILE;

// macros ----------------------------------------

#define BEESLOG(lv,x)   do { if (lv < bees_log_level) { Chatter __chatter(lv, BeesNote::get_name()); __chatter << x; } } while (0)
#define BEESLOGTRACE(x) do { BEESLOG(LOG_DEBUG, x); BeesTracer::trace_now(); } while (0)

#define BEESTRACE(x)   BeesTracer  SRSLY_WTF_C(beesTracer_,  __LINE__) ([&]()                 { BEESLOG(LOG_ERR, x << " at " << __FILE__ << ":" << __LINE__);   })
#define BEESTOOLONG(x) BeesTooLong SRSLY_WTF_C(beesTooLong_, __LINE__) ([&](ostream &_btl_os) { _btl_os << x; })
#define BEESNOTE(x)    BeesNote    SRSLY_WTF_C(beesNote_,    __LINE__) ([&](ostream &_btl_os) { _btl_os << x; })

#define BEESLOGERR(x)    BEESLOG(LOG_ERR, x)
#define BEESLOGWARN(x)   BEESLOG(LOG_WARNING, x)
#define BEESLOGNOTICE(x) BEESLOG(LOG_NOTICE, x)
#define BEESLOGINFO(x)   BEESLOG(LOG_INFO, x)
#define BEESLOGDEBUG(x)  BEESLOG(LOG_DEBUG, x)

#define BEESCOUNT(stat) do { \
	BeesStats::s_global.add_count(#stat); \
} while (0)

#define BEESCOUNTADD(stat, amount) do { \
	BeesStats::s_global.add_count(#stat, (amount)); \
} while (0)

// ----------------------------------------

template <class T> class BeesStatTmpl;
template <class T> ostream& operator<<(ostream &os, const BeesStatTmpl<T> &bs);

template <class T>
class BeesStatTmpl {
	map<string, T>	m_stats_map;
	mutable mutex	m_mutex;

	T& at(string idx);
public:
	BeesStatTmpl() = default;
	BeesStatTmpl(const BeesStatTmpl &that);
	BeesStatTmpl &operator=(const BeesStatTmpl &that);
	void add_count(string idx, size_t amount = 1);
	T at(string idx) const;

friend ostream& operator<< <>(ostream &os, const BeesStatTmpl<T> &bs);
friend struct BeesStats;
};

using BeesRates = BeesStatTmpl<double>;

struct BeesStats : public BeesStatTmpl<uint64_t> {
	static BeesStats s_global;

	BeesStats operator-(const BeesStats &that) const;
	BeesRates operator/(double d) const;
	explicit operator bool() const;
};

class BeesContext;
class BeesBlockData;

class BeesTracer {
	function<void()> m_func;
	BeesTracer *m_next_tracer = 0;

	thread_local static BeesTracer *tl_next_tracer;
	thread_local static bool tl_silent;
	thread_local static bool tl_first;
public:
	BeesTracer(function<void()> f, bool silent = false);
	~BeesTracer();
	static void trace_now();
	static bool get_silent();
	static void set_silent();
};

class BeesNote {
	function<void(ostream &)>	m_func;
	BeesNote			*m_prev;
	Timer				m_timer;
	string				m_name;

	static mutex			s_mutex;
	static map<pid_t, BeesNote*>	s_status;

	thread_local static BeesNote	*tl_next;
	thread_local static string	tl_name;

public:
	BeesNote(function<void(ostream &)> f);
	~BeesNote();

	using ThreadStatusMap = map<pid_t, string>;

	static ThreadStatusMap get_status();

	static void set_name(const string &name);
	static string get_name();
};

// C++ threads dumbed down even further
class BeesThread {
	string			m_name;
	Timer			m_timer;
	shared_ptr<thread>	m_thread_ptr;

public:
	~BeesThread();
	BeesThread(string name);
	BeesThread(string name, function<void()> args);
	void exec(function<void()> args);
	void join();
	void set_name(const string &name);
};

class BeesFileId {
	uint64_t	m_root;
	uint64_t	m_ino;

public:
	uint64_t root() const { return m_root; }
	uint64_t ino() const { return m_ino; }
	bool operator<(const BeesFileId &that) const;
	bool operator!=(const BeesFileId &that) const;
	bool operator==(const BeesFileId &that) const;
	operator bool() const;
	BeesFileId(const BtrfsInodeOffsetRoot &bior);
	BeesFileId(int fd);
	BeesFileId(uint64_t root, uint64_t ino);
	BeesFileId();
};

ostream& operator<<(ostream &os, const BeesFileId &bfi);

class BeesFileRange {
protected:
	Fd			m_fd;
	mutable BeesFileId	m_fid;
	off_t			m_begin = 0, m_end = 0;
	mutable off_t		m_file_size = -1;

public:

	BeesFileRange() = default;
	BeesFileRange(Fd fd, off_t begin, off_t end);
	BeesFileRange(const BeesFileId &fid, off_t begin, off_t end);
	BeesFileRange(const BeesBlockData &bbd);

	operator BeesBlockData() const;

	bool operator<(const BeesFileRange &that) const;
	bool operator==(const BeesFileRange &that) const;
	bool operator!=(const BeesFileRange &that) const;

	bool empty() const;
	bool is_same_file(const BeesFileRange &that) const;
	bool overlaps(const BeesFileRange &that) const;

	off_t begin() const { return m_begin; }
	off_t end() const { return m_end; }
	off_t size() const;

	/// @{ Lazy accessors
	off_t file_size() const;
	BeesFileId fid() const;
	/// @}

	/// Get the fd if there is one
	Fd fd() const;

	/// Get the fd, opening it if necessary
	Fd fd(const shared_ptr<BeesContext> &ctx);

	/// Copy the BeesFileId but not the Fd
	BeesFileRange copy_closed() const;

	/// Is it defined?
	operator bool() const { return !!m_fd || m_fid; }

	/// @{ Make range larger
	off_t grow_end(off_t delta);
	off_t grow_begin(off_t delta);
	/// @}

	/// @{ Make range smaller
	off_t shrink_end(off_t delta);
	off_t shrink_begin(off_t delta);
	/// @}

friend ostream & operator<<(ostream &os, const BeesFileRange &bfr);
};

class BeesAddress {
public:
	using Type = uint64_t;
private:
	Type	m_addr = ZERO;
	bool magic_check(uint64_t flags);
public:

	// Blocks with no physical address (not yet allocated, hole, or "other").
	// PREALLOC blocks have a physical address so they're not magic enough to be handled here.
	enum MagicValue {
		ZERO,		// BeesAddress uninitialized
		DELALLOC,	// delayed allocation
		HOLE,		// no extent present, no space allocated
		UNUSABLE,	// inline extent or unrecognized FIEMAP flags
		LAST,		// all further values are non-magic
	};

	BeesAddress(Type addr = ZERO) : m_addr(addr) {}
	BeesAddress(MagicValue addr) : m_addr(addr) {}
	BeesAddress& operator=(const BeesAddress &that) = default;
	BeesAddress(const BeesAddress &that) = default;
	operator Type() const { return m_addr; }
	bool operator==(const BeesAddress &that) const;
	bool operator==(const MagicValue that) const { return *this == BeesAddress(that); }
	bool operator!=(const BeesAddress &that) const { return !(*this == that); }
	bool operator!=(const MagicValue that) const { return *this != BeesAddress(that); }
	bool operator<(const BeesAddress &that) const;

	static const Type c_offset_min = 1;
	static const Type c_offset_max = BLOCK_SIZE_MAX_COMPRESSED_EXTENT / BLOCK_SIZE_CLONE;

	// if this isn't 0x3f we will have problems
	static const Type c_offset_mask = (c_offset_max - 1) | (c_offset_max);

	static const Type c_compressed_mask = 1 << 11;
	static const Type c_eof_mask = 1 << 10;
	static const Type c_toxic_mask = 1 << 9;

	static const Type c_all_mask = c_compressed_mask | c_eof_mask | c_offset_mask | c_toxic_mask;

	bool is_compressed() const { return m_addr >= MagicValue::LAST && (m_addr & c_compressed_mask); }
	bool has_compressed_offset() const { return m_addr >= MagicValue::LAST && (m_addr & c_compressed_mask) && (m_addr & c_offset_mask); }
	bool is_toxic() const { return m_addr >= MagicValue::LAST && (m_addr & c_toxic_mask); }
	bool is_unaligned_eof() const { return m_addr >= MagicValue::LAST && (m_addr & c_eof_mask); }
	bool is_magic() const { return m_addr < MagicValue::LAST; }

	Type get_compressed_offset() const;
	Type get_physical_or_zero() const;

	void set_toxic();

	BeesAddress(int fd, off_t offset);
	BeesAddress(int fd, off_t offset, shared_ptr<BeesContext> ctx);
	BeesAddress(const Extent &e, off_t offset);
};

ostream & operator<<(ostream &os, const BeesAddress &ba);

class BeesStringFile {
	Fd	m_dir_fd;
	string	m_name;
	size_t	m_limit;

public:
	BeesStringFile(Fd dir_fd, string name, size_t limit = 16 * 1024 * 1024);
	string read();
	void write(string contents);
	void name(const string &new_name);
	string name() const;
};

class BeesHashTable {
	shared_ptr<BeesContext>	m_ctx;
public:
	using HashType = uint64_t;
	using AddrType = uint64_t;

	struct Cell {
		HashType	e_hash;
		AddrType	e_addr;
		Cell(const Cell &) = default;
		Cell &operator=(const Cell &) = default;
		Cell(HashType hash, AddrType addr) : e_hash(hash), e_addr(addr) { }
		bool operator==(const Cell &e) const { return tie(e_hash, e_addr) == tie(e.e_hash, e.e_addr); }
		bool operator!=(const Cell &e) const { return tie(e_hash, e_addr) != tie(e.e_hash, e.e_addr); }
		bool operator<(const Cell &e) const { return tie(e_hash, e_addr) < tie(e.e_hash, e.e_addr); }
	} __attribute__((packed));

private:
	static const uint64_t c_cells_per_bucket = BLOCK_SIZE_HASHTAB_BUCKET / sizeof(Cell);
	static const uint64_t c_buckets_per_extent = BLOCK_SIZE_HASHTAB_EXTENT / BLOCK_SIZE_HASHTAB_BUCKET;

public:
	union Bucket {
		Cell	p_cells[c_cells_per_bucket];
		uint8_t	p_byte[BLOCK_SIZE_HASHTAB_BUCKET];
	} __attribute__((packed));

	union Extent {
		Bucket	p_buckets[BLOCK_SIZE_HASHTAB_EXTENT / BLOCK_SIZE_HASHTAB_BUCKET];
		uint8_t	p_byte[BLOCK_SIZE_HASHTAB_EXTENT];
	} __attribute__((packed));

	BeesHashTable(shared_ptr<BeesContext> ctx, string filename, off_t size = BLOCK_SIZE_HASHTAB_EXTENT);
	~BeesHashTable();

	void stop_request();
	void stop_wait();

	vector<Cell>	find_cell(HashType hash);
	bool		push_random_hash_addr(HashType hash, AddrType addr);
	void		erase_hash_addr(HashType hash, AddrType addr);
	bool		push_front_hash_addr(HashType hash, AddrType addr);
	bool            flush_dirty_extent(uint64_t extent_index);

private:
	string		m_filename;
	Fd		m_fd;
	uint64_t	m_size;
	union {
		void	*m_void_ptr;	// Save some casting
		uint8_t	*m_byte_ptr;	// for pointer arithmetic
		Cell	*m_cell_ptr;	// pointer to one table cell (entry)
		Bucket	*m_bucket_ptr;	// all cells in one LRU unit
		Extent	*m_extent_ptr;	// all buckets in one I/O unit
	};
	union {
		void	*m_void_ptr_end;
		uint8_t	*m_byte_ptr_end;
		Cell	*m_cell_ptr_end;
		Bucket	*m_bucket_ptr_end;
		Extent	*m_extent_ptr_end;
	};
	uint64_t		m_buckets;
	uint64_t		m_extents;
	uint64_t		m_cells;
	BeesThread  		m_writeback_thread;
	BeesThread	        m_prefetch_thread;
	RateLimiter		m_flush_rate_limit;
	BeesStringFile		m_stats_file;

	// Prefetch readahead hint
	bool			m_prefetch_running = false;

	// Mutex/condvar for the writeback thread
	mutex			m_dirty_mutex;
	condition_variable	m_dirty_condvar;
	bool			m_dirty = false;

	// Mutex/condvar to stop
	mutex			m_stop_mutex;
	condition_variable	m_stop_condvar;
	bool			m_stop_requested = false;

	// Per-extent structures
	struct ExtentMetaData {
		shared_ptr<mutex> m_mutex_ptr;		// Access serializer
		bool	m_dirty = false;	// Needs to be written back to disk
		bool	m_missing = true;	// Needs to be read from disk
		ExtentMetaData();
	};
	vector<ExtentMetaData>	m_extent_metadata;

	void open_file();
	void writeback_loop();
	void prefetch_loop();
	void try_mmap_flags(int flags);
	pair<Cell *, Cell *> get_cell_range(HashType hash);
	pair<uint8_t *, uint8_t *> get_extent_range(HashType hash);
	void fetch_missing_extent_by_hash(HashType hash);
	void fetch_missing_extent_by_index(uint64_t extent_index);
	void set_extent_dirty_locked(uint64_t extent_index);
	size_t flush_dirty_extents(bool slowly);

	size_t			hash_to_extent_index(HashType ht);
	unique_lock<mutex>	lock_extent_by_hash(HashType ht);
	unique_lock<mutex>	lock_extent_by_index(uint64_t extent_index);

	BeesHashTable(const BeesHashTable &) = delete;
	BeesHashTable &operator=(const BeesHashTable &) = delete;

	static thread_local uniform_int_distribution<size_t> tl_distribution;
};

ostream &operator<<(ostream &os, const BeesHashTable::Cell &bhte);

struct BeesCrawlState {
	uint64_t	m_root;
	uint64_t	m_objectid;
	uint64_t	m_offset;
	uint64_t	m_min_transid;
	uint64_t	m_max_transid;
	time_t		m_started;
	BeesCrawlState();
	bool operator<(const BeesCrawlState &that) const;
};

class BeesCrawl {
	shared_ptr<BeesContext>			m_ctx;

	mutex					m_mutex;
	BtrfsTreeItem				m_next_extent_data;
	bool					m_deferred = false;
	bool					m_finished = false;

	mutex					m_state_mutex;
	ProgressTracker<BeesCrawlState>		m_state;

	BtrfsTreeObjectFetcher			m_btof;

	bool fetch_extents();
	void fetch_extents_harder();
	bool restart_crawl();
	BeesFileRange bti_to_bfr(const BtrfsTreeItem &bti) const;

public:
	BeesCrawl(shared_ptr<BeesContext> ctx, BeesCrawlState initial_state);
	BeesFileRange peek_front();
	BeesFileRange pop_front();
	ProgressTracker<BeesCrawlState>::ProgressHolder hold_state(const BeesCrawlState &bcs);
	BeesCrawlState get_state_begin();
	BeesCrawlState get_state_end() const;
	void set_state(const BeesCrawlState &bcs);
	void deferred(bool def_setting);
	bool deferred() const;
	bool finished() const;
};

class BeesScanMode;

class BeesRoots : public enable_shared_from_this<BeesRoots> {
	shared_ptr<BeesContext>			m_ctx;

	BeesStringFile				m_crawl_state_file;
	map<uint64_t, shared_ptr<BeesCrawl>>	m_root_crawl_map;
	mutex					m_mutex;
	uint64_t				m_crawl_dirty = 0;
	uint64_t				m_crawl_clean = 0;
	Timer					m_crawl_timer;
	BeesThread				m_crawl_thread;
	BeesThread				m_writeback_thread;
	RateEstimator				m_transid_re;
	bool					m_workaround_btrfs_send = false;

	shared_ptr<BeesScanMode>		m_scanner;

	mutex					m_tmpfiles_mutex;
	map<BeesFileId, Fd>			m_tmpfiles;

	mutex					m_stop_mutex;
	condition_variable			m_stop_condvar;
	bool					m_stop_requested = false;

	void insert_new_crawl();
	Fd open_root_nocache(uint64_t root);
	Fd open_root_ino_nocache(uint64_t root, uint64_t ino);
	uint64_t transid_max_nocache();
	void state_load();
	ostream &state_to_stream(ostream &os);
	void state_save();
	string crawl_state_filename() const;
	void crawl_state_set_dirty();
	void crawl_state_erase(const BeesCrawlState &bcs);
	void crawl_thread();
	void writeback_thread();
	uint64_t next_root(uint64_t root = 0);
	void current_state_set(const BeesCrawlState &bcs);
	RateEstimator& transid_re();
	bool crawl_batch(shared_ptr<BeesCrawl> crawl);
	void clear_caches();

friend class BeesScanModeExtent;
	shared_ptr<BeesCrawl> insert_root(const BeesCrawlState &bcs);

friend class BeesCrawl;
friend class BeesFdCache;
friend class BeesScanMode;

public:
	BeesRoots(shared_ptr<BeesContext> ctx);
	void start();
	void stop_request();
	void stop_wait();

	void insert_tmpfile(Fd fd);
	void erase_tmpfile(Fd fd);

	Fd open_root(uint64_t root);
	Fd open_root_ino(uint64_t root, uint64_t ino);
	Fd open_root_ino(const BeesFileId &bfi) { return open_root_ino(bfi.root(), bfi.ino()); }
	bool is_root_ro(uint64_t root);

	enum ScanMode {
		SCAN_MODE_LOCKSTEP,
		SCAN_MODE_INDEPENDENT,
		SCAN_MODE_SEQUENTIAL,
		SCAN_MODE_RECENT,
		SCAN_MODE_EXTENT,
		SCAN_MODE_COUNT, // must be last
	};

	void set_scan_mode(ScanMode new_mode);
	void set_workaround_btrfs_send(bool do_avoid);

	uint64_t transid_min();
	uint64_t transid_max();
};

struct BeesHash {
	using Type = uint64_t;

	BeesHash() : m_hash(0) { }
	BeesHash(Type that) : m_hash(that) { }
	operator Type() const { return m_hash; }
	BeesHash& operator=(const Type that) { m_hash = that; return *this; }
	BeesHash(const uint8_t *ptr, size_t len);
private:
	Type	m_hash;

};

ostream & operator<<(ostream &os, const BeesHash &bh);

class BeesBlockData {
	using Blob = ByteVector;

	mutable Fd		m_fd;
	off_t			m_offset;
	off_t			m_length;
	mutable BeesAddress	m_addr;
	mutable Blob		m_data;
	mutable BeesHash	m_hash;
	mutable bool		m_hash_done = false;

public:
	// Constructor with the immutable fields
	BeesBlockData(Fd fd, off_t offset, size_t read_length = BLOCK_SIZE_SUMS);
	BeesBlockData();

	// Non-lazy accessors
	Fd fd() const { return m_fd; }

	// Renaming
	off_t begin() const { return m_offset; }
	off_t end() const { return m_offset + m_length; }
	off_t size() const { return m_length; }
	bool empty() const { return !m_length; }

	// Lazy accessors may modify const things
	const Blob &data() const;
	BeesHash hash() const;
	BeesAddress addr() const;
	bool is_data_zero() const;
	bool is_data_equal(const BeesBlockData &that) const;

	// Setters
	BeesBlockData &addr(const BeesAddress &a);

friend ostream &operator<<(ostream &, const BeesBlockData &);
};

class BeesRangePair : public pair<BeesFileRange, BeesFileRange> {
public:
	BeesRangePair(const BeesFileRange &src, const BeesFileRange &dst);
	bool grow(shared_ptr<BeesContext> ctx, bool constrained);
	void shrink_begin(const off_t delta);
	void shrink_end(const off_t delta);
	BeesRangePair copy_closed() const;
	bool operator<(const BeesRangePair &that) const;
friend ostream & operator<<(ostream &os, const BeesRangePair &brp);
};

class BeesTempFile {
	shared_ptr<BeesContext> m_ctx;
	shared_ptr<BeesRoots>   m_roots;
	Fd			m_fd;
	off_t			m_end_offset;

	void realign();
	void resize(off_t new_end_offset);

public:
	~BeesTempFile();
	BeesTempFile(shared_ptr<BeesContext> ctx);
	BeesFileRange make_hole(off_t count);
	BeesFileRange make_copy(const BeesFileRange &src);
	void reset();
};

class BeesFdCache {
	shared_ptr<BeesContext> 		m_ctx;
	LRUCache<Fd, uint64_t>			m_root_cache;
	LRUCache<Fd, uint64_t, uint64_t>	m_file_cache;
	Timer					m_root_cache_timer;
	Timer					m_file_cache_timer;

public:
	BeesFdCache(shared_ptr<BeesContext> ctx);
	Fd open_root(uint64_t root);
	Fd open_root_ino(uint64_t root, uint64_t ino);
	void clear();
};

struct BeesResolveAddrResult {
	BeesResolveAddrResult();
	vector<BtrfsInodeOffsetRoot> m_biors;
	bool m_is_toxic = false;
	bool is_toxic() const { return m_is_toxic; }
};

class BeesContext : public enable_shared_from_this<BeesContext> {
	Fd						m_home_fd;

	shared_ptr<BeesFdCache>				m_fd_cache;
	shared_ptr<BeesHashTable>			m_hash_table;
	shared_ptr<BeesRoots>				m_roots;
	Pool<BeesTempFile>				m_tmpfile_pool;
	Pool<BtrfsIoctlLogicalInoArgs>			m_logical_ino_pool;

	LRUCache<BeesResolveAddrResult, BeesAddress>	m_resolve_cache;

	string						m_root_path;
	Fd						m_root_fd;

	mutable mutex					m_blacklist_mutex;
	set<BeesFileId>					m_blacklist;

	Timer						m_total_timer;

	NamedPtr<Exclusion, uint64_t>			m_extent_locks;
	NamedPtr<Exclusion, uint64_t>			m_inode_locks;

	mutable mutex					m_stop_mutex;
	condition_variable				m_stop_condvar;
	bool						m_stop_requested = false;
	bool						m_stop_status = false;

	shared_ptr<BeesThread>				m_progress_thread;
	shared_ptr<BeesThread>				m_status_thread;

	mutex						m_progress_mtx;
	string						m_progress_str;

	void set_root_fd(Fd fd);

	BeesResolveAddrResult resolve_addr_uncached(BeesAddress addr);

	void scan_one_extent(const BeesFileRange &bfr, const Extent &e);
	void rewrite_file_range(const BeesFileRange &bfr);

public:

	void set_root_path(string path);

	Fd root_fd() const { return m_root_fd; }
	Fd home_fd();
	string root_path() const { return m_root_path; }

	bool scan_forward(const BeesFileRange &bfr);

	shared_ptr<BtrfsIoctlLogicalInoArgs> logical_ino(uint64_t bytenr, bool all_refs);

	bool is_root_ro(uint64_t root);
	BeesRangePair dup_extent(const BeesFileRange &src, const shared_ptr<BeesTempFile> &tmpfile);
	bool dedup(const BeesRangePair &brp);

	void blacklist_insert(const BeesFileId &fid);
	void blacklist_erase(const BeesFileId &fid);
	bool is_blacklisted(const BeesFileId &fid) const;

	shared_ptr<Exclusion> get_inode_mutex(uint64_t inode);

	BeesResolveAddrResult resolve_addr(BeesAddress addr);
	void invalidate_addr(BeesAddress addr);
	void resolve_cache_clear();

	void dump_status();
	void show_progress();
	void set_progress(const string &str);
	string get_progress();

	void start();
	void stop();
	bool stop_requested() const;

	shared_ptr<BeesFdCache> fd_cache();
	shared_ptr<BeesHashTable> hash_table();
	shared_ptr<BeesRoots> roots();
	shared_ptr<BeesTempFile> tmpfile();

	const Timer &total_timer() const { return m_total_timer; }
};

class BeesResolver {
	shared_ptr<BeesContext>			m_ctx;
	BeesAddress				m_addr;
	vector<BtrfsInodeOffsetRoot>		m_biors;
	set<BeesFileRange>			m_ranges;
	size_t					m_bior_count;

	// We found matching data, so we can dedupe
	bool					m_found_data = false;

	// We found matching data, so we *did* dedupe
	bool					m_found_dup = false;

	// We found matching hash, so the hash table is still correct
	bool					m_found_hash = false;

	// We found matching physical address, so the hash table isn't totally wrong
	bool					m_found_addr = false;

	// We found matching physical address, but data did not match
	bool					m_wrong_data = false;

	// The whole thing is a placebo to avoid crippling btrfs performance bugs
	bool					m_is_toxic = false;

	BeesFileRange chase_extent_ref(const BtrfsInodeOffsetRoot &bior, BeesBlockData &needle_bbd);
	BeesBlockData adjust_offset(const BeesFileRange &haystack, const BeesBlockData &needle);
	void find_matches(bool just_one, BeesBlockData &bbd);

	// FIXME: Do we need these?  We probably always have at least one BBD
	BeesFileRange chase_extent_ref(const BtrfsInodeOffsetRoot &bior, BeesHash hash);
	BeesBlockData adjust_offset(const BeesFileRange &haystack, bool inexact, BeesHash needle);
	void find_matches(bool just_one, BeesHash hash);

public:
	BeesResolver(shared_ptr<BeesContext> ctx, BeesAddress addr);
	BeesAddress addr(BeesAddress new_addr);

	// visitor returns true to stop loop, false to continue
	bool for_each_extent_ref(BeesBlockData bbd, function<bool(const BeesFileRange &bfr)> visitor);

	set<BeesFileRange> find_all_matches(BeesBlockData &bbd);
	set<BeesFileRange> find_all_matches(BeesHash hash);

	// TODO:  Replace these with "for_each_extent_ref"
	BeesFileRange find_one_match(BeesBlockData &bbd);
	BeesFileRange find_one_match(BeesHash hash);

	void replace_src(const BeesFileRange &src_bfr);
	BeesRangePair replace_dst(const BeesFileRange &dst_bfr);

	bool found_addr() const { return m_found_addr; }
	bool found_data() const { return m_found_data; }
	bool found_dup() const { return m_found_dup; }
	bool found_hash() const { return m_found_hash; }
	bool is_toxic() const { return m_is_toxic; }
	size_t count() const { return m_bior_count; }
	BeesAddress addr() const { return m_addr; }

	bool operator<(const BeesResolver &that) const;
};

class BeesTooLong : public Timer {
	using func_type = function<void(ostream &)>;
	double m_limit;
	func_type m_func;

public:
	BeesTooLong(const func_type &func = [](ostream &os) { os << __PRETTY_FUNCTION__; }, double limit = BEES_TOO_LONG);
	BeesTooLong(const string &s, double limit = BEES_TOO_LONG);
	BeesTooLong &operator=(const func_type &s);
	~BeesTooLong();
	void check() const;

};

// And now, a giant pile of extern declarations
extern int bees_log_level;
extern const char *BEES_USAGE;
extern const char *BEES_VERSION;
extern thread_local default_random_engine bees_generator;
string pretty(double d);
void bees_readahead(int fd, off_t offset, size_t size);
void bees_readahead_pair(int fd, off_t offset, size_t size, int fd2, off_t offset2, size_t size2);
void bees_unreadahead(int fd, off_t offset, size_t size);
string format_time(time_t t);

#endif
