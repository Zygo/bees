#pragma once

/// @file bees.h
/// Core types, constants, macros, and class declarations for the bees daemon.
/// This is the main internal header; virtually every .cc file includes it.

#include "bees-fwd.h"

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

/// @name Block size and alignment constants
/// @{

/// Required alignment for the FIDEDUPERANGE (FILE_EXTENT_SAME) ioctl source and
/// destination offsets.  Should match the filesystem's clone_alignment, but is
/// currently hardcoded to 4096.
/// FIXME: should read this from /sys/fs/btrfs/<FS-UUID>/clone_alignment
const off_t BLOCK_SIZE_CLONE = 4096;

/// Granularity at which blocks are hashed for deduplication.  Must be a multiple
/// of BLOCK_SIZE_CLONE.
const off_t BLOCK_SIZE_SUMS = 4096;

/// Granularity for memory-mapped I/O and hash-table bucket allocation.
/// FIXME: should be the CPU page size.
const off_t BLOCK_SIZE_MMAP = 4096;

/// Maximum byte count accepted by a single FIDEDUPERANGE ioctl call.
/// This limit is hardcoded in the kernel.
const off_t BLOCK_SIZE_MAX_EXTENT_SAME = 4096 * 4096;

/// Maximum size of a btrfs compressed extent in bytes (128 KiB).
const off_t BLOCK_SIZE_MAX_COMPRESSED_EXTENT = 128 * 1024;

/// Upper bound on any btrfs extent size in bytes.
/// Set to 256 MiB to accommodate a kernel bug introduced in
/// commit 24542bf7ea5e4fdfdb5157ff544c093fa4dcb536
/// ("btrfs: limit fallocate extent reservation to 256MB")
/// where preallocated extents may be 256 MiB instead of the normal 128 MiB limit.
const off_t BLOCK_SIZE_MAX_EXTENT = 256 * 1024 * 1024;

/// @}

/// @name Block alignment masks
/// Complement masks for the corresponding BLOCK_SIZE_* values; avoids repeating
/// @c (BLOCK_SIZE_CLONE - 1) throughout the code.
/// @{
const off_t BLOCK_MASK_CLONE = BLOCK_SIZE_CLONE - 1;
const off_t BLOCK_MASK_SUMS  = BLOCK_SIZE_SUMS  - 1;
/// @}

/// Maximum size of a temporary staging file used for partial-extent copies (1 GiB).
const off_t BLOCK_SIZE_MAX_TEMP_FILE = 1024 * 1024 * 1024;

/// Size of one hash-table bucket (the smallest independently-lockable unit).
const off_t BLOCK_SIZE_HASHTAB_BUCKET = BLOCK_SIZE_MMAP;

/// Size of one hash-table I/O extent (the unit read from / written to disk at once).
/// Equals the max compressed extent size so that the hash table file compresses well.
const off_t BLOCK_SIZE_HASHTAB_EXTENT = BLOCK_SIZE_MAX_COMPRESSED_EXTENT;

/// Target writeback rate for the hash table in bytes per second.
/// Chosen to match a typical SD-card sustained write rate.
const double BEES_FLUSH_RATE = 128 * 1024;

/// Seconds between checkpointing crawl state and hash table to disk.
const int BEES_WRITEBACK_INTERVAL = 900;

/// Seconds between logging cumulative statistics during a scan.
const int BEES_STATS_INTERVAL = 3600;

/// Seconds between logging instantaneous rates and per-thread status.
const int BEES_PROGRESS_INTERVAL = BEES_STATS_INTERVAL;

/// Seconds between writing the status output file (BEESSTATUS).
const int BEES_STATUS_INTERVAL = 1;

/// Maximum number of file file descriptors to keep open in the FD cache.
const size_t BEES_FILE_FD_CACHE_SIZE = 524288;

/// Maximum number of btrfs root (subvolume) file descriptors to keep open.
const size_t BEES_ROOT_FD_CACHE_SIZE = 65536;

/// RLIMIT_NOFILE value bees requests at startup.
const size_t BEES_OPEN_FILE_LIMIT = BEES_FILE_FD_CACHE_SIZE + BEES_ROOT_FD_CACHE_SIZE + 100;

// Worker thread factor (multiplied by detected number of CPU cores)
const double BEES_DEFAULT_THREAD_FACTOR = 1.0;

/// Wall-clock seconds after which a single operation is logged as slow.
const double BEES_TOO_LONG = 5.0;

/// Kernel CPU seconds consumed by a LOGICAL_INO ioctl above which an extent is
/// marked toxic and skipped to avoid performance degradation.
const double BEES_TOXIC_SYS_DURATION = 5.0;

/// Maximum number of references to a single extent before bees stops adding more.
/// Beyond this the per-reference space saving becomes negligible (<0.01%).
/// The kernel limit is (16 MiB buffer) / (24 bytes per (root, ino, offset) tuple)
/// = 16 * 1024 * 1024 / (3 * 8) = 699050, but that is far too large for practical
/// use: memory consumption and performance degrade severely well before that point.
const size_t BEES_MAX_EXTENT_REF_COUNT = 9999;

/// Maximum number of extent scan tasks queued in memory at once.
/// Larger values increase parallelism and memory use; smaller values reduce
/// repeated work after a restart.
const size_t BEES_MAX_EXTENT_TASK_COUNT = 9999;

/// Seconds between hash-table occupancy histogram logs.
const double BEES_HASH_TABLE_ANALYZE_INTERVAL = BEES_STATS_INTERVAL;

/// Minimum poll interval in seconds when waiting for a new btrfs transaction ID.
const double BEES_TRANSID_POLL_INTERVAL = 30.0;

/// Number of bytes to prefetch ahead during scanning to improve sequential read performance.
const size_t BEES_READAHEAD_SIZE = 1024 * 1024;

/// @name open(2) flag sets used throughout bees
/// @{
/// Flags common to all bees open() calls: no follow symlinks, non-blocking,
/// close-on-exec, no access-time updates, large-file aware, no controlling tty.
const int FLAGS_OPEN_COMMON   = O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC | O_NOATIME | O_LARGEFILE | O_NOCTTY;
const int FLAGS_OPEN_DIR      = FLAGS_OPEN_COMMON | O_RDONLY | O_DIRECTORY;  ///< Open a directory read-only.
const int FLAGS_OPEN_FILE     = FLAGS_OPEN_COMMON | O_RDONLY;                ///< Open a regular file read-only.
const int FLAGS_OPEN_FILE_RW  = FLAGS_OPEN_COMMON | O_RDWR;                  ///< Open a regular file read-write.
const int FLAGS_OPEN_TMPFILE  = FLAGS_OPEN_FILE_RW | O_TMPFILE | O_TRUNC | O_EXCL; ///< Create an anonymous temp file.
const int FLAGS_CREATE_FILE   = FLAGS_OPEN_COMMON | O_WRONLY | O_CREAT | O_EXCL;   ///< Create a new file exclusively.
/// @}

// macros ----------------------------------------

/// Emit a log message at syslog priority @p lv if that level is enabled.
/// @p x is an expression chain suitable for @c operator<< on an ostream.
#define BEESLOG(lv,x)   do { if (lv < bees_log_level) { Chatter __chatter(lv, BeesNote::get_name()); __chatter << x; } } while (0)

/// Current trace log level (controls verbosity of BEESTRACE output).
extern int bees_trace_level;

/// Register a RAII trace entry that fires if the enclosing scope exits
/// unexpectedly (exception or early return).  @p x is a stream expression.
#define BEESTRACE(x)   BeesTracer  SRSLY_WTF_C(beesTracer_,  __LINE__) ([&]()                 { BEESLOG(bees_trace_level, "TRACE: " << x << " at " << __FILE__ << ":" << __LINE__);   })

/// Register a RAII BeesTooLong guard that logs @p x if the scope takes longer
/// than BEES_TOO_LONG seconds.
#define BEESTOOLONG(x) BeesTooLong SRSLY_WTF_C(beesTooLong_, __LINE__) ([&](ostream &_btl_os) { _btl_os << x; })

/// Register a RAII BeesNote that publishes @p x as the current thread's status
/// while the enclosing scope is active.
#define BEESNOTE(x)    BeesNote    SRSLY_WTF_C(beesNote_,    __LINE__) ([&](ostream &_btl_os) { _btl_os << x; })

/// @name Leveled logging convenience macros
/// @{
#define BEESLOGERR(x)    BEESLOG(LOG_ERR, x)      ///< Log at LOG_ERR priority.
#define BEESLOGWARN(x)   BEESLOG(LOG_WARNING, x)  ///< Log at LOG_WARNING priority.
#define BEESLOGNOTICE(x) BEESLOG(LOG_NOTICE, x)   ///< Log at LOG_NOTICE priority.
#define BEESLOGINFO(x)   BEESLOG(LOG_INFO, x)     ///< Log at LOG_INFO priority.
#define BEESLOGDEBUG(x)  BEESLOG(LOG_DEBUG, x)    ///< Log at LOG_DEBUG priority.
/// @}

/// Log @p __x at NOTICE level exactly once per process lifetime.
#define BEESLOGONCE(__x) do { \
        static bool already_logged = false; \
        if (!already_logged) { \
                already_logged = true; \
                BEESLOGNOTICE(__x); \
        } \
} while (false)

/// Increment the named event counter in BeesStats::s_global by 1.
#define BEESCOUNT(stat) do { \
	BeesStats::s_global.add_count(#stat); \
} while (0)

/// Add @p amount to the named event counter in BeesStats::s_global.
#define BEESCOUNTADD(stat, amount) do { \
	BeesStats::s_global.add_count(#stat, (amount)); \
} while (0)

// ----------------------------------------

template <class T> class BeesStatTmpl;
template <class T> ostream& operator<<(ostream &os, const BeesStatTmpl<T> &bs);

/**
 * Thread-safe named counter map, parameterized on the counter type @p T.
 *
 * Counters are identified by arbitrary string keys and created on first use.
 * Used directly as BeesStats (uint64_t counts) and BeesRates (double rates).
 */
template <class T>
class BeesStatTmpl {
	map<string, T>	m_stats_map;
	mutable mutex	m_mutex;

	T& at(string idx);
public:
	BeesStatTmpl() = default;
	BeesStatTmpl(const BeesStatTmpl &that);
	BeesStatTmpl &operator=(const BeesStatTmpl &that);
	/// Add @p amount to the counter named @p idx, creating it if needed.
	void add_count(string idx, size_t amount = 1);
	/// Return the current value of the counter named @p idx (0 if absent).
	T at(string idx) const;

friend ostream& operator<< <>(ostream &os, const BeesStatTmpl<T> &bs);
friend struct BeesStats;
};

/// Instantiation of BeesStatTmpl for per-second rates derived from two samples.
using BeesRates = BeesStatTmpl<double>;

/// Global event-counter store for the bees daemon.
/// The static member @c s_global is the singleton used by the BEESCOUNT macro.
struct BeesStats : public BeesStatTmpl<uint64_t> {
	/// The singleton counter store written to by BEESCOUNT / BEESCOUNTADD.
	static BeesStats s_global;

	/// Compute the difference of two snapshots (for rate calculation).
	BeesStats operator-(const BeesStats &that) const;
	/// Divide all counters by @p d to produce a BeesRates instance.
	BeesRates operator/(double d) const;
	/// Return true if any counter is non-zero.
	explicit operator bool() const;
};

/**
 * RAII scope tracer used by the BEESTRACE macro.
 *
 * ### Linked-list / thread-local stack
 *
 * Each thread owns a singly-linked list of BeesTracer nodes rooted at the
 * thread-local pointer `tl_next_tracer`.  The list is managed in LIFO order:
 *
 *  - The **constructor** stores the current `tl_next_tracer` in `m_next_tracer`
 *    (making the old head the node's successor) and then sets `tl_next_tracer`
 *    to `this`.  The new node is therefore always at the head of the list.
 *  - The **destructor** pops the node by restoring `tl_next_tracer =
 *    m_next_tracer`, so cleanup always happens in the correct LIFO order even
 *    when scopes are nested.
 *
 * `m_next_tracer` points toward the *bottom* of the stack (oldest / outermost
 * frame), so iterating `tl_next_tracer → m_next_tracer → …` visits nodes
 * from the **innermost** (most-recently-pushed) frame outward to the outermost.
 *
 * ### Output order during exception unwinding
 *
 * Trace messages are emitted inside the destructor, which is called by the
 * C++ runtime during stack unwinding.  Because C++ unwinds the stack from the
 * innermost scope outward, destructors fire in *reverse* call order:
 *
 *  - If the call chain is A → B → C → D and D throws, the destructor order is
 *    D, C, B, A — but only A, B, C are BeesTracer scopes, so the output order
 *    is **C, B, A** (innermost-first), followed by the exception message itself
 *    at the point where the exception is caught and re-reported.
 *
 * In other words: output is shown in **reverse** call order (callee first,
 * caller last), not in the natural caller-to-callee reading order.
 *
 * ### Where output is emitted
 *
 * Each node's `m_func` callback was supplied by the BEESTRACE macro as a
 * lambda that calls `BEESLOG(bees_trace_level, "TRACE: " << … )`.  Inside
 * `~BeesTracer()`, `m_func()` is invoked directly (wrapped in a try/catch so
 * a misbehaving formatter cannot suppress the rest of the trace).  `BEESLOG`
 * expands to a `Chatter` stream write, which ultimately reaches syslog (or
 * stderr depending on the Chatter configuration).  The sentinel lines
 * "TRACE: --- BEGIN TRACE --- exception ---" and
 * "TRACE: ---  END  TRACE --- exception ---" are emitted by the first and last
 * destructor in the unwinding sequence respectively, bracketing the per-frame
 * messages.
 *
 * The static `trace_now()` method provides an unconditional trace dump
 * (independent of exception state) by walking `tl_next_tracer` directly from
 * head to tail and calling each node's `m_func`.
 */
class BeesTracer {
	function<void()> m_func;
	BeesTracer *m_next_tracer = 0;

	thread_local static BeesTracer *tl_next_tracer;
	thread_local static bool tl_silent;
	thread_local static bool tl_first;
public:
	/// Push a new tracer onto this thread's stack.
	/// If @p silent is true the trace message is suppressed unless re-enabled.
	BeesTracer(const function<void()> &f, bool silent = false);
	/// Pop this tracer from the thread's stack.
	~BeesTracer();
	/// Walk the current thread's tracer stack and emit all registered messages.
	static void trace_now();
	/// Return true if trace output for this thread is currently suppressed.
	static bool get_silent();
	/// Suppress trace output for this thread.
	static void set_silent();
};

/**
 * RAII per-thread status annotation used by the BEESNOTE macro.
 *
 * Each thread maintains a stack of BeesNote instances.  The topmost (most
 * recent) note is the thread's current status string, which is collected by
 * get_status() and published to the status file and progress output.
 * A process-global map (keyed by kernel TID) allows any thread to read all
 * threads' current status.
 *
 * The status reporting thread inspects the m_timer of each active note; if the
 * elapsed time exceeds a threshold, the elapsed time is included in the output.
 * Short-lived notes appear without a timestamp, while long-running notes (e.g.
 * waiting on a btrfs commit or a scheduled wakeup) display their age, making
 * it easy to spot unexpectedly slow operations.
 */
class BeesNote {
	function<void(ostream &)>	m_func;
	BeesNote			*m_prev;     ///< Previous note on this thread's stack.
	Timer				m_timer;     ///< Elapsed time since this note was pushed.
	string				m_name;      ///< Thread name at the time this note was created.

	static mutex			s_mutex;
	static map<pid_t, BeesNote*>	s_status;    ///< Global map: TID -> top-of-stack note.

	thread_local static BeesNote	*tl_next;    ///< Top of this thread's note stack.
	thread_local static string	tl_name;     ///< Logical name for the current thread.

public:
	/// Push a status note onto this thread's stack.
	BeesNote(function<void(ostream &)> f);
	/// Pop this note from the thread's stack.
	~BeesNote();

	/// Map from kernel TID to formatted status string for each known thread.
	using ThreadStatusMap = map<pid_t, string>;

	/// Collect and return the current status string for every thread.
	static ThreadStatusMap get_status();

	/// Set the logical name for the calling thread (shown in status output).
	static void set_name(const string &name);
	/// Return the logical name of the calling thread.
	static string get_name();
};

/**
 * Named thread wrapper with automatic join-on-destroy.
 *
 * Wraps @c std::thread with a human-readable name (used for logging and
 * status output) and a Timer that records when the thread was started.
 * The destructor joins the underlying thread if it is still running.
 */
class BeesThread {
	string			m_name;
	Timer			m_timer;
	shared_ptr<thread>	m_thread_ptr;

public:
	~BeesThread();
	/// Construct a named thread without starting it yet.
	BeesThread(string name);
	/// Construct and immediately start a named thread running @p args.
	BeesThread(string name, function<void()> args);
	/// Start the thread running @p args (must not already be running).
	void exec(function<void()> args);
	/// Block until the thread finishes.
	void join();
	/// Update the thread's display name (e.g. after a phase transition).
	void set_name(const string &name);
};

/**
 * Immutable identifier for a file within a btrfs filesystem.
 *
 * Combines the btrfs subvolume root ID and the inode number, which together
 * uniquely identify a file across all subvolumes of a mounted btrfs.
 * A default-constructed BeesFileId is falsy (both fields are zero).
 */
class BeesFileId {
	uint64_t	m_root;  ///< Btrfs subvolume root object ID.
	uint64_t	m_ino;   ///< Inode number within the subvolume.

public:
	/// Return the subvolume root ID.
	uint64_t root() const { return m_root; }
	/// Return the inode number.
	uint64_t ino() const { return m_ino; }
	bool operator<(const BeesFileId &that) const;
	bool operator!=(const BeesFileId &that) const;
	bool operator==(const BeesFileId &that) const;
	/// Return true if this ID is non-zero (i.e. refers to a real file).
	operator bool() const;
	/// Construct from a LOGICAL_INO result entry.
	BeesFileId(const BtrfsInodeOffsetRoot &bior);
	/// Construct by calling fstat() on @p fd to obtain root and inode.
	BeesFileId(int fd);
	/// Construct directly from a known root/inode pair.
	BeesFileId(uint64_t root, uint64_t ino);
	/// Construct a null (falsy) BeesFileId.
	BeesFileId();
};

ostream& operator<<(ostream &os, const BeesFileId &bfi);

/**
 * A half-open byte range [begin, end) within a specific file.
 *
 * Carries an optional open Fd and a lazily-populated BeesFileId and file size.
 * Can be constructed from either an open Fd or a BeesFileId alone; the Fd is
 * opened on demand via fd(ctx) when required.
 */
class BeesFileRange {
protected:
	Fd			m_fd;
	mutable BeesFileId	m_fid;        ///< Lazily populated from m_fd if not set at construction.
	off_t			m_begin = 0, m_end = 0;
	mutable off_t		m_file_size = -1;  ///< Lazily populated via fstat(); -1 means unknown.

public:

	BeesFileRange() = default;
	/// Construct from an open Fd and byte offsets.
	BeesFileRange(Fd fd, off_t begin, off_t end);
	/// Construct from a BeesFileId (no open Fd) and byte offsets.
	BeesFileRange(const BeesFileId &fid, off_t begin, off_t end);
	/// Construct from a BeesBlockData block.
	BeesFileRange(const BeesBlockData &bbd);

	/// Convert to a BeesBlockData (single block view of this range).
	operator BeesBlockData() const;

	bool operator<(const BeesFileRange &that) const;
	bool operator==(const BeesFileRange &that) const;
	bool operator!=(const BeesFileRange &that) const;

	/// Return true if the range is empty (begin == end).
	bool empty() const;
	/// Return true if both ranges refer to the same file (same BeesFileId).
	bool is_same_file(const BeesFileRange &that) const;
	/// Return true if this range overlaps @p that (same file, overlapping bytes).
	bool overlaps(const BeesFileRange &that) const;

	off_t begin() const { return m_begin; }
	off_t end() const { return m_end; }
	off_t size() const;

	/// @{ Lazy accessors (may call fstat() or ioctl on first use)
	off_t file_size() const;
	BeesFileId fid() const;
	/// @}

	/// Get the fd if there is one
	Fd fd() const;

	/// Get the fd, opening it if necessary
	Fd fd(const shared_ptr<BeesContext> &ctx);

	/// Copy the BeesFileId but not the Fd
	BeesFileRange copy_closed() const;

	/// Return true if a file identity (Fd or BeesFileId) is set.
	operator bool() const { return !!m_fd || m_fid; }

	/// @{ Extend the range by @p delta bytes
	off_t grow_end(off_t delta);
	off_t grow_begin(off_t delta);
	/// @}

	/// @{ Shrink the range by @p delta bytes
	off_t shrink_end(off_t delta);
	off_t shrink_begin(off_t delta);
	/// @}

friend ostream & operator<<(ostream &os, const BeesFileRange &bfr);
};

/**
 * Physical block address as stored in the bees hash table.
 *
 * Encodes the btrfs physical byte address of a block along with flag bits and
 * a compressed-extent offset packed into the low bits of the address.  Those
 * low bits are available because btrfs requires all extent addresses to be
 * aligned to BLOCK_SIZE_CLONE (4096 bytes); the kernel tree checker rejects
 * metadata with non-aligned extent addresses, so bees can safely repurpose
 * the bottom 12 bits.  The encoding would also break if clone_alignment were
 * ever below 4096, but no known architecture has a page size that small.
 *
 * Also carries "magic" sentinel values for blocks with no usable physical address.
 *
 * Layout of the 64-bit value:
 *  - Bits 63..12: physical block address
 *  - Bit 11: c_compressed_mask — extent is compressed
 *  - Bit 10: c_eof_mask — block is at an unaligned file EOF
 *  - Bit  9: c_toxic_mask — LOGICAL_INO was too slow; avoid this address
 *  - Bits 8..6: reserved (must be zero)
 *  - Bits 5..0: c_offset_mask — stored index 1–32 of the 4 KiB block within a
 *               compressed extent; get_compressed_offset() subtracts 1 before
 *               multiplying by BLOCK_SIZE_CLONE, so stored value 1 → byte offset 0,
 *               stored value 32 → byte offset 124 KiB.  Value 0 means no offset
 *               recorded.  This 1-based encoding wastes one bit relative to 0-based,
 *               but is retained for compatibility with the legacy hash table format.
 *
 * Assumption: bits 5..0 can encode stored values 1–32, so compressed extents are
 * assumed to be at most 32 × BLOCK_SIZE_CLONE = 128 KiB.  Code throughout
 * bees relies on this 32:1 ratio.  If btrfs ever allows compressed extents
 * larger than 128 KiB, the offset field and all dependent code must be updated.
 * (No btrfs version has violated this assumption to date.)
 */
class BeesAddress {
public:
	using Type = uint64_t;
private:
	Type	m_addr = ZERO;
	bool magic_check(uint64_t flags);
public:

	/// Sentinel values for blocks with no usable physical address.
	/// PREALLOC blocks have a physical address and are handled separately.
	enum MagicValue {
		ZERO,      ///< Default-constructed / uninitialized BeesAddress.
		DELALLOC,  ///< Block is in delayed allocation; physical address not yet assigned.
		HOLE,      ///< No extent present at this offset; no space allocated.
		UNUSABLE,  ///< Inline extent or unrecognized FIEMAP extent flags.
		LAST,      ///< Sentinel: all values >= LAST are non-magic physical addresses.
	};

	BeesAddress(Type addr = ZERO) : m_addr(addr) {}
	BeesAddress(MagicValue addr) : m_addr(addr) {}
	BeesAddress& operator=(const BeesAddress &that) = default;
	BeesAddress(const BeesAddress &that) = default;
	/// Implicit conversion to the raw 64-bit representation.
	operator Type() const { return m_addr; }
	bool operator==(const BeesAddress &that) const;
	bool operator==(const MagicValue that) const { return *this == BeesAddress(that); }
	bool operator!=(const BeesAddress &that) const { return !(*this == that); }
	bool operator!=(const MagicValue that) const { return *this != BeesAddress(that); }
	bool operator<(const BeesAddress &that) const;

	/// Minimum stored value in the compressed-block offset field (1-based; stored 1 → byte offset 0).
	static const Type c_offset_min = 1;
	/// Maximum stored value in the compressed-block offset field (stored 32 → byte offset 124 KiB).
	/// Equals 128 KiB / 4 KiB = 32 possible 4 KiB blocks within a compressed extent.
	static const Type c_offset_max = BLOCK_SIZE_MAX_COMPRESSED_EXTENT / BLOCK_SIZE_CLONE;

	/// Bitmask covering the compressed-block offset field (bits 5..0 = 0x3f).
	/// Computed as (c_offset_max - 1) | c_offset_max = 31 | 32 = 0x3f.
	/// Valid stored values are 1–32; stored value 0 means "no offset recorded".
	/// The mask extends to bit 5 (value 32 = c_offset_max), which is the maximum
	/// valid stored value, so all valid values are covered.
	static const Type c_offset_mask = (c_offset_max - 1) | (c_offset_max);

	static const Type c_compressed_mask = 1 << 11;  ///< Set when the extent is compressed.
	static const Type c_eof_mask = 1 << 10;          ///< Set when the block is at an unaligned EOF.
	static const Type c_toxic_mask = 1 << 9;         ///< Set when this address should be avoided.

	/// Union of all flag bits (useful for clearing flags to get the raw address).
	static const Type c_all_mask = c_compressed_mask | c_eof_mask | c_offset_mask | c_toxic_mask;

	/// Return true if this is a non-magic address within a compressed extent.
	bool is_compressed() const { return m_addr >= MagicValue::LAST && (m_addr & c_compressed_mask); }
	/// Return true if compressed and the offset field is also set.
	bool has_compressed_offset() const { return m_addr >= MagicValue::LAST && (m_addr & c_compressed_mask) && (m_addr & c_offset_mask); }
	/// Return true if this address has been marked toxic.
	bool is_toxic() const { return m_addr >= MagicValue::LAST && (m_addr & c_toxic_mask); }
	/// Return true if this block is at an unaligned file EOF.
	bool is_unaligned_eof() const { return m_addr >= MagicValue::LAST && (m_addr & c_eof_mask); }
	/// Return true if this is a magic sentinel value (not a real physical address).
	bool is_magic() const { return m_addr < MagicValue::LAST; }

	/// Extract the compressed-block offset field.
	Type get_compressed_offset() const;
	/// Return the physical address with all flag bits cleared, or 0 if magic.
	Type get_physical_or_zero() const;

	/// Mark this address as toxic (sets the toxic flag bit in place).
	void set_toxic();

	/// Construct by looking up the extent containing @p offset in @p fd via
	/// BtrfsExtentWalker (TREE_SEARCH_V2).  Uses the inode fd itself as the
	/// btrfs tree root, which works but is weaker than providing an explicit
	/// root fd.  Used where no BeesContext is available.
	BeesAddress(int fd, off_t offset);
	/// Construct by looking up the extent containing @p offset in @p fd via
	/// BtrfsExtentWalker (TREE_SEARCH_V2), using @p ctx->root_fd() as the
	/// btrfs tree root.  Preferred over the two-arg form when a context is
	/// available.
	/// @todo Upgrade two-arg call sites in BeesContext (dedup, rewrite_file_range)
	///       to use this form for consistency.
	BeesAddress(int fd, off_t offset, shared_ptr<BeesContext> ctx);
	/// Construct from a pre-fetched btrfs Extent item and a logical @p offset
	/// within it.  The Extent comes from TREE_SEARCH_V2 (via BtrfsExtentWalker);
	/// FIEMAP_EXTENT_* flags are used as an adapter representation on the struct,
	/// but no FIEMAP ioctl is issued.  This is the foundational constructor;
	/// the two-arg and three-arg forms both delegate here.
	BeesAddress(const Extent &e, off_t offset);
};

ostream & operator<<(ostream &os, const BeesAddress &ba);

/**
 * A named text file in a specific directory, with size-limited read and
 * atomic write semantics.
 *
 * Used for small state files (crawl state, hash-table stats) that must be
 * updated atomically.  Writes are performed by writing to a temp file and
 * renaming it into place.
 */
class BeesStringFile {
	Fd	m_dir_fd;   ///< Directory containing the file.
	string	m_name;     ///< Filename within m_dir_fd.
	size_t	m_limit;    ///< Maximum number of bytes read() will return.

public:
	/// Construct referencing @p name within @p dir_fd; read() is capped at @p limit bytes.
	BeesStringFile(Fd dir_fd, string name, size_t limit = 16 * 1024 * 1024);
	/// Read and return the file's contents (up to m_limit bytes).
	string read();
	/// Atomically replace the file's contents with @p contents.
	void write(string contents);
	/// Change the filename (does not rename on disk).
	void name(const string &new_name);
	/// Return the current filename.
	string name() const;
};

/**
 * Fixed-size persistent hash table mapping block content hashes to physical addresses.
 *
 * The table is stored in a file (@c beeshash.dat) and is memory-mapped for fast
 * in-process access.  It is organized as a flat array of Extents, each containing
 * multiple Buckets, each containing multiple Cells.  Hash values determine the
 * bucket; within the bucket entries are managed with an approximate LRU policy.
 *
 * Background threads handle:
 *  - Writeback: dirty extents are flushed to disk at BEES_FLUSH_RATE bytes/sec.
 *  - Prefetch: extents are read from disk ahead of the scan cursor.
 *
 * The table is intentionally lossy — old entries are evicted when a bucket is
 * full — so duplicate detection degrades gracefully as the working set grows
 * beyond the table capacity.
 */
class BeesHashTable {
	shared_ptr<BeesContext>	m_ctx;
public:
	using HashType = uint64_t;  ///< Type of content hash values (CityHash64).
	using AddrType = uint64_t;  ///< Type of physical block addresses (BeesAddress raw).

	/// A single entry in the hash table: one (hash, address) pair.
	struct Cell {
		HashType	e_hash;  ///< Content hash of the block.
		AddrType	e_addr;  ///< Physical address where a block with this hash was seen.
		Cell(const Cell &) = default;
		Cell &operator=(const Cell &) = default;
		Cell(HashType hash, AddrType addr) : e_hash(hash), e_addr(addr) { }
		bool operator==(const Cell &e) const { return tie(e_hash, e_addr) == tie(e.e_hash, e.e_addr); }
		bool operator!=(const Cell &e) const { return tie(e_hash, e_addr) != tie(e.e_hash, e.e_addr); }
		bool operator<(const Cell &e) const { return tie(e_hash, e_addr) < tie(e.e_hash, e.e_addr); }
	} __attribute__((packed));

private:
	/// Number of Cells that fit in one Bucket (= one memory page).
	static const uint64_t c_cells_per_bucket = BLOCK_SIZE_HASHTAB_BUCKET / sizeof(Cell);
	/// Number of Buckets in one Extent (= one I/O unit).
	static const uint64_t c_buckets_per_extent = BLOCK_SIZE_HASHTAB_EXTENT / BLOCK_SIZE_HASHTAB_BUCKET;

public:
	/// A contiguous group of Cells that share the same hash bucket; the unit of LRU eviction.
	union Bucket {
		Cell	p_cells[c_cells_per_bucket];
		uint8_t	p_byte[BLOCK_SIZE_HASHTAB_BUCKET];
	} __attribute__((packed));

	/// A contiguous group of Buckets; the unit of disk I/O and extent-level locking.
	union Extent {
		Bucket	p_buckets[BLOCK_SIZE_HASHTAB_EXTENT / BLOCK_SIZE_HASHTAB_BUCKET];
		uint8_t	p_byte[BLOCK_SIZE_HASHTAB_EXTENT];
	} __attribute__((packed));

	/// Open or create the hash table file at @p filename with the given @p size.
	BeesHashTable(shared_ptr<BeesContext> ctx, string filename, off_t size = BLOCK_SIZE_HASHTAB_EXTENT);
	~BeesHashTable();

	/// Signal background threads to stop.
	void stop_request();
	/// Block until background threads have stopped.
	void stop_wait();

	/// Return all Cells whose hash matches @p hash.
	vector<Cell>	find_cell(HashType hash);
	/// Insert (@p hash, @p addr) into a randomly chosen Cell in the bucket (evicting one if full).
	bool		push_random_hash_addr(HashType hash, AddrType addr);
	/// Remove the Cell matching both @p hash and @p addr from the bucket.
	void		erase_hash_addr(HashType hash, AddrType addr);
	/// Insert (@p hash, @p addr) at the front of the bucket (most-recently-used position).
	bool		push_front_hash_addr(HashType hash, AddrType addr);
	/// Write the extent at @p extent_index to disk if dirty; return true if written.
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

	/// Per-extent metadata tracked in memory alongside the mmap'd data.
	struct ExtentMetaData {
		shared_ptr<mutex> m_mutex_ptr;  ///< Serializes concurrent access to this extent.
		bool	m_dirty = false;         ///< True when the extent has unsaved modifications.
		bool	m_missing = true;        ///< True when the extent has not yet been loaded from disk.
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

/**
 * Serializable snapshot of a BeesCrawl's scan position.
 *
 * Persisted to beescrawl.dat so that a scan can resume from the same position
 * after a daemon restart.  The key fields (m_root, m_objectid, m_offset) identify
 * the next btrfs extent-tree item to process; the transid fields filter which
 * items are in scope for the current pass.
 */
struct BeesCrawlState {
	uint64_t	m_root;         ///< Subvolume root ID being scanned.
	uint64_t	m_objectid;     ///< Next objectid (inode) to scan in the extent tree.
	uint64_t	m_offset;       ///< Next offset within that objectid.
	uint64_t	m_min_transid;  ///< Lower bound on the transaction ID filter.
	uint64_t	m_max_transid;  ///< Upper bound on the transaction ID filter.
	time_t		m_started;      ///< Wall-clock time when this crawl pass was started.
	BeesCrawlState();
	bool operator<(const BeesCrawlState &that) const;
};

/**
 * Incremental scanner for a single btrfs subvolume's extent tree.
 *
 * BeesCrawl uses a BtrfsTreeObjectFetcher to walk EXTENT_DATA items in the
 * btrfs filesystem tree for one subvolume.  It delivers file ranges one at a
 * time to the caller via pop_front() / peek_front().  When the scan of the
 * current transaction window is complete, finished() returns true and
 * restart_crawl() advances the window.
 *
 * A ProgressTracker records the scan position so it can be checkpointed and
 * restored across daemon restarts.
 */
class BeesCrawl {
	shared_ptr<BeesContext>			m_ctx;

	mutex					m_mutex;
	BtrfsTreeItem				m_next_extent_data;  ///< Lookahead item from the last fetch.
	bool					m_deferred = false;  ///< True when crawl is intentionally paused.
	bool					m_finished = false;  ///< True when the current transid window is exhausted.

	mutex					m_state_mutex;
	ProgressTracker<BeesCrawlState>		m_state;  ///< Tracks and checkpoints the scan position.

	BtrfsTreeObjectFetcher			m_btof;  ///< btrfs tree cursor for this subvolume.

	/// Fetch the next batch of extent-data items into the internal queue.
	/// Returns false when the tree is exhausted.
	bool fetch_extents();
	/// Like fetch_extents() but retries more aggressively.
	void fetch_extents_harder();
	/// Advance to the next transaction window without holding m_mutex.
	bool restart_crawl_unlocked();
	/// Convert a raw BtrfsTreeItem into a BeesFileRange.
	BeesFileRange bti_to_bfr(const BtrfsTreeItem &bti) const;

public:
	/// Construct a crawl starting from @p initial_state.
	BeesCrawl(shared_ptr<BeesContext> ctx, BeesCrawlState initial_state);
	/// Return the next file range without consuming it (non-destructive).
	BeesFileRange peek_front();
	/// Return and consume the next file range.
	BeesFileRange pop_front();
	/// Acquire a progress hold at @p bcs (prevents checkpoint advance past this point).
	ProgressTracker<BeesCrawlState>::ProgressHolder hold_state(const BeesCrawlState &bcs);
	/// Return the earliest in-progress state (the low-water mark for checkpointing).
	BeesCrawlState get_state_begin();
	/// Return the current end-of-scan state.
	BeesCrawlState get_state_end() const;
	/// Force the crawl position to @p bcs (used when loading persisted state).
	void set_state(const BeesCrawlState &bcs);
	/// Set or clear the deferred flag.
	void deferred(bool def_setting);
	/// Return true if the crawl is currently deferred.
	bool deferred() const;
	/// Return true if the current transaction window has been fully scanned.
	bool finished() const;
	/// Advance to the next transaction window and begin a new pass.
	bool restart_crawl();
};

/// Abstract base for scan-ordering strategies (pImpl for BeesRoots).
class BeesScanMode;

/**
 * Manages btrfs subvolume roots and orchestrates the deduplication scan loop.
 *
 * BeesRoots owns one BeesCrawl per known subvolume and coordinates their
 * execution according to the active scan mode.  It also maintains the FD cache
 * for root and inode opens, persists crawl state to beescrawl.dat, and tracks
 * temporary files created during deduplication.
 *
 * Two background threads run continuously:
 *  - crawl_thread: selects and processes extents from the active BeesCrawl(s).
 *  - writeback_thread: periodically checkpoints crawl state to disk.
 */
class BeesRoots : public enable_shared_from_this<BeesRoots> {
	shared_ptr<BeesContext>			m_ctx;

	BeesStringFile				m_crawl_state_file;  ///< beescrawl.dat persistence file.
	using CrawlMap = map<uint64_t, shared_ptr<BeesCrawl>>;
	CrawlMap				m_root_crawl_map;   ///< Map from root ID to its BeesCrawl.
	mutex					m_mutex;
	/// Generation counter incremented when crawl state is modified.
	/// Writeback fires when dirty != clean.
	/// @note This pair may be a partially implemented feature or debugging remnant.
	/// Current semantics: writeback thread writes checkpoint when dirty != clean,
	/// then advances clean to match dirty.
	uint64_t				m_crawl_dirty = 0;
	/// Generation counter advanced to m_crawl_dirty after each successful checkpoint write.
	uint64_t				m_crawl_clean = 0;
	Timer					m_crawl_timer;
	BeesThread				m_crawl_thread;
	BeesThread				m_writeback_thread;
	RateEstimator				m_transid_re;       ///< Estimates how fast new transids appear.
	bool					m_workaround_btrfs_send = false;  ///< Skip RO roots to avoid interfering with btrfs send.

	shared_ptr<BeesScanMode>		m_scanner;  ///< Active scan-mode strategy object.

	mutex					m_tmpfiles_mutex;
	map<BeesFileId, Fd>			m_tmpfiles;  ///< Temp files in use by active dedup operations.

	mutex					m_stop_mutex;
	condition_variable			m_stop_condvar;
	bool					m_stop_requested = false;

	CrawlMap insert_new_crawl();
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
	void clear_caches();
	shared_ptr<BeesCrawl> insert_root(const BeesCrawlState &bcs);
	bool up_to_date(const BeesCrawlState &bcs);

friend class BeesCrawl;
friend class BeesFdCache;
friend class BeesScanMode;
friend class BeesScanModeSubvol;
friend class BeesScanModeExtent;

public:
	BeesRoots(shared_ptr<BeesContext> ctx);
	/// Start the crawl and writeback threads.
	void start();
	/// Signal background threads to stop.
	void stop_request();
	/// Block until background threads have stopped.
	void stop_wait();

	/// Register a temp file in the open_root_ino() lookup table.
	/// Temp files have no names, so this registry is the only way to resolve
	/// a BeesFileId back to a temp file fd.
	void insert_tmpfile(Fd fd);
	/// Deregister a temp file from the open_root_ino() lookup table
	/// when it is returned to the pool.
	void erase_tmpfile(Fd fd);

	/// Open (with caching) the root directory for subvolume @p root.
	Fd open_root(uint64_t root);
	/// Open (with caching) the file at inode @p ino in subvolume @p root.
	Fd open_root_ino(uint64_t root, uint64_t ino);
	/// Open (with caching) the file identified by @p bfi.
	Fd open_root_ino(const BeesFileId &bfi) { return open_root_ino(bfi.root(), bfi.ino()); }
	/// Return true if the subvolume @p root is mounted read-only.
	bool is_root_ro(uint64_t root);

	/// Scan ordering strategy selection.
	enum ScanMode {
		/// Subvol scan mode (mode 0, legacy): scans the same inode number in each subvol at
		/// close to the same time, maximising dedupe hit rate when subvols share a common ancestor.
		SCAN_MODE_LOCKSTEP,
		/// Subvol scan mode (mode 1, legacy): scans the next inode with new data in each subvol
		/// independently, distributing work across subvols in round-robin order.
		SCAN_MODE_INDEPENDENT,
		/// Subvol scan mode (mode 2, legacy): scans one subvol at a time in numerical subvol ID
		/// order, processing each subvol completely before proceeding to the next.
		SCAN_MODE_SEQUENTIAL,
		/// Subvol scan mode (mode 3, legacy): scans subvols with the highest @c min_transid
		/// (most recently completely scanned) first, falling back to independent ordering for ties.
		SCAN_MODE_RECENT,
		/// Extent scan mode (mode 4, default): scans the extent tree instead of subvol trees,
		/// reading each extent once regardless of the number of reflinks or snapshots.
		SCAN_MODE_EXTENT,
		SCAN_MODE_COUNT,       ///< Sentinel: number of valid scan modes.
	};

	/// Switch to @p new_mode; takes effect at the next scan iteration.
	void set_scan_mode(ScanMode new_mode);
	/// Enable or disable the btrfs-send workaround (skip RO subvolumes).
	void set_workaround_btrfs_send(bool do_avoid);

	/// Return the lowest transaction ID seen across all active crawls.
	uint64_t transid_min();
	/// Return the highest transaction ID currently committed on the filesystem.
	uint64_t transid_max();

	/// Block until at least @p count new transaction IDs have been committed.
	void wait_for_transid(const uint64_t count);
};

/// Thin wrapper around a CityHash64 content hash value.
/// Provides a named type to avoid confusion with raw uint64_t values.
struct BeesHash {
	using Type = uint64_t;

	BeesHash() : m_hash(0) { }
	BeesHash(Type that) : m_hash(that) { }
	/// Implicit conversion to the raw hash value.
	operator Type() const { return m_hash; }
	BeesHash& operator=(const Type that) { m_hash = that; return *this; }
	/// Hash @p len bytes starting at @p ptr using CityHash64.
	BeesHash(const uint8_t *ptr, size_t len);
private:
	Type	m_hash;

};

ostream & operator<<(ostream &os, const BeesHash &bh);

/**
 * A single block of file data with lazily computed hash and physical address.
 *
 * Carries an open Fd, a byte offset, and a length.  The actual data bytes,
 * content hash, and physical address are computed on first access and cached
 * in mutable members.  Used as the primary unit of comparison in the
 * deduplication pipeline.
 */
class BeesBlockData {
	using Blob = ByteVector;

	mutable Fd		m_fd;
	off_t			m_offset;
	off_t			m_length;
	mutable BeesAddress	m_addr;       ///< Physical address, lazily resolved via FIEMAP.
	mutable Blob		m_data;       ///< File contents, lazily read via pread().
	mutable BeesHash	m_hash;       ///< CityHash64 of m_data, lazily computed.
	mutable bool		m_hash_done = false;

public:
	/// Construct a block descriptor for @p read_length bytes at @p offset in @p fd.
	BeesBlockData(Fd fd, off_t offset, size_t read_length = BLOCK_SIZE_SUMS);
	/// Construct an empty (null) block.
	BeesBlockData();

	/// Return the open file descriptor.
	Fd fd() const { return m_fd; }

	/// Byte offset of the first byte of this block.
	off_t begin() const { return m_offset; }
	/// Byte offset one past the last byte of this block.
	off_t end() const { return m_offset + m_length; }
	/// Number of bytes in this block.
	off_t size() const { return m_length; }
	/// Return true if the block has zero length.
	bool empty() const { return !m_length; }

	/// Return the raw bytes (reads from disk on first call).
	const Blob &data() const;
	/// Return the CityHash64 of the block data (hashes on first call).
	BeesHash hash() const;
	/// Return the physical address (queries FIEMAP on first call).
	BeesAddress addr() const;
	/// Return true if all data bytes are zero.
	bool is_data_zero() const;
	/// Return true if this block's data equals @p that's data byte-for-byte.
	bool is_data_equal(const BeesBlockData &that) const;

	/// Override the cached physical address (e.g. after resolving a compressed extent).
	BeesBlockData &addr(const BeesAddress &a);

friend ostream &operator<<(ostream &, const BeesBlockData &);
};

/**
 * A matched pair of file ranges (source and destination) ready for deduplication.
 *
 * Inherits from @c pair<BeesFileRange, BeesFileRange> where @c first is the
 * source (the block already in the hash table) and @c second is the destination
 * (the duplicate block to be replaced).  The grow() method extends the pair
 * outward by comparing adjacent blocks, maximizing the amount deduped per ioctl.
 */
class BeesRangePair : public pair<BeesFileRange, BeesFileRange> {
public:
	BeesRangePair(const BeesFileRange &src, const BeesFileRange &dst);
	/// Attempt to extend both ranges outward by matching adjacent blocks.
	/// If @p constrained is true, restricts growth to aligned boundaries.
	/// Returns true if the pair was grown by at least one block.
	bool grow(shared_ptr<BeesContext> ctx, bool constrained);
	/// Shrink both ranges from their beginning by @p delta bytes.
	void shrink_begin(const off_t delta);
	/// Shrink both ranges from their end by @p delta bytes.
	void shrink_end(const off_t delta);
	/// Return a copy with the Fd members closed (only BeesFileId retained).
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

/**
 * LRU cache for btrfs root and inode file descriptors.
 *
 * Opening a file in btrfs requires opening the subvolume root first, then
 * opening the inode within it.  Both operations are expensive (ioctl + open),
 * so BeesFdCache keeps recently-used FDs in two separate LRU caches.
 * Cache capacity is controlled by BEES_ROOT_FD_CACHE_SIZE and
 * BEES_FILE_FD_CACHE_SIZE.
 */
class BeesFdCache {
	shared_ptr<BeesContext> 		m_ctx;
	LRUCache<Fd, uint64_t>			m_root_cache;         ///< Cache keyed by root ID.
	LRUCache<Fd, uint64_t, uint64_t>	m_file_cache;         ///< Cache keyed by (root, inode).
	Timer					m_root_cache_timer;   ///< Tracks time since last cache clear.
	Timer					m_file_cache_timer;

public:
	BeesFdCache(shared_ptr<BeesContext> ctx);
	/// Return a cached (or freshly opened) Fd for the subvolume root @p root.
	Fd open_root(uint64_t root);
	/// Return a cached (or freshly opened) Fd for inode @p ino in root @p root.
	Fd open_root_ino(uint64_t root, uint64_t ino);
	/// Evict all cached file descriptors.
	void clear();
};

/// Result of a LOGICAL_INO ioctl lookup for one physical block address.
struct BeesResolveAddrResult {
	BeesResolveAddrResult();
	/// List of (inode, offset, root) tuples that reference this physical address.
	vector<BtrfsInodeOffsetRoot> m_biors;
	/// True if the ioctl took too long and this address should be avoided in future.
	bool m_is_toxic = false;
	bool is_toxic() const { return m_is_toxic; }
};

/**
 * Central coordinator for the bees daemon.
 *
 * BeesContext owns and connects all major subsystems:
 *  - BeesConfig   — parsed configuration
 *  - BeesFdCache  — LRU cache for root and inode FDs
 *  - BeesHashTable — persistent dedup hash table
 *  - BeesRoots    — subvolume crawl management and scan loop
 *  - BeesTempFile pool — recycled staging files for partial-extent dedup
 *
 * It also provides the top-level scan entry points (scan_forward, dedup),
 * address resolution with caching (resolve_addr), per-inode locking, and
 * the blacklist of files that should never be deduplicated.
 *
 * Constructed once at startup; accessed throughout the daemon via
 * @c shared_ptr<BeesContext>.
 */
class BeesContext : public enable_shared_from_this<BeesContext> {
	Fd						m_home_fd;    ///< FD to the bees home directory (contains state files).

	shared_ptr<BeesFdCache>				m_fd_cache;
	shared_ptr<BeesHashTable>			m_hash_table;
	shared_ptr<BeesRoots>				m_roots;
	Pool<BeesTempFile>				m_tmpfile_pool;         ///< Recycled BeesTempFile instances.
	Pool<BtrfsIoctlLogicalInoArgs>			m_logical_ino_pool;     ///< Recycled LOGICAL_INO argument buffers.

	LRUCache<BeesResolveAddrResult, BeesAddress>	m_resolve_cache;  ///< Caches LOGICAL_INO results.

	string						m_root_path;  ///< Path to the mounted btrfs root.
	Fd						m_root_fd;    ///< FD to the btrfs root mount point.

	mutable mutex					m_blacklist_mutex;
	set<BeesFileId>					m_blacklist;  ///< Files excluded from deduplication.

	/// Timer recording total daemon uptime (used in status output).
	Timer						m_total_timer;

	NamedPtr<Exclusion, uint64_t>			m_extent_locks;  ///< Per-physical-extent mutex pool.
	NamedPtr<Exclusion, uint64_t>			m_inode_locks;   ///< Per-inode mutex pool.

	mutable mutex					m_stop_mutex;
	condition_variable				m_stop_condvar;
	bool						m_stop_requested = false;
	bool						m_stop_status = false;  ///< Exit status to report on stop.

	shared_ptr<BeesThread>				m_progress_thread;
	shared_ptr<BeesThread>				m_status_thread;

	mutex						m_progress_mtx;
	string						m_progress_str;  ///< Current progress string for status output.

	void set_root_fd(Fd fd);

	/// Perform an uncached LOGICAL_INO lookup for @p addr.
	BeesResolveAddrResult resolve_addr_uncached(BeesAddress addr);

	/// Hash and process one extent found during scanning.
	void scan_one_extent(const BeesFileRange &bfr, const Extent &e);
	/// Rewrite a file range by deduplicating it against already-hashed data.
	void rewrite_file_range(const BeesFileRange &bfr);

public:

	void set_root_path(string path);

	/// Return the FD to the btrfs root mount point.
	Fd root_fd() const { return m_root_fd; }
	/// Return the FD to the bees home directory.
	Fd home_fd();
	/// Return the path to the btrfs root mount point.
	string root_path() const { return m_root_path; }

	/// Scan @p bfr forward: hash each block, look up in hash table, and dedup matches.
	bool scan_forward(const BeesFileRange &bfr);

	/// Issue a LOGICAL_INO ioctl for @p bytenr, returning (inode, offset, root) tuples.
	shared_ptr<BtrfsIoctlLogicalInoArgs> logical_ino(uint64_t bytenr, bool all_refs);

	/// Return true if subvolume @p root is read-only.
	bool is_root_ro(uint64_t root);
	/// Copy @p src into @p tmpfile and return a BeesRangePair covering the copy.
	BeesRangePair dup_extent(const BeesFileRange &src, const shared_ptr<BeesTempFile> &tmpfile);
	/// Issue the FIDEDUPERANGE ioctl for @p brp; return true on success.
	bool dedup(const BeesRangePair &brp);

	/// Add @p fid to the deduplication blacklist.
	void blacklist_insert(const BeesFileId &fid);
	/// Remove @p fid from the deduplication blacklist.
	void blacklist_erase(const BeesFileId &fid);
	/// Return true if @p fid is on the blacklist.
	bool is_blacklisted(const BeesFileId &fid) const;

	/// Return the per-inode Exclusion mutex for @p inode (creates if absent).
	shared_ptr<Exclusion> get_inode_mutex(uint64_t inode);

	/// Return a cached LOGICAL_INO result for @p addr (calls kernel if not cached).
	BeesResolveAddrResult resolve_addr(BeesAddress addr);
	/// Evict @p addr from the resolve cache (called after dedup changes the mapping).
	void invalidate_addr(BeesAddress addr);
	/// Flush the entire resolve cache.
	void resolve_cache_clear();

	/// Write a full status report to the status file.
	void dump_status();
	/// Log current scan progress rates to the log.
	void show_progress();
	/// Update the in-memory progress string (read by dump_status).
	void set_progress(const string &str);
	/// Return the current progress string.
	string get_progress();

	/// Start all background threads.
	void start();
	/// Request all background threads to stop and block until they do.
	void stop();
	/// Return true if stop() has been called.
	bool stop_requested() const;

	shared_ptr<BeesFdCache>   fd_cache();    ///< Access the FD cache subsystem.
	shared_ptr<BeesHashTable> hash_table();  ///< Access the hash table subsystem.
	shared_ptr<BeesRoots>     roots();       ///< Access the roots/crawl subsystem.
	shared_ptr<BeesTempFile>  tmpfile();     ///< Borrow a temp file from the pool.

	/// Return a reference to the daemon uptime timer.
	const Timer &total_timer() const { return m_total_timer; }
};

/**
 * Resolves a physical block address to candidate file ranges and drives deduplication.
 *
 * Given a physical @c BeesAddress, BeesResolver:
 *  1. Calls BeesContext::resolve_addr() to get the list of (inode, offset, root)
 *     tuples that share that physical block (via LOGICAL_INO ioctl).
 *  2. For each candidate, reads the file data and compares it with the needle
 *     block to confirm a content match (not just a hash collision).
 *  3. On a confirmed match, issues FIDEDUPERANGE via BeesContext::dedup().
 *
 * Several boolean flags record what was found during the search, allowing the
 * caller to update the hash table appropriately (e.g. evict stale entries).
 */
class BeesResolver {
	shared_ptr<BeesContext>			m_ctx;
	BeesAddress				m_addr;           ///< The physical address being resolved.
	vector<BtrfsInodeOffsetRoot>		m_biors;          ///< Candidates from LOGICAL_INO.
	set<BeesFileRange>			m_ranges;         ///< Confirmed matching file ranges.
	size_t					m_bior_count;     ///< Number of LOGICAL_INO results.

	/// A content-matching candidate was found; deduplication is possible.
	bool					m_found_data = false;

	/// Content-matching deduplication was actually performed.
	bool					m_found_dup = false;

	/// A candidate with both matching hash and matching data was found
	/// (confirms the hash table entry is still valid).
	bool					m_found_hash = false;

	/// A candidate referencing the same physical address was found
	/// (the hash table entry points to a real block, even if data didn't match).
	bool					m_found_addr = false;

	/// A candidate had the correct physical address but mismatched data
	/// (hash collision or the block was overwritten after hashing).
	bool					m_wrong_data = false;

	/// This address is toxic: LOGICAL_INO is too slow to use it.
	bool					m_is_toxic = false;

	/// Verify @p bior against @p needle_bbd and return the matching file range if found.
	BeesFileRange chase_extent_ref(const BtrfsInodeOffsetRoot &bior, BeesBlockData &needle_bbd);
	/// Adjust @p needle to align with the extent in @p haystack for comparison.
	BeesBlockData adjust_offset(const BeesFileRange &haystack, const BeesBlockData &needle);
	/// Search LOGICAL_INO candidates for content matches against @p bbd.
	/// If @p just_one is true, stop after the first valid match (sufficient for
	/// a read-only hash-table lookup that only needs one reference).  If false,
	/// process all candidates (required for deduplication, which must replace
	/// every duplicate reference).
	void find_matches(bool just_one, BeesBlockData &bbd);

	// FIXME: Do we need these?  We probably always have at least one BBD
	BeesFileRange chase_extent_ref(const BtrfsInodeOffsetRoot &bior, BeesHash hash);
	BeesBlockData adjust_offset(const BeesFileRange &haystack, bool inexact, BeesHash needle);
	void find_matches(bool just_one, BeesHash hash);

public:
	/// Construct and immediately resolve @p addr.
	BeesResolver(shared_ptr<BeesContext> ctx, BeesAddress addr);
	/// Switch to a new address and re-resolve (resets all flags).
	BeesAddress addr(BeesAddress new_addr);

	/// Call @p visitor for each confirmed file range; visitor returns true to stop.
	bool for_each_extent_ref(BeesBlockData bbd, function<bool(const BeesFileRange &bfr)> visitor);

	/// Return all file ranges that contain data matching @p bbd.
	set<BeesFileRange> find_all_matches(BeesBlockData &bbd);
	/// Return all file ranges whose physical address matches and hash matches @p hash.
	set<BeesFileRange> find_all_matches(BeesHash hash);

	// TODO:  Replace these with "for_each_extent_ref"
	/// Return the first matching file range for @p bbd, or an empty range.
	BeesFileRange find_one_match(BeesBlockData &bbd);
	/// Return the first matching file range for @p hash, or an empty range.
	BeesFileRange find_one_match(BeesHash hash);

	/// Replace the btrfs extent reference at @p src_bfr with a deduplicated copy.
	/// Verifies the correct block via content hash.  Does not interact with the hash table.
	/// @note The names replace_src / replace_dst are misleading; these functions operate
	/// on btrfs extent references, not on the hash table.  They will be superseded by
	/// the planned extent-level deduplication redesign.
	void replace_src(const BeesFileRange &src_bfr);
	/// Replace the btrfs extent reference at @p dst_bfr with the deduplicated source; return the pair.
	/// Verifies the correct block via content hash.  Does not interact with the hash table.
	BeesRangePair replace_dst(const BeesFileRange &dst_bfr);

	/// Return true if a candidate with the correct physical address was found.
	bool found_addr() const { return m_found_addr; }
	/// Return true if a content-matching candidate was found.
	bool found_data() const { return m_found_data; }
	/// Return true if deduplication was successfully performed.
	bool found_dup() const { return m_found_dup; }
	/// Return true if a candidate with matching hash and data was found.
	bool found_hash() const { return m_found_hash; }
	/// Return true if this address should be avoided in future.
	bool is_toxic() const { return m_is_toxic; }
	/// Return the number of LOGICAL_INO candidates examined.
	size_t count() const { return m_bior_count; }
	/// Return the physical address currently being resolved.
	BeesAddress addr() const { return m_addr; }

	bool operator<(const BeesResolver &that) const;
};

/**
 * RAII guard that logs a warning if an operation exceeds a time limit.
 *
 * Extends Timer; the destructor compares elapsed time against m_limit and calls
 * m_func(ostream) to produce the warning message if the limit is exceeded.
 * Used via the BEESTOOLONG macro.
 */
class BeesTooLong : public Timer {
	using func_type = function<void(ostream &)>;
	/// Time limit in seconds; if elapsed time exceeds this, a warning is logged.
	double m_limit;
	/// Callback that writes the warning message to an ostream.
	func_type m_func;

public:
	/// Construct with a formatting callback and a time limit (default BEES_TOO_LONG).
	BeesTooLong(const func_type &func = [](ostream &os) { os << __PRETTY_FUNCTION__; }, double limit = BEES_TOO_LONG);
	/// Construct with a fixed string message and a time limit.
	BeesTooLong(const string &s, double limit = BEES_TOO_LONG);
	/// Replace the message callback.
	BeesTooLong &operator=(const func_type &s);
	/// Log the warning if elapsed time > m_limit.
	~BeesTooLong();
	/// Explicitly check the limit and log if exceeded (without resetting the timer).
	void check() const;

};

/// @name Global state and utility functions
/// @{

/// Current log verbosity level; messages at levels >= this are suppressed.
extern int bees_log_level;
/// Usage string printed by --help.
extern const char *BEES_USAGE;
/// Version string derived from git describe at build time.
extern const char *BEES_VERSION;
/// Per-thread PRNG used for randomized hash-table eviction.
extern thread_local default_random_engine bees_generator;

/// Format @p d as a human-readable number with SI suffix (e.g. "1.23M").
string pretty(double d);
/// open(2) with bees flag conventions; returns the raw fd or throws.
/// @p flags is @c uint64_t because @c openat2() (the preferred call) uses
/// @c uint64_t in @c open_how.  Falls back to @c openat() when @c openat2()
/// is unavailable; callers must ensure flags fit in @c int in that case.
int bees_openat(int const parent_fd, const char *const pathname, uint64_t const flags);
/// Emulate POSIX_FADV_WILLNEED for the given range by issuing actual pread() calls.
/// FADV_WILLNEED submits reads at iopriority 3, which are preempted by bees's own
/// reader threads and may never execute; using pread() forces the data into the page
/// cache immediately under normal thread scheduling.
void bees_readahead(int fd, off_t offset, size_t size);
/// Emulate POSIX_FADV_WILLNEED for two ranges simultaneously via pread().
/// @see bees_readahead
void bees_readahead_pair(int fd, off_t offset, size_t size, int fd2, off_t offset2, size_t size2);
/// Issue POSIX_FADV_DONTNEED to release cached pages for the given range.
void bees_unreadahead(int fd, off_t offset, size_t size);
/// Sleep if necessary to keep instantaneous CPU usage below the throttle target.
void bees_throttle(double time_used, const char *context);
/// Format a @c time_t as a human-readable local-time string.
string format_time(time_t t);
/// Return true if the current thread has a pending exception (i.e. is in a catch block).
bool exception_check();

/// @}
