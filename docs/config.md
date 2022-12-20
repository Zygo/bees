bees Configuration
==================

The only configuration parameter that *must* be provided is the hash
table size.  Other parameters are optional or hardcoded, and the defaults
are reasonable in most cases.

Hash Table Sizing
-----------------

Hash table entries are 16 bytes per data block.  The hash table stores
the most recently read unique hashes.  Once the hash table is full,
each new entry in the table evicts an old entry.

Here are some numbers to estimate appropriate hash table sizes:

    unique data size |  hash table size |average dedupe extent size
        1TB          |      4GB         |        4K
        1TB          |      1GB         |       16K
        1TB          |    256MB         |       64K
        1TB          |    128MB         |      128K <- recommended
        1TB          |     16MB         |     1024K
       64TB          |      1GB         |     1024K

Notes:

 * If the hash table is too large, no extra dedupe efficiency is
obtained, and the extra space just wastes RAM.  Extra space can also slow
bees down by preventing old data from being evicted, so bees wastes time
looking for matching data that is no longer present on the filesystem.

 * If the hash table is too small, bees extrapolates from matching
blocks to find matching adjacent blocks in the filesystem that have been
evicted from the hash table.  In other words, bees only needs to find
one block in common between two extents in order to be able to dedupe
the entire extents.  This provides significantly more dedupe hit rate
per hash table byte than other dedupe tools.

 * When counting unique data in compressed data blocks to estimate
optimum hash table size, count the *uncompressed* size of the data.

 * Another way to approach the hash table size is to simply decide how much
RAM can be spared without too much discomfort, give bees that amount of
RAM, and accept whatever dedupe hit rate occurs as a result.  bees will
do the best job it can with the RAM it is given.

Factors affecting optimal hash table size
-----------------------------------------

It is difficult to predict the net effect of data layout and access
patterns on dedupe effectiveness without performing deep inspection of
both the filesystem data and its structure--a task that is as expensive
as performing the deduplication.

* **Compression** on the filesystem reduces the average extent length
compared to uncompressed filesystems.  The maximum compressed extent
length on btrfs is 128KB, while the maximum uncompressed extent length
is 128MB.  Longer extents decrease the optimum hash table size while
shorter extents increase the optimum hash table size because the
probability of a hash table entry being present (i.e. unevicted) in
each extent is proportional to the extent length.

   As a rule of thumb, the optimal hash table size for a compressed
filesystem is 2-4x larger than the optimal hash table size for the same
data on an uncompressed filesystem.  Dedupe efficiency falls dramatically
with hash tables smaller than 128MB/TB as the average dedupe extent size
is larger than the largest possible compressed extent size (128KB).

* **Short writes** also shorten the average extent length and increase
optimum hash table size.  If a database writes to files randomly using
4K page writes, all of these extents will be 4K in length, and the hash
table size must be increased to retain each one (or the user must accept
a lower dedupe hit rate).

   Defragmenting files that have had many short writes increases the
extent length and therefore reduces the optimum hash table size.

* **Time between duplicate writes** also affects the optimum hash table
size.  bees reads data blocks in logical order during its first pass,
and after that new data blocks are read incrementally a few seconds or
minutes after they are written.  bees finds more matching blocks if there
is a smaller amount of data between the matching reads, i.e. there are
fewer blocks evicted from the hash table.  If most identical writes to
the filesystem occur near the same time, the optimum hash table size is
smaller.  If most identical writes occur over longer intervals of time,
the optimum hash table size must be larger to avoid evicting hashes from
the table before matches are found.

   For example, a build server normally writes out very similar source
code files over and over, so it will need a smaller hash table than a
backup server which has to refer to the oldest data on the filesystem
every time a new client machine's data is added to the server.

Scanning modes for multiple subvols
-----------------------------------

The `--scan-mode` option affects how bees schedules worker threads
between subvolumes.  Scan modes are an experimental feature and will
likely be deprecated in favor of a better solution.

Scan mode can be changed at any time by restarting bees with a different
mode option.  Scan state tracking is the same for all of the currently
implemented modes.  The difference between the modes is the order in
which subvols are selected.

If a filesystem has only one subvolume with data in it, then the
`--scan-mode` option has no effect.  In this case, there is only one
subvolume to scan, so worker threads will all scan that one.

Within a subvol, there is a single optimal scan order:  files are scanned
in ascending numerical inode order.  Each worker will scan a different
inode to avoid having the threads contend with each other for locks.
File data is read sequentially and in order, but old blocks from earlier
scans are skipped.

Between subvols, there are several scheduling algorithms with different
trade-offs:

Scan mode 0, "lockstep", scans the same inode number in each subvol at
close to the same time.  This is useful if the subvols are snapshots
with a common ancestor, since the same inode number in each subvol will
have similar or identical contents.  This maximizes the likelihood
that all of the references to a snapshot of a file are scanned at
close to the same time, improving dedupe hit rate and possibly taking
advantage of VFS caching in the Linux kernel.  If the subvols are
unrelated (i.e. not snapshots of a single subvol) then this mode does
not provide significant benefit over random selection.  This mode uses
smaller amounts of temporary space for shorter periods of time when most
subvols are snapshots.  When a new snapshot is created, this mode will
stop scanning other subvols and scan the new snapshot until the same
inode number is reached in each subvol, which will effectively stop
dedupe temporarily as this data has already been scanned and deduped
in the other snapshots.

Scan mode 1, "independent", scans the next inode with new data in each
subvol.  Each subvol's scanner shares inodes uniformly with all other
subvol scanners until the subvol has no new inodes left.  This mode makes
continuous forward progress across the filesystem and provides average
performance across a variety of workloads, but is slow to respond to new
data, and may spend a lot of time deduping short-lived subvols that will
soon be deleted when it is preferable to dedupe long-lived subvols that
will be the origin of future snapshots.  When a new snapshot is created,
previous subvol scans continue as before, but the time is now divided
among one more subvol.

Scan mode 2, "sequential", scans one subvol at a time, in numerical subvol
ID order, processing each subvol completely before proceeding to the
next subvol.  This avoids spending time scanning short-lived snapshots
that will be deleted before they can be fully deduped (e.g. those used
for `btrfs send`).  Scanning is concentrated on older subvols that are
more likely to be origin subvols for future snapshots, eliminating the
need to dedupe future snapshots separately.  This mode uses the largest
amount of temporary space for the longest time, and typically requires
a larger hash table to maintain dedupe hit rate.

Scan mode 3, "recent", scans the subvols with the highest `min_transid`
value first (i.e. the ones that were most recently completely scanned),
then falls back to "independent" mode to break ties.  This interrupts
long scans of old subvols to give a rapid dedupe response to new data,
then returns to the old subvols after the new data is scanned.  It is
useful for large filesystems with multiple active subvols and rotating
snapshots, where the first-pass scan can take months, but new duplicate
data appears every day.

The default scan mode is 1, "independent".

If you are using bees for the first time on a filesystem with many
existing snapshots, you should read about [snapshot gotchas](gotchas.md).

Threads and load management
---------------------------

By default, bees creates one worker thread for each CPU detected.
These threads then perform scanning and dedupe operations.  The number of
worker threads can be set with the [`--thread-count` and `--thread-factor`
options](options.md).

If desired, bees can automatically increase or decrease the number
of worker threads in response to system load.  This reduces impact on
the rest of the system by pausing bees when other CPU and IO intensive
loads are active on the system, and resumes bees when the other loads
are inactive.  This is configured with the [`--loadavg-target` and
`--thread-min` options](options.md).

Log verbosity
-------------

bees can be made less chatty with the [`--verbose` option](options.md).
