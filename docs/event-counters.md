Event Counters
==============

General
-------

Event counters are used in bees to collect simple branch-coverage
statistics.  Every time bees makes a decision, it increments an event
counter, so there are _many_ event counters.

Events are grouped by prefix in their event names, e.g. `block` is block
I/O, `dedup` is deduplication requests, `tmp` is temporary files, etc.

Events with the suffix `_ms` count total milliseconds spent performing
the operation.  These are counted separately for each thread, so there
can be more than 1000 ms per second.

There is considerable overlap between some events, e.g. `example_try`
denotes an event that is counted when an action is attempted,
`example_hit` is counted when the attempt succeeds and has a desired
outcome, and `example_miss` is counted when the attempt succeeds but
the desired outcome is not achieved.  In most cases `example_try =
example_hit + example_miss + (`example failed and threw an exception`)`,
but some event groups defy such simplistic equations.

addr
----

The `addr` event group consists of operations related to translating `(root,
inode, offset)` tuples (i.e. logical position within a file) into btrfs
virtual block addresses (i.e. physical position on disk).

 * `addr_block`: The address of a block was computed.
 * `addr_compressed`: Obsolete implementation of `addr_compressed_offset`.
 * `addr_compressed_offset`: The address of a compressed block was computed.
 * `addr_delalloc`: The address of a block could not be computed due to
 delayed allocation.  Only possible when using obsolete `FIEMAP` code.
 * `addr_eof_e`: The address of a block at EOF that was not block-aligned was computed.
 * `addr_from_fd`: The address of a block was computed using a `fd`
 (open to the file in question) and `offset` pair.
 * `addr_from_root_fd`: The address of a block was computed using
 the filesystem root `fd` instead of the open file `fd` for the
 `TREE_SEARCH_V2` ioctl.  This is obsolete and should probably be removed
 at some point.
 * `addr_hole`: The address of a block in a hole was computed.
 * `addr_magic`: The address of a block cannot be determined in a way
 that bees can use (unrecognized flags or flags known to be incompatible
 with bees).
 * `addr_uncompressed`: The address of an uncompressed block was computed.
 * `addr_unrecognized`: The address of a block with unrecognized flags
 (i.e. kernel version newer than bees) was computed.
 * `addr_unusable`: The address of a block with unusable flags (i.e. flags
 that are known to be incompatible with bees) was computed.

adjust
------

The `adjust` event group consists of operations related to translating stored virtual block addresses (i.e. physical position on disk) to `(root, inode, offset)` tuples (i.e. logical positions within files).  `BeesResolver::adjust_offset` determines if a single candidate reference from the `LOGICAL_INO` ioctl corresponds to the requested btrfs virtual block address.

 * `adjust_compressed_offset_correct`: A block address corresponding to a compressed block was retrieved from the hash table and resolved to a physical block containing data that matches another block bees has already read.
 * `adjust_compressed_offset_wrong`: A block address corresponding to a compressed block was retrieved from the hash table and resolved to a physical block containing data that matches the hash but not the data from another block bees has already read (i.e. there was a hash collision).
 * `adjust_eof_fail`: A block address corresponding to a block at EOF that was not aligned to a block boundary matched another block bees already read, but the length of the unaligned data in both blocks was not equal.  This is usually caused by stale entries in the hash table pointing to blocks that have been overwritten since the hash table entries were created.  It can also be caused by hash collisions, but hashes are not yet computed at this point in the code, so this event does not correlate to the `hash_collision` counter.
 * `adjust_eof_haystack`: A block address from the hash table corresponding to a block at EOF that was not aligned to a block boundary was processed.
 * `adjust_eof_hit`: A block address corresponding to a block at EOF that was not aligned to a block boundary matched a similarly unaligned block that bees already read.
 * `adjust_eof_miss`: A block address from the hash table corresponding to a block at EOF that was not aligned to a block boundary did not match a similarly unaligned block that bees already read.
 * `adjust_eof_needle`: A block address from scanning the disk corresponding to a block at EOF that was not aligned to a block boundary was processed.
 * `adjust_exact`: A block address from the hash table corresponding to an uncompressed data block was processed to find its `(root, inode, offset)` references.
 * `adjust_exact_correct`: A block address corresponding to an uncompressed block was retrieved from the hash table and resolved to a physical block containing data that matches another block bees has already read.
 * `adjust_exact_wrong`: A block address corresponding to an uncompressed block was retrieved from the hash table and resolved to a physical block containing data that matches the hash but not the data from another block bees has already read (i.e. there was a hash collision).
 * `adjust_hit`: A block address was retrieved from the hash table and resolved to a physical block in an uncompressed extent containing data that matches the data from another block bees has already read (i.e. a duplicate match was found).
 * `adjust_miss`: A block address was retrieved from the hash table and resolved to a physical block containing a hash that does not match the hash from another block bees has already read (i.e. the hash table contained a stale entry and the data it referred to has since been overwritten in the filesystem).
 * `adjust_needle_too_long`: A block address was retrieved from the hash table, but when the corresponding extent item was retrieved, its offset or length were out of range to be a match (i.e. the hash table contained a stale entry and the data it referred to has since been overwritten in the filesystem).
 * `adjust_no_match`: A hash collision occurred (i.e. a block on disk was located with the same hash as the hash table entry but different data) .  Effectively an alias for `hash_collision` as it is not possible to have one event without the other.
 * `adjust_offset_high`: The `LOGICAL_INO` ioctl gave an extent item that does not overlap with the desired block because the extent item ends before the desired block in the extent data.
 * `adjust_offset_hit`: A block address was retrieved from the hash table and resolved to a physical block in a compressed extent containing data that matches the data from another block bees has already read (i.e. a duplicate match was found).
 * `adjust_offset_low`: The `LOGICAL_INO` ioctl gave an extent item that does not overlap with the desired block because the extent item begins after the desired block in the extent data.
 * `adjust_try`: A block address and extent item candidate were passed to `BeesResolver::adjust_offset` for processing.

block
-----

The `block` event group consists of operations related to reading data blocks from the filesystem.

 * `block_bytes`: Number of data bytes read.
 * `block_hash`: Number of block hashes computed.
 * `block_ms`: Total time reading data blocks.
 * `block_read`: Number of data blocks read.
 * `block_zero`: Number of data blocks read with zero contents (i.e. candidates for replacement with a hole).

bug
---

The `bug` event group consists of known bugs in bees.

 * `bug_bad_max_transid`: A bad `max_transid` was found and removed in `beescrawl.dat`.
 * `bug_bad_min_transid`: A bad `min_transid` was found and removed in `beescrawl.dat`.
 * `bug_dedup_same_physical`: `BeesContext::dedup` detected that the physical extent was the same for `src` and `dst`.  This has no effect on space usage so it is a waste of time, and also carries the risk of creating a toxic extent.
 * `bug_grow_pair_overlaps`: Two identical blocks were found, and while searching matching adjacent extents, the potential `src` grew to overlap the potential `dst`.  This would create a cycle where bees keeps trying to eliminate blocks but instead just moves them around.
 * `bug_hash_duplicate_cell`: Two entries in the hash table were identical.  This only happens due to data corruption or a bug.
 * `bug_hash_magic_addr`: An entry in the hash table contains an address with magic.  Magic addresses cannot be deduplicated so they should not be stored in the hash table.

chase
-----

The `chase` event group consists of operations connecting btrfs virtual block addresses with `(root, inode, offset)` tuples.  `resolve` is the top level, `adjust` is the bottom level, and `chase` is the middle level.  `BeesResolver::chase_extent_ref` iterates over `(root, inode, offset)` tuples from `LOGICAL_INO` and attempts to find a single matching block in the filesystem given a candidate block from an earlier `scan` operation.

 * `chase_corrected`: A matching block was resolved to a `(root, inode, offset)` tuple, but the offset of a block matching data did not match the offset given by `LOGICAL_INO`.
 * `chase_hit`: A block address was successfully and correctly translated to a `(root, inode, offset)` tuple.
 * `chase_no_data`: A block address was not successfully translated to a `(root, inode, offset)` tuple.
 * `chase_no_fd`: A `(root, inode)` tuple could not be opened (i.e. the file was deleted on the filesystem).
 * `chase_try`: A block address translation attempt started.
 * `chase_uncorrected`: A matching block was resolved to a `(root, inode, offset)` tuple, and the offset of a block matching data did match the offset given by `LOGICAL_INO`.
 * `chase_wrong_addr`: The btrfs virtual address (i.e. physical block address) found at a candidate `(root, inode, offset)` tuple did not match the expected btrfs virtual address (i.e. the filesystem was modified during the resolve operation).
 * `chase_wrong_magic`: The extent item at a candidate `(root, inode, offset)` tuple has magic bits and cannot match any btrfs virtual address in the hash table (i.e. the filesystem was modified during the resolve operation).

crawl
-----

The `crawl` event group consists of operations related to scanning btrfs trees to find new extent refs to scan for dedupe.

 * `crawl_again`: An inode crawl was restarted because the extent was already locked by another running crawl.
 * `crawl_blacklisted`: An extent was not scanned because it belongs to a blacklisted file.
 * `crawl_deferred_inode`: Two tasks attempted to scan the same inode at the same time, so one was deferred.
 * `crawl_done`: One pass over a subvol was completed.
 * `crawl_discard_high`: An extent that was too large for the crawler's size tier was discarded.
 * `crawl_discard_low`: An extent that was too small for the crawler's size tier was discarded.
 * `crawl_empty`: A `TREE_SEARCH_V2` ioctl call failed or returned an empty set (usually because all data in the subvol was scanned).
 * `crawl_extent`: The extent crawler queued all references to an extent for processing.
 * `crawl_fail`: A `TREE_SEARCH_V2` ioctl call failed.
 * `crawl_flop`: Small extent items were not skipped because the next extent started at or before the end of the previous extent.
 * `crawl_gen_high`: An extent item in the search results refers to an extent that is newer than the current crawl's `max_transid` allows.
 * `crawl_gen_low`: An extent item in the search results refers to an extent that is older than the current crawl's `min_transid` allows.
 * `crawl_hole`: An extent item in the search results refers to a hole.
 * `crawl_inline`: An extent item in the search results contains an inline extent.
 * `crawl_items`: An item in the `TREE_SEARCH_V2` data was processed.
 * `crawl_ms`: Time spent running the `TREE_SEARCH_V2` ioctl.
 * `crawl_no_empty`: Attempted to delete the last crawler.  Should never happen.
 * `crawl_nondata`: An item in the search results is not data.
 * `crawl_prealloc`: An extent item in the search results refers to a `PREALLOC` extent.
 * `crawl_push`: An extent item in the search results is suitable for scanning and deduplication.
 * `crawl_scan`: An extent item in the search results is submitted to `BeesContext::scan_forward` for scanning and deduplication.
 * `crawl_skip`: Small extent items were skipped because no extent of sufficient size was found within the minimum search distance.
 * `crawl_skip_ms`: Time spent skipping small extent items.
 * `crawl_search`: A `TREE_SEARCH_V2` ioctl call was successful.
 * `crawl_throttled`: Extent scan created too many work queue items and was prevented from creating any more.
 * `crawl_tree_block`: Extent scan found and skipped a metadata tree block.
 * `crawl_unknown`: An extent item in the search results has an unrecognized type.
 * `crawl_unthrottled`: Extent scan allowed to create work queue items again.

dedup
-----

The `dedup` (sic) event group consists of operations that deduplicate data.

 * `dedup_bytes`: Total bytes in extent references deduplicated.
 * `dedup_copy`: Total bytes copied to eliminate unique data in extents containing a mix of unique and duplicate data.
 * `dedup_hit`: Total number of pairs of identical extent references.
 * `dedup_miss`: Total number of pairs of non-identical extent references.
 * `dedup_ms`: Total time spent running the `FILE_EXTENT_SAME` (aka `FI_DEDUPERANGE` or `dedupe_file_range`) ioctl.
 * `dedup_prealloc_bytes`: Total bytes in eliminated `PREALLOC` extent references.
 * `dedup_prealloc_hit`: Total number of successfully eliminated `PREALLOC` extent references.
 * `dedup_prealloc_hit`: Total number of unsuccessfully eliminated `PREALLOC` extent references (i.e. filesystem data changed between scan and dedupe).
 * `dedup_try`: Total number of pairs of extent references submitted for deduplication.
 * `dedup_workaround_btrfs_send`: Total number of extent reference pairs submitted for deduplication that were discarded to workaround `btrfs send` bugs.

exception
---------

The `exception` event group consists of C++ exceptions.  C++ exceptions are thrown due to IO errors and internal constraint check failures.

 * `exception_caught`: Total number of C++ exceptions thrown and caught by a generic exception handler.
 * `exception_caught_silent`: Total number of "silent" C++ exceptions thrown and caught by a generic exception handler.  These are exceptions which are part of the correct and normal operation of bees.  The exceptions are logged at a lower log level.

extent
------

The `extent` event group consists of events that occur within the extent scanner.

 * `extent_deferred_inode`: A lock conflict was detected when two worker threads attempted to manipulate the same inode at the same time.
 * `extent_empty`: A complete list of references to an extent was created but the list was empty, e.g. because all refs are in deleted inodes or snapshots.
 * `extent_fail`: An ioctl call to `LOGICAL_INO` failed.
 * `extent_forward`: An extent reference was submitted for scanning.
 * `extent_mapped`: A complete map of references to an extent was created and added to the crawl queue.
 * `extent_ok`: An ioctl call to `LOGICAL_INO` completed successfully.
 * `extent_overflow`: A complete map of references to an extent exceeded `BEES_MAX_EXTENT_REF_COUNT`, so the extent was dropped.
 * `extent_ref_missing`: An extent reference reported by `LOGICAL_INO` was not found by later `TREE_SEARCH_V2` calls.
 * `extent_ref_ok`: One extent reference was queued for scanning.
 * `extent_restart`: An extent reference was requeued to be scanned again after an active extent lock is released.
 * `extent_retry`: An extent reference was requeued to be scanned again after an active inode lock is released.
 * `extent_skip`: A 4K extent with more than 1000 refs was skipped.
 * `extent_zero`: An ioctl call to `LOGICAL_INO` succeeded, but reported an empty list of extents.

hash
----

The `hash` event group consists of operations related to the bees hash table.

 * `hash_already`: A `(hash, address)` pair was already present in the hash table during a `BeesHashTable::push_random_hash_addr` operation.
 * `hash_bump`: An existing `(hash, address)` pair was moved forward in the hash table by a `BeesHashTable::push_random_hash_addr` operation.
 * `hash_collision`: A pair of data blocks was found with identical hashes but different data.
 * `hash_erase`: A `(hash, address)` pair in the hash table was removed because a matching data block could not be found in the filesystem (i.e. the hash table entry is out of date).
 * `hash_erase_miss`: A `(hash, address)` pair was reported missing from the filesystem but no such entry was found in the hash table (i.e. race between scanning threads or pair already evicted).
 * `hash_evict`: A `(hash, address)` pair was evicted from the hash table to accommodate a new hash table entry.
 * `hash_extent_in`: A hash table extent was read.
 * `hash_extent_out`: A hash table extent was written.
 * `hash_front`: A `(hash, address)` pair was pushed to the front of the list because it matched a duplicate block.
 * `hash_front_already`: A `(hash, address)` pair was pushed to the front of the list because it matched a duplicate block, but the pair was already at the front of the list so no change occurred.
 * `hash_insert`: A `(hash, address)` pair was inserted by `BeesHashTable::push_random_hash_addr`.
 * `hash_lookup`: The hash table was searched for `(hash, address)` pairs matching a given `hash`.

open
----

The `open` event group consists of operations related to translating `(root, inode)` tuples into open file descriptors (i.e. `open_by_handle` emulation for btrfs).

 * `open_clear`: The open FD cache was cleared to avoid keeping file descriptors open too long.
 * `open_fail_enoent`: A file could not be opened because it no longer exists (i.e. it was deleted or renamed during the lookup/resolve operations).
 * `open_fail_error`: A file could not be opened for other reasons (e.g. IO error, permission denied, out of resources).
 * `open_file`: A file was successfully opened.  This counts only the `open()` system call, not other reasons why the opened FD might not be usable.
 * `open_hit`: A file was successfully opened and the FD was acceptable.
 * `open_ino_ms`: Total time spent executing the `open()` system call.
 * `open_lookup_empty`: No paths were found for the inode in the `INO_PATHS` ioctl.
 * `open_lookup_enoent`: The `INO_PATHS` ioctl returned ENOENT.
 * `open_lookup_error`: The `INO_PATHS` ioctl returned a different error.
 * `open_lookup_ok`: The `INO_PATHS` ioctl successfully returned a list of one or more filenames.
 * `open_no_path`: All attempts to open a file by `(root, inode)` pair failed.
 * `open_no_root`: An attempt to open a file by `(root, inode)` pair failed because the `root` could not be opened.
 * `open_root_ms`: Total time spent opening subvol root FDs.
 * `open_wrong_dev`: A FD returned by `open()` did not match the device belonging to the filesystem subvol.
 * `open_wrong_flags`: A FD returned by `open()` had incompatible flags (`NODATASUM` / `NODATACOW`).
 * `open_wrong_ino`: A FD returned by `open()` did not match the expected inode (i.e. the file was renamed or replaced during the lookup/resolve operations).
 * `open_wrong_root`: A FD returned by `open()` did not match the expected subvol ID (i.e. `root`).

pairbackward
------------

The `pairbackward` event group consists of events related to extending matching block ranges backward starting from the initial block match found using the hash table.

 * `pairbackward_bof_first`: A matching pair of block ranges could not be extended backward because the beginning of the first (src) file was reached.
 * `pairbackward_bof_second`: A matching pair of block ranges could not be extended backward because the beginning of the second (dst) file was reached.
 * `pairbackward_hit`: A pair of matching block ranges was extended backward by one block.
 * `pairbackward_miss`: A pair of matching block ranges could not be extended backward by one block because the pair of blocks before the first block in the range did not contain identical data.
 * `pairbackward_ms`: Total time spent extending matching block ranges backward from the first matching block found by hash table lookup.
 * `pairbackward_overlap`: A pair of matching block ranges could not be extended backward by one block because this would cause the two block ranges to overlap.
 * `pairbackward_same`: A pair of matching block ranges could not be extended backward by one block because this would cause the two block ranges to refer to the same btrfs data extent.
 * `pairbackward_stop`: Stopped extending a pair of matching block ranges backward for any of the reasons listed here.
 * `pairbackward_toxic_addr`: A pair of matching block ranges was abandoned because the extended range would include a data block with a toxic address.
 * `pairbackward_toxic_hash`: A pair of matching block ranges was abandoned because the extended range would include a data block with a toxic hash.
 * `pairbackward_try`: Started extending a pair of matching block ranges backward.
 * `pairbackward_zero`: A pair of matching block ranges could not be extended backward by one block because the src block contained all zeros and was not compressed.

pairforward
-----------

The `pairforward` event group consists of events related to extending matching block ranges forward starting from the initial block match found using the hash table.

 * `pairforward_eof_first`: A matching pair of block ranges could not be extended forward because the end of the first (src) file was reached.
 * `pairforward_eof_malign`: A matching pair of block ranges could not be extended forward because the end of the second (dst) file was not aligned to a 4K boundary nor the end of the first (src) file.
 * `pairforward_eof_second`: A matching pair of block ranges could not be extended forward because the end of the second (dst) file was reached.
 * `pairforward_hit`: A pair of matching block ranges was extended forward by one block.
 * `pairforward_hole`: A pair of matching block ranges was extended forward by one block, and the block was a hole in the second (dst) file.
 * `pairforward_miss`: A pair of matching block ranges could not be extended forward by one block because the pair of blocks after the last block in the range did not contain identical data.
 * `pairforward_ms`: Total time spent extending matching block ranges forward from the first matching block found by hash table lookup.
 * `pairforward_overlap`: A pair of matching block ranges could not be extended forward by one block because this would cause the two block ranges to overlap.
 * `pairforward_same`: A pair of matching block ranges could not be extended forward by one block because this would cause the two block ranges to refer to the same btrfs data extent.
 * `pairforward_stop`: Stopped extending a pair of matching block ranges forward for any of the reasons listed here.
 * `pairforward_toxic_addr`: A pair of matching block ranges was abandoned because the extended range would include a data block with a toxic address.
 * `pairforward_toxic_hash`: A pair of matching block ranges was abandoned because the extended range would include a data block with a toxic hash.
 * `pairforward_try`: Started extending a pair of matching block ranges forward.
 * `pairforward_zero`: A pair of matching block ranges could not be extended backward by one block because the src block contained all zeros and was not compressed.

progress
--------

The `progress` event group consists of events related to progress estimation.

 * `progress_no_data_bg`: Failed to retrieve any data block groups from the filesystem.
 * `progress_not_created`: A crawler for one size tier had not been created for the extent scanner.
 * `progress_complete`: A crawler for one size tier has completed a scan.
 * `progress_not_found`: The extent position for a crawler does not correspond to any block group.
 * `progress_out_of_bg`: The extent position for a crawler does not correspond to any data block group.
 * `progress_ok`: Table of progress and ETA created successfully.

readahead
---------

The `readahead` event group consists of events related to data prefetching (formerly calls to `posix_fadvise` or `readahead`, but now emulated in userspace).

 * `readahead_bytes`: Number of bytes prefetched.
 * `readahead_count`: Number of read calls.
 * `readahead_clear`: Number of times the duplicate read cache was cleared.
 * `readahead_fail`: Number of read errors during prefetch.
 * `readahead_ms`: Total time spent emulating readahead in user-space (kernel readahead is not measured).
 * `readahead_skip`: Number of times a duplicate read was identified in the cache and skipped.
 * `readahead_unread_ms`: Total time spent running `posix_fadvise(..., POSIX_FADV_DONTNEED)`.

replacedst
----------

The `replacedst` event group consists of events related to replacing a single reference to a dst extent using any suitable src extent (i.e. eliminating a single duplicate extent ref during a crawl).

 * `replacedst_dedup_hit`: A duplicate extent reference was identified and removed.
 * `replacedst_dedup_miss`: A duplicate extent reference was identified, but src and dst extents did not match (i.e. the filesystem changed in the meantime).
 * `replacedst_grown`: A duplicate block was identified, and adjacent blocks were duplicate as well.
 * `replacedst_overlaps`: A pair of duplicate block ranges was identified, but the pair was not usable for dedupe because the two ranges overlap.
 * `replacedst_same`: A pair of duplicate block ranges was identified, but the pair was not usable for dedupe because the physical block ranges were the same.
 * `replacedst_try`: A duplicate block was identified and an attempt was made to remove it (i.e. this is the total number of replacedst calls).

replacesrc
----------

The `replacesrc` event group consists of events related to replacing every reference to a src extent using a temporary copy of the extent's data (i.e. eliminating leftover unique data in a partially duplicate extent during a crawl).

 * `replacesrc_dedup_hit`: A duplicate extent reference was identified and removed.
 * `replacesrc_dedup_miss`: A duplicate extent reference was identified, but src and dst extents did not match (i.e. the filesystem changed in the meantime).
 * `replacesrc_grown`: A duplicate block was identified, and adjacent blocks were duplicate as well.
 * `replacesrc_overlaps`: A pair of duplicate block ranges was identified, but the pair was not usable for dedupe because the two ranges overlap.
 * `replacesrc_try`: A duplicate block was identified and an attempt was made to remove it (i.e. this is the total number of replacedst calls).


resolve
-------

The `resolve` event group consists of operations related to translating a btrfs virtual block address (i.e. physical block address) to a `(root, inode, offset)` tuple (i.e. locating and opening the file containing a matching block).  `resolve` is the top level, `chase` and `adjust` are the lower two levels.

 * `resolve_empty`: The `LOGICAL_INO` ioctl returned successfully with an empty reference list (0 items).
 * `resolve_fail`: The `LOGICAL_INO` ioctl returned an error.
 * `resolve_large`: The `LOGICAL_INO` ioctl returned more than 2730 results (the limit of the v1 ioctl).
 * `resolve_ms`: Total time spent in the `LOGICAL_INO` ioctl (i.e. wallclock time, not kernel CPU time).
 * `resolve_ok`: The `LOGICAL_INO` ioctl returned success.
 * `resolve_overflow`: The `LOGICAL_INO` ioctl returned 9999 or more extents (the limit configured in `bees.h`).
 * `resolve_toxic`: The `LOGICAL_INO` ioctl took more than 0.1 seconds of kernel CPU time.

root
----

The `root` event group consists of operations related to translating a btrfs root ID (i.e. subvol ID) into an open file descriptor by navigating the btrfs root tree.

 * `root_clear`: The root FD cache was cleared.
 * `root_found`: A root FD was successfully opened.
 * `root_notfound`: A root FD could not be opened because all candidate paths could not be opened, or there were no paths available.
 * `root_ok`: A root FD was opened and its correctness verified.
 * `root_open_fail`: A root FD `open()` attempt returned an error.
 * `root_parent_open_fail`: A recursive call to open the parent of a subvol failed.
 * `root_parent_open_ok`: A recursive call to open the parent of a subvol succeeded.
 * `root_parent_open_try`: A recursive call to open the parent of a subvol was attempted.
 * `root_parent_path_empty`: No path could be found to connect a parent root FD to its child.
 * `root_parent_path_fail`: The `INO_PATH` ioctl failed to find a name for a child subvol relative to its parent.
 * `root_parent_path_open_fail`: The `open()` call in a recursive call to open the parent of a subvol returned an error.
 * `root_workaround_btrfs_send`: A subvol was determined to be read-only and disabled to implement the btrfs send workaround.

scan
----

The `scan` event group consists of operations related to scanning incoming data.  This is where bees finds duplicate data and populates the hash table.

 * `scan_blacklisted`: A blacklisted extent was passed to `scan_forward` and dropped.
 * `scan_block`: A block of data was scanned.
 * `scan_compressed_no_dedup`: An extent that was compressed contained non-zero, non-duplicate data.
 * `scan_dup_block`: Number of duplicate block references deduped.
 * `scan_dup_hit`: A pair of duplicate block ranges was found.
 * `scan_dup_miss`: A pair of duplicate blocks was found in the hash table but not in the filesystem.
 * `scan_extent`: An extent was scanned (`scan_one_extent`).
 * `scan_forward`: A logical byte range was scanned (`scan_forward`).
 * `scan_found`: An entry was found in the hash table matching a scanned block from the filesystem.
 * `scan_hash_hit`: A block was found on the filesystem corresponding to a block found in the hash table.
 * `scan_hash_miss`: A block was not found on the filesystem corresponding to a block found in the hash table.
 * `scan_hash_preinsert`: A non-zero data block's hash was prepared for possible insertion into the hash table.
 * `scan_hash_insert`: A non-zero data block's hash was inserted into the hash table.
 * `scan_hole`: A hole extent was found during scan and ignored.
 * `scan_interesting`: An extent had flags that were not recognized by bees and was ignored.
 * `scan_lookup`: A hash was looked up in the hash table.
 * `scan_malign`: A block being scanned matched a hash at EOF in the hash table, but the EOF was not aligned to a block boundary and the two blocks did not have the same length.
 * `scan_push_front`: An entry in the hash table matched a duplicate block, so the entry was moved to the head of its LRU list.
 * `scan_reinsert`: A copied block's hash and block address was inserted into the hash table.
 * `scan_resolve_hit`: A block address in the hash table was successfully resolved to an open FD and offset pair.
 * `scan_resolve_zero`: A block address in the hash table was not resolved to any subvol/inode pair, so the corresponding hash table entry was removed.
 * `scan_rewrite`: A range of bytes in a file was copied, then the copy deduped over the original data.
 * `scan_root_dead`: A deleted subvol was detected.
 * `scan_seen_clear`: The list of recently scanned extents reached maximum size and was cleared.
 * `scan_seen_erase`: An extent reference was modified by scan, so all future references to the extent must be scanned.
 * `scan_seen_hit`: A scan was skipped because the same extent had recently been scanned.
 * `scan_seen_insert`: An extent reference was not modified by scan and its hashes have been inserted into the hash table, so all future references to the extent can be ignored.
 * `scan_seen_miss`: A scan was not skipped because the same extent had not recently been scanned (i.e. the extent was scanned normally).
 * `scan_skip_bytes`: Nuisance dedupe or hole-punching would save less than half of the data in an extent.
 * `scan_skip_ops`: Nuisance dedupe or hole-punching would require too many dedupe/copy/hole-punch operations in an extent.
 * `scan_toxic_hash`: A scanned block has the same hash as a hash table entry that is marked toxic.
 * `scan_toxic_match`: A hash table entry points to a block that is discovered to be toxic.
 * `scan_twice`: Two references to the same block have been found in the hash table.
 * `scan_zero`: A data block containing only zero bytes was detected.

scanf
-----

The `scanf` event group consists of operations related to `BeesContext::scan_forward`.  This is the entry point where `crawl` schedules new data for scanning.

 * `scanf_deferred_extent`: Two tasks attempted to scan the same extent at the same time, so one was deferred.
 * `scanf_eof`: Scan past EOF was attempted.
 * `scanf_extent`: A btrfs extent item was scanned.
 * `scanf_extent_ms`: Total thread-seconds spent scanning btrfs extent items.
 * `scanf_no_fd`: References to a block from the hash table were found, but a FD could not be opened.
 * `scanf_total`: A logical byte range of a file was scanned.
 * `scanf_total_ms`: Total thread-seconds spent scanning logical byte ranges.

Note that in current versions of bees, `scan_forward` is passed extents
that correspond exactly to btrfs extent items, so the `scanf_extent` and
`scanf_total` numbers can only be different if the filesystem changes
between crawl time and scan time.

sync
----

The `sync` event group consists of operations related to the `fsync` workarounds in bees.

 * `sync_count`: `fsync()` was called on a temporary file.
 * `sync_ms`: Total time spent executing `fsync()`.

tmp
---

The `sync` event group consists of operations related temporary files and the data within them.

 * `tmp_aligned`: A temporary extent was allocated on a block boundary.
 * `tmp_block`: Total number of temporary blocks copied.
 * `tmp_block_zero`: Total number of temporary hole blocks copied.
 * `tmp_bytes`: Total number of temporary bytes copied.
 * `tmp_copy`: Total number of extents copied.
 * `tmp_copy_ms`: Total time spent copying extents.
 * `tmp_create`: Total number of temporary files created.
 * `tmp_create_ms`: Total time spent creating temporary files.
 * `tmp_hole`: Total number of hole extents created.
 * `tmp_realign`: A temporary extent was not aligned to a block boundary.
 * `tmp_resize`: A temporary file was resized with `ftruncate()`
 * `tmp_resize_ms`: Total time spent in `ftruncate()`
 * `tmp_trunc`: The temporary file size limit was exceeded, triggering a new temporary file creation.
