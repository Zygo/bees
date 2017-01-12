#include "bees.h"

#include "crucible/cache.h"
#include "crucible/string.h"

#include <fstream>
#include <tuple>

#include <inttypes.h>

using namespace crucible;
using namespace std;

string
format_time(time_t t)
{
	struct tm *tmp = localtime(&t);
	char buf[1024];
	strftime(buf, sizeof(buf), "%Y-%m-%d-%H-%M-%S", tmp);
	return buf;
}

ostream &
operator<<(ostream &os, const BeesCrawlState &bcs)
{
	time_t now = time(NULL);
	auto age = now - bcs.m_started;
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
	return tie(m_objectid, m_offset, m_root, m_min_transid, m_max_transid)
		< tie(that.m_objectid, that.m_offset, that.m_root, that.m_min_transid, that.m_max_transid);
}

string
BeesRoots::crawl_state_filename() const
{
	string rv;

	// Legacy filename included UUID
	rv += "beescrawl.";
	rv += m_ctx->root_uuid();
	rv += ".dat";

	struct stat buf;
	if (fstatat(m_ctx->home_fd(), rv.c_str(), &buf, AT_SYMLINK_NOFOLLOW)) {
		// Use new filename
		rv = "beescrawl.dat";
	}

	return rv;
}

void
BeesRoots::state_save()
{
	// Make sure we have a full complement of crawlers
	insert_new_crawl();

	BEESNOTE("saving crawl state");
	BEESLOG("Saving crawl state");
	BEESTOOLONG("Saving crawl state");

	Timer save_time;

	unique_lock<mutex> lock(m_mutex);

	// We don't have ofstreamat or ofdstream in C++11, so we're building a string and writing it with raw syscalls.
	ostringstream ofs;

	if (!m_crawl_dirty) {
		BEESLOG("Nothing to save");
		return;
	}

	for (auto i : m_root_crawl_map) {
		auto ibcs = i.second->get_state();
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

	if (ofs.str().empty()) {
		BEESLOG("Crawl state empty!");
		m_crawl_dirty = false;
		return;
	}

	lock.unlock();

	m_crawl_state_file.write(ofs.str());

	// Renaming things is hard after release
	if (m_crawl_state_file.name() != "beescrawl.dat") {
		renameat(m_ctx->home_fd(), m_crawl_state_file.name().c_str(), m_ctx->home_fd(), "beescrawl.dat");
		m_crawl_state_file.name("beescrawl.dat");
	}

	BEESNOTE("relocking crawl state");
	lock.lock();
	// Not really correct but probably close enough
	m_crawl_dirty = false;
	BEESLOG("Saved crawl state in " << save_time << "s");
}

BeesCrawlState
BeesRoots::crawl_state_get(uint64_t rootid)
{
	unique_lock<mutex> lock(m_mutex);
	auto rv = m_root_crawl_map.at(rootid)->get_state();
	THROW_CHECK2(runtime_error, rv.m_root, rootid, rv.m_root == rootid);
	return rv;
}

void
BeesRoots::crawl_state_set_dirty()
{
	unique_lock<mutex> lock(m_mutex);
	m_crawl_dirty = true;
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

	auto found = m_root_crawl_map.find(bcs.m_root);
	if (found != m_root_crawl_map.end()) {
		auto hold_this_until_unlocked = found->second;
		m_root_crawl_map.erase(found);
		m_crawl_dirty = true;
		lock.unlock();
	}
}

uint64_t
BeesRoots::transid_min()
{
	BEESNOTE("Calculating transid_min");
	unique_lock<mutex> lock(m_mutex);
	if (m_root_crawl_map.empty()) {
		return 0;
	}
	uint64_t rv = numeric_limits<uint64_t>::max();
	for (auto i : m_root_crawl_map) {
		rv = min(rv, i.second->get_state().m_min_transid);
	}
	return rv;
}

uint64_t
BeesRoots::transid_max()
{
	BEESNOTE("Calculating transid_max");
	uint64_t rv = 0;
	uint64_t root = 0;
	BEESTRACE("Calculating transid_max...");
	do {
		root = next_root(root);
		if (root) {
			catch_all([&]() {
				auto transid = btrfs_get_root_transid(open_root(root));
				rv = max(rv, transid);
				// BEESLOG("\troot " << root << " transid " << transid << " max " << rv);
			});
		}
	} while (root);
	return rv;
}

void
BeesRoots::writeback_thread()
{
	while (true) {
		// BEESNOTE(m_crawl_current << (m_crawl_dirty ? " (dirty)" : ""));
		BEESNOTE((m_crawl_dirty ? "dirty" : "clean") << ", interval " << BEES_WRITEBACK_INTERVAL << "s");

		catch_all([&]() {
			BEESNOTE("saving crawler state");
			state_save();
		});

		nanosleep(BEES_WRITEBACK_INTERVAL);

	}
}

void
BeesRoots::insert_root(const BeesCrawlState &new_bcs)
{
	unique_lock<mutex> lock(m_mutex);
	if (!m_root_crawl_map.count(new_bcs.m_root)) {
		auto new_bcp = make_shared<BeesCrawl>(m_ctx, new_bcs);
		auto new_pair = make_pair(new_bcs.m_root, new_bcp);
		m_root_crawl_map.insert(new_pair);
		m_crawl_dirty = true;
	}
}

void
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
	for (auto i : m_root_crawl_map) {
		excess_roots.insert(i.first);
	}
	lock.unlock();

	while (new_bcs.m_root) {
		excess_roots.erase(new_bcs.m_root);
		insert_root(new_bcs);
		BEESCOUNT(crawl_create);
		new_bcs.m_root = next_root(new_bcs.m_root);
	}

	for (auto i : excess_roots) {
		new_bcs.m_root = i;
		crawl_state_erase(new_bcs);
	}

	// Wake up crawl_roots if sleeping
	lock.lock();
	m_condvar.notify_all();
}

void
BeesRoots::state_load()
{
	BEESNOTE("loading crawl state");
	BEESLOG("loading crawl state");

	string crawl_data = m_crawl_state_file.read();

	for (auto line : split("\n", crawl_data)) {
		BEESLOG("Read line: " << line);
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
		BEESLOG("loaded_state " << loaded_state);
		insert_root(loaded_state);
	}
}

BeesRoots::BeesRoots(shared_ptr<BeesContext> ctx) :
	m_ctx(ctx),
	m_crawl_state_file(ctx->home_fd(), crawl_state_filename()),
	m_writeback_thread("crawl_writeback")
{
	m_lock_set.max_size(bees_worker_thread_count());

	catch_all([&]() {
		state_load();
	});
	m_writeback_thread.exec([&]() {
		writeback_thread();
	});
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
	BtrfsIoctlSearchKey sk;
	sk.tree_id = BTRFS_ROOT_TREE_OBJECTID;
	sk.min_objectid = sk.max_objectid = rootid;
	sk.min_type = sk.max_type = BTRFS_ROOT_BACKREF_KEY;

	BEESTRACE("sk " << sk);
	while (sk.min_objectid <= rootid) {
		sk.nr_items = 1024;
		sk.do_ioctl(m_ctx->root_fd());

		if (sk.m_result.empty()) {
			break;
		}

		for (auto i : sk.m_result) {
			sk.next_min(i);
			if (i.type == BTRFS_ROOT_BACKREF_KEY && i.objectid == rootid) {
				auto dirid = call_btrfs_get(btrfs_stack_root_ref_dirid, i.m_data);
				auto name_len = call_btrfs_get(btrfs_stack_root_ref_name_len, i.m_data);
				auto name_start = sizeof(struct btrfs_root_ref);
				auto name_end = name_len + name_start;
				THROW_CHECK2(runtime_error, i.m_data.size(), name_end, i.m_data.size() >= name_end);
				string name(i.m_data.data() + name_start, i.m_data.data() + name_end);

				auto parent_rootid = i.offset;
				// BEESLOG("parent_rootid " << parent_rootid << " dirid " << dirid << " name " << name);
				BEESTRACE("parent_rootid " << parent_rootid << " dirid " << dirid << " name " << name);
				Fd parent_fd = open_root(parent_rootid);
				if (!parent_fd) {
					BEESLOGTRACE("no parent_fd");
					continue;
				}

				if (dirid != BTRFS_FIRST_FREE_OBJECTID) {
					BEESTRACE("dirid " << dirid << " root " << rootid << " INO_PATH");
					BtrfsIoctlInoPathArgs ino(dirid);
					if (!ino.do_ioctl_nothrow(parent_fd)) {
						BEESINFO("dirid " << dirid << " inode path lookup failed in parent_fd " << name_fd(parent_fd));
						continue;
					}
					if (ino.m_paths.empty()) {
						BEESINFO("dirid " << dirid << " inode has no paths in parent_fd " << name_fd(parent_fd));
						continue;
					}
					BEESTRACE("dirid " << dirid << " path " << ino.m_paths.at(0));
					parent_fd = openat(parent_fd, ino.m_paths.at(0).c_str(), FLAGS_OPEN_DIR);
					if (!parent_fd) {
						BEESLOGTRACE("no parent_fd from dirid");
						continue;
					}
				}
				// BEESLOG("openat(" << name_fd(parent_fd) << ", " << name << ")");
				BEESTRACE("openat(" << name_fd(parent_fd) << ", " << name << ")");
				Fd rv = openat(parent_fd, name.c_str(), FLAGS_OPEN_DIR);
				if (!rv) {
					BEESLOGTRACE("open failed for name " << name);
					continue;
				}
				BEESCOUNT(root_found);

				// Verify correct root ID
				auto new_root_id = btrfs_get_root_id(rv);
				THROW_CHECK2(runtime_error, new_root_id, rootid, new_root_id == rootid);
				Stat st(rv);
				THROW_CHECK1(runtime_error, st.st_ino, st.st_ino == BTRFS_FIRST_FREE_OBJECTID);
				BEESINFO("open_root_nocache " << rootid << ": " << name_fd(rv));
				return rv;
			}
		}
	}
	BEESINFO("No path for rootid " << rootid);
	BEESCOUNT(root_notfound);
	return Fd();
}

Fd
BeesRoots::open_root(uint64_t rootid)
{
	// Ignore some of the crap that comes out of LOGICAL_INO
	if (rootid == BTRFS_ROOT_TREE_OBJECTID) {
		return Fd();
	}

	return m_ctx->fd_cache()->open_root(m_ctx, rootid);
}


uint64_t
BeesRoots::next_root(uint64_t root)
{
	BEESNOTE("Next root from " << root);
	BEESTRACE("Next root from " << root);

	// BTRFS_FS_TREE_OBJECTID has no backref keys so we can't find it that way
	if (root < BTRFS_FS_TREE_OBJECTID) {
		// BEESLOG("First root is BTRFS_FS_TREE_OBJECTID = " << BTRFS_FS_TREE_OBJECTID);
		return BTRFS_FS_TREE_OBJECTID;
	}

	BtrfsIoctlSearchKey sk;
	sk.tree_id = BTRFS_ROOT_TREE_OBJECTID;
	sk.min_type = sk.max_type = BTRFS_ROOT_BACKREF_KEY;
	sk.min_objectid = root + 1;

	while (true) {
		sk.nr_items = 1024;
		sk.do_ioctl(m_ctx->root_fd());

		if (sk.m_result.empty()) {
			return 0;
		}

		for (auto i : sk.m_result) {
			sk.next_min(i);
			if (i.type == BTRFS_ROOT_BACKREF_KEY) {
				// BEESLOG("Found root " << i.objectid << " parent " << i.offset);
				return i.objectid;
			}
		}
	}
}

Fd
BeesRoots::open_root_ino_nocache(uint64_t root, uint64_t ino)
{
	BEESTRACE("opening root " << root << " ino " << ino);

	Fd root_fd = open_root(root);
	if (!root_fd) {
		return root_fd;
	}

	BEESTOOLONG("open_root_ino(root " << root << ", ino " << ino << ")");

	BEESTRACE("looking up ino " << ino);
	BtrfsIoctlInoPathArgs ipa(ino);
	if (!ipa.do_ioctl_nothrow(root_fd)) {
		BEESINFO("Lookup root " << root << " ino " << ino << " failed: " << strerror(errno));
		return Fd();
	}

	BEESTRACE("searching paths for root " << root << " ino " << ino);
	Fd rv;
	if (ipa.m_paths.empty()) {
		BEESLOG("No paths for root " << root << " ino " << ino);
	}
	for (auto file_path : ipa.m_paths) {
		BEESTRACE("Looking up root " << root << " ino " << ino << " in dir " << name_fd(root_fd) << " path " << file_path);
		BEESCOUNT(open_file);
		// Try to open file RW, fall back to RO
		const char *fp_cstr = file_path.c_str();
		rv = openat(root_fd, fp_cstr, FLAGS_OPEN_FILE);
		if (!rv) {
			BEESCOUNT(open_fail);
			// errno == ENOENT is common during snapshot delete, ignore it
			if (errno != ENOENT) {
				BEESLOG("Could not open path '" << file_path << "' at root " << root << " " << name_fd(root_fd) << ": " << strerror(errno));
				BEESNOTE("ipa" << ipa);
			}
			continue;
		}

		// Correct inode?
		Stat file_stat(rv);
		if (file_stat.st_ino != ino) {
			BEESLOG("Opening " << name_fd(root_fd) << "/" << file_path << " found wrong inode " << file_stat.st_ino << " instead of " << ino);
			rv = Fd();
			BEESCOUNT(open_wrong_ino);
			break;
		}

		// Correct root?
		auto file_root = btrfs_get_root_id(rv);
		if (file_root != root) {
			BEESLOG("Opening " << name_fd(root_fd) << "/" << file_path << " found wrong root " << file_root << " instead of " << root);
			rv = Fd();
			BEESCOUNT(open_wrong_root);
			break;
		}

		// Same filesystem?
		Stat root_stat(root_fd);
		if (root_stat.st_dev != file_stat.st_dev) {
			BEESLOG("Opening root " << name_fd(root_fd) << " path " << file_path << " found path st_dev " << file_stat.st_dev << " but root st_dev is " << root_stat.st_dev);
			rv = Fd();
			BEESCOUNT(open_wrong_dev);
			break;
		}

		BEESTRACE("mapped " << BeesFileId(root, ino));
		BEESTRACE("\tto " << name_fd(rv));
		BEESCOUNT(open_hit);
		return rv;
	}

	// Odd, we didn't find a path.
	return Fd();
}

Fd
BeesRoots::open_root_ino(uint64_t root, uint64_t ino)
{
	return m_ctx->fd_cache()->open_root_ino(m_ctx, root, ino);
}

BeesCrawl::BeesCrawl(shared_ptr<BeesContext> ctx, BeesCrawlState initial_state) :
	m_ctx(ctx),
	m_state(initial_state),
	m_thread(astringprintf("crawl_%" PRIu64, m_state.m_root))
{
	m_thread.exec([&]() {
		crawl_thread();
	});
}

BeesCrawl::~BeesCrawl()
{
	BEESLOGNOTE("Stopping crawl thread " << m_state);
	unique_lock<mutex> lock(m_mutex);
	m_stopped = true;
	m_cond_stopped.notify_all();
	lock.unlock();
	BEESLOGNOTE("Joining crawl thread " << m_state);
	m_thread.join();
	BEESLOG("Stopped crawl thread " << m_state);
}

void
BeesCrawl::crawl_thread()
{
	Timer crawl_timer;
	LockSet<uint64_t>::Lock crawl_lock(m_ctx->roots()->lock_set(), m_state.m_root, false);
	while (!m_stopped) {
		BEESNOTE("waiting for crawl thread limit " << m_state);
		crawl_lock.lock();
		BEESNOTE("pop_front " << m_state);
		auto this_range = pop_front();
		crawl_lock.unlock();
		if (this_range) {
			catch_all([&]() {
				BEESNOTE("scan_forward " << this_range);
				m_ctx->scan_forward(this_range);
			});
			BEESCOUNT(crawl_scan);
		} else {
			auto crawl_time = crawl_timer.age();
			BEESLOGNOTE("Crawl ran out of data after " << crawl_time << "s, waiting for more...");
			unique_lock<mutex> lock(m_mutex);
			if (m_stopped) {
				break;
			}
			m_cond_stopped.wait_for(lock, chrono::duration<double>(BEES_COMMIT_INTERVAL));
			crawl_timer.reset();
		}
	}
	BEESLOG("Crawl thread stopped");
}

bool
BeesCrawl::next_transid()
{
	// If this crawl is recently empty, quickly and _silently_ bail out
	auto current_time = time(NULL);
	auto crawl_state = get_state();
	auto elapsed_time = current_time - crawl_state.m_started;
	if (elapsed_time < BEES_COMMIT_INTERVAL) {
		if (!m_deferred) {
			BEESLOG("Deferring next transid in " << get_state());
		}
		m_deferred = true;
		BEESCOUNT(crawl_defer);
		return false;
	}

	// Log performance stats from the old crawl
	BEESLOG("Next transid in " << get_state());

	// Start new crawl
	m_deferred = false;
	auto roots = m_ctx->roots();
	crawl_state.m_min_transid = crawl_state.m_max_transid;
	crawl_state.m_max_transid = roots->transid_max();
	crawl_state.m_objectid = 0;
	crawl_state.m_offset = 0;
	crawl_state.m_started = current_time;
	BEESLOG("Restarting crawl " << get_state());
	BEESCOUNT(crawl_restart);
	set_state(crawl_state);
	return true;
}

bool
BeesCrawl::fetch_extents()
{
	THROW_CHECK1(runtime_error, m_extents.size(), m_extents.empty());

	auto old_state = get_state();
	if (m_deferred || old_state.m_max_transid <= old_state.m_min_transid) {
		BEESTRACE("Nothing to crawl in " << get_state());
		return next_transid();
	}

	BEESNOTE("crawling " << get_state());
	// BEESLOG("Crawling " << get_state());

	Timer crawl_timer;

	BtrfsIoctlSearchKey sk(BEES_MAX_CRAWL_SIZE * (sizeof(btrfs_file_extent_item) + sizeof(btrfs_ioctl_search_header)));
	sk.tree_id = old_state.m_root;
	sk.min_objectid = old_state.m_objectid;
	sk.min_type = sk.max_type = BTRFS_EXTENT_DATA_KEY;
	sk.min_offset = old_state.m_offset;
	sk.min_transid = old_state.m_min_transid;
	sk.max_transid = old_state.m_max_transid;
	sk.nr_items = BEES_MAX_CRAWL_SIZE;

	// Lock in the old state
	set_state(old_state);

	BEESTRACE("Searching crawl sk " << static_cast<btrfs_ioctl_search_key&>(sk));
	bool ioctl_ok = false;
	{
		BEESNOTE("waiting to search crawl sk " << static_cast<btrfs_ioctl_search_key&>(sk));
		unique_lock<mutex> lock(bees_ioctl_mutex);

		BEESNOTE("searching crawl sk " << static_cast<btrfs_ioctl_search_key&>(sk));
		BEESTOOLONG("Searching crawl sk " << static_cast<btrfs_ioctl_search_key&>(sk));
		Timer crawl_timer;
		ioctl_ok = sk.do_ioctl_nothrow(m_ctx->root_fd());
		BEESCOUNTADD(crawl_ms, crawl_timer.age() * 1000);
	}

	if (ioctl_ok) {
		BEESCOUNT(crawl_search);
	} else {
		BEESLOG("Search ioctl failed: " << strerror(errno));
		BEESCOUNT(crawl_fail);
	}

	if (!ioctl_ok || sk.m_result.empty()) {
		BEESCOUNT(crawl_empty);
		BEESLOG("Crawl empty " << get_state());
		return next_transid();
	}

	// BEESLOG("Crawling " << sk.m_result.size() << " results from " << get_state());
	auto results_left = sk.m_result.size();
	BEESNOTE("crawling " << results_left << " results from " << get_state());
	size_t count_other = 0;
	size_t count_inline = 0;
	size_t count_unknown = 0;
	size_t count_data = 0;
	size_t count_low = 0;
	size_t count_high = 0;
	BeesFileRange last_bfr;
	for (auto i : sk.m_result) {
		sk.next_min(i);
		--results_left;
		BEESCOUNT(crawl_items);

		BEESTRACE("i = " << i);

		// We need the "+ 1" and objectid rollover that next_min does.
		auto new_state = get_state();
		new_state.m_objectid = sk.min_objectid;
		new_state.m_offset = sk.min_offset;

		// Saving state here means we can skip a search result
		// if we are interrupted.  Not saving state here means we
		// can fail to make forward progress in cases where there
		// is a lot of metadata we can't process.  Favor forward
		// progress over losing search results.
		set_state(new_state);

		// Ignore things that aren't EXTENT_DATA_KEY
		if (i.type != BTRFS_EXTENT_DATA_KEY) {
			++count_other;
			BEESCOUNT(crawl_nondata);
			continue;
		}

		auto gen = call_btrfs_get(btrfs_stack_file_extent_generation, i.m_data);
		if (gen < get_state().m_min_transid) {
			BEESCOUNT(crawl_gen_low);
			++count_low;
			// We probably want (need?) to scan these anyway.
			// continue;
		}
		if (gen > get_state().m_max_transid) {
			BEESCOUNT(crawl_gen_high);
			++count_high;
			// This shouldn't ever happen
			// continue;
		}

		auto type = call_btrfs_get(btrfs_stack_file_extent_type, i.m_data);
		switch (type) {
			default:
				BEESINFO("Unhandled file extent type " << type << " in root " << get_state().m_root << " ino " << i.objectid << " offset " << to_hex(i.offset));
				++count_unknown;
				BEESCOUNT(crawl_unknown);
				break;
			case BTRFS_FILE_EXTENT_INLINE:
				// Ignore these for now.
				// BEESINFO("Ignored file extent type INLINE in root " << get_state().m_root << " ino " << i.objectid << " offset " << to_hex(i.offset));
				++count_inline;
				// TODO:  replace with out-of-line dup extents
				BEESCOUNT(crawl_inline);
				break;
			case BTRFS_FILE_EXTENT_PREALLOC:
				BEESCOUNT(crawl_prealloc);
			case BTRFS_FILE_EXTENT_REG: {
				auto physical = call_btrfs_get(btrfs_stack_file_extent_disk_bytenr, i.m_data);
				auto ram = call_btrfs_get(btrfs_stack_file_extent_ram_bytes, i.m_data);
				auto len = call_btrfs_get(btrfs_stack_file_extent_num_bytes, i.m_data);
				auto offset = call_btrfs_get(btrfs_stack_file_extent_offset, i.m_data);
				BEESTRACE("Root " << get_state().m_root << " ino " << i.objectid << " physical " << to_hex(physical)
					<< " logical " << to_hex(i.offset) << ".." << to_hex(i.offset + len)
					<< " gen " << gen);
				++count_data;
				if (physical) {
					THROW_CHECK1(runtime_error, ram, ram > 0);
					THROW_CHECK1(runtime_error, len, len > 0);
					THROW_CHECK2(runtime_error, offset, ram, offset < ram);
					BeesFileId bfi(get_state().m_root, i.objectid);
					if (m_ctx->is_blacklisted(bfi)) {
						BEESCOUNT(crawl_blacklisted);
					} else {
						BeesFileRange bfr(bfi, i.offset, i.offset + len);
						// BEESNOTE("pushing bfr " << bfr << " limit " << BEES_MAX_QUEUE_SIZE);
						m_extents.insert(bfr);
						BEESCOUNT(crawl_push);
					}
				} else {
					BEESCOUNT(crawl_hole);
				}
				break;
			}
		}
	}
	// BEESLOG("Crawled inline " << count_inline << " data " << count_data << " other " << count_other << " unknown " << count_unknown << " gen_low " << count_low << " gen_high " << count_high << " " << get_state() << " in " << crawl_timer << "s");

	return true;
}

void
BeesCrawl::fetch_extents_harder()
{
	BEESNOTE("fetch_extents_harder " << get_state() << " with " << m_extents.size() << " extents");
	while (m_extents.empty()) {
		bool progress_made = fetch_extents();
		if (!progress_made) {
			return;
		}
	}
}

BeesFileRange
BeesCrawl::peek_front()
{
	unique_lock<mutex> lock(m_mutex);
	fetch_extents_harder();
	if (m_extents.empty()) {
		return BeesFileRange();
	}
	auto rv = *m_extents.begin();
	return rv;
}

BeesFileRange
BeesCrawl::pop_front()
{
	unique_lock<mutex> lock(m_mutex);
	fetch_extents_harder();
	if (m_extents.empty()) {
		return BeesFileRange();
	}
	auto rv = *m_extents.begin();
	m_extents.erase(m_extents.begin());
	return rv;
}

BeesCrawlState
BeesCrawl::get_state()
{
	unique_lock<mutex> lock(m_state_mutex);
	auto rv = m_state;
	return rv;
}

void
BeesCrawl::set_state(const BeesCrawlState &bcs)
{
	unique_lock<mutex> lock(m_state_mutex);
	m_state = bcs;
	lock.unlock();
	m_ctx->roots()->crawl_state_set_dirty();
}
