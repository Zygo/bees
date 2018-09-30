How bees Works
--------------

bees is a daemon designed to run continuously and maintain its state
across crashes and reboots.

bees uses checkpoints for persistence to eliminate the IO overhead of a
transactional data store.  On restart, bees will dedupe any data that
was added to the filesystem since the last checkpoint.  Checkpoints
occur every 15 minutes for scan progress, stored in `beescrawl.dat`.
The hash table trickle-writes to disk at 4GB/hour to `beeshash.dat`.
An hourly performance report is written to `beesstats.txt`.  There are
no special requirements for bees hash table storage--`.beeshome` could
be stored on a different btrfs filesystem, ext4, or even CIFS.

bees uses a persistent dedupe hash table with a fixed size configured
by the user.  Any size of hash table can be dedicated to dedupe.  If a
fast dedupe with low hit rate is desired, bees can use a hash table as
small as 16MB.

The bees hash table is loaded into RAM at startup and `mlock`ed so it
will not be swapped out by the kernel (if swap is permitted, performance
degrades to nearly zero).

bees scans the filesystem in a single pass which removes duplicate
extents immediately after they are detected.  There are no distinct
scanning and dedupe phases, so bees can start recovering free space
immediately after startup.

Once a filesystem scan has been completed, bees uses the `min_transid`
parameter of the `TREE_SEARCH_V2` ioctl to avoid rescanning old data
on future scans and quickly scan new data.  An incremental data scan
can complete in less than a millisecond on an idle filesystem.

Once a duplicate data block is identified, bees examines the nearby
blocks in the files where the matched block appears.  This allows bees
to find long runs of adjacent duplicate block pairs if it has an entry
for any one of the blocks in its hash table.  On typical data sets,
this means most of the blocks in the hash table are redundant and can
be discarded without significant impact on dedupe hit rate.

Hash table entries are grouped together into LRU lists.  As each block
is scanned, its hash table entry is inserted into the LRU list at a
random position.  If the LRU list is full, the entry at the end of the
list is deleted.  If a hash table entry is used to discover duplicate
blocks, the entry is moved to the beginning of the list.  This makes bees
unable to detect a small number of duplicates, but it dramatically
improves efficiency on filesystems with many small files.

Once the hash table fills up, old entries are evicted by new entries.
This means that the optimum hash table size is determined by the
distance between duplicate blocks on the filesystem rather than the
filesystem unique data size.  Even if the hash table is too small
to find all duplicates, it may still find _most_ of them, especially
during incremental scans where the data in many workloads tends to be
more similar.

When a duplicate block pair is found in two btrfs extents, bees will
attempt to match all other blocks in the newer extent with blocks in
the older extent (i.e. the goal is to keep the extent referenced in the
hash table and remove the most recently scanned extent).  If this is
possible, then the new extent will be replaced with a reference to the
old extent.  If this is not possible, then bees will create a temporary
copy of the unmatched data in the new extent so that the entire new
extent can be removed by deduplication.  This must be done because btrfs
cannot partially overwrite extents--the _entire_ extent must be replaced.
The temporary copy is then scanned during the next pass bees makes over
the filesystem for potential duplication of other extents.

When a block containing all-zero bytes is found, bees dedupes the extent
against a temporary file containing a hole, possibly creating temporary
copies of any non-zero data in the extent for later deduplication as
described above.  If the extent is compressed, bees avoids splitting
the extent in the middle as this generally has a negative impact on
compression ratio (and also triggers a [kernel bug](btrfs-kernel.md)).

bees does not store any information about filesystem structure, so
its performance is linear in the number or size of files.  The hash
table stores physical block numbers which are converted into paths
and FDs on demand through btrfs `SEARCH_V2` and `LOGICAL_INO` ioctls.
This eliminates the storage required to maintain the equivalents
of these functions in userspace, at the expense of encountering [some
kernel bugs in `LOGICAL_INO` performance](btrfs-kernel.md).

bees uses only the data-safe `FILE_EXTENT_SAME` (aka `FIDEDUPERANGE`)
kernel operations to manipulate user data, so it can dedupe live data
(e.g. build servers, sqlite databases, VM disk images).  It does not
modify file attributes or timestamps.

When bees has scanned all of the data, bees will pause until 10
transactions have been completed in the btrfs filesystem.  bees tracks
the current btrfs transaction ID over time so that it polls less often
on quiescent filesystems and more often on busy filesystems.

Scanning and deduplication work is performed by worker threads.  If the
[`--loadavg-target` option](options.md) is used, bees adjusts the number
of worker threads up or down as required to have a user-specified load
impact on the system.  The maximum and minimum number of threads is
configurable.  If the system load is too high then bees will stop until
the load falls to acceptable levels.
