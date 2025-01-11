bees Configuration
==================

The only configuration parameter that *must* be provided is the hash
table size.  Other parameters are optional or hardcoded, and the defaults
are reasonable in most cases.

Hash Table Sizing
-----------------

Hash table entries are 16 bytes per data block.  The hash table stores the
most recently read unique hashes.  Once the hash table is full, each new
entry added to the table evicts an old entry.  This makes the hash table
a sliding window over the most recently scanned data from the filesystem.

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
obtained, and the extra space wastes RAM.

 * If the hash table is too small, bees extrapolates from matching
blocks to find matching adjacent blocks in the filesystem that have been
evicted from the hash table.  In other words, bees only needs to find
one block in common between two extents in order to be able to dedupe
the entire extents.  This provides significantly more dedupe hit rate
per hash table byte than other dedupe tools.

 * There is a fairly wide range of usable hash sizes, and performances
degrades according to a smooth probabilistic curve in both directions.
Double or half the optimium size usually works just as well.

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

* **Compression** in files reduces the average extent length compared
to uncompressed files.  The maximum compressed extent length on
btrfs is 128KB, while the maximum uncompressed extent length is 128MB.
Longer extents decrease the optimum hash table size while shorter extents
increase the optimum hash table size, because the probability of a hash
table entry being present (i.e. unevicted) in each extent is proportional
to the extent length.

   As a rule of thumb, the optimal hash table size for a compressed
filesystem is 2-4x larger than the optimal hash table size for the same
data on an uncompressed filesystem.  Dedupe efficiency falls rapidly with
hash tables smaller than 128MB/TB as the average dedupe extent size is
larger than the largest possible compressed extent size (128KB).

* **Short writes or fragmentation** also shorten the average extent
length and increase optimum hash table size.  If a database writes to
files randomly using 4K page writes, all of these extents will be 4K
in length, and the hash table size must be increased to retain each one
(or the user must accept a lower dedupe hit rate).

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

Scanning modes
--------------

The `--scan-mode` option affects how bees iterates over the filesystem,
schedules extents for scanning, and tracks progress.

There are now two kinds of scan mode:  the legacy **subvol** scan modes,
and the new **extent** scan mode.

Scan mode can be changed by restarting bees with a different scan mode
option.

Extent scan mode:

 * Works with 4.15 and later kernels.
 * Can estimate progress and provide an ETA.
 * Can optimize scanning order to dedupe large extents first.
 * Can keep up with frequent creation and deletion of snapshots.

Subvol scan modes:

 * Work with 4.14 and earlier kernels.
 * Cannot estimate or report progress.
 * Cannot optimize scanning order by extent size.
 * Have problems keeping up with multiple snapshots created during a scan.

The default scan mode is 4, "extent".

If you are using bees for the first time on a filesystem with many
existing snapshots, you should read about [snapshot gotchas](gotchas.md).

Subvol scan modes
-----------------

Subvol scan modes are maintained for compatibility with existing
installations, but will not be developed further.  New installations
should use extent scan mode instead.

The _quantity_ of text below detailing the shortcomings of each subvol
scan mode should be informative all by itself.

Subvol scan modes work on any kernel version supported by bees.  They
are the only scan modes usable on kernel 4.14 and earlier.

The difference between the subvol scan modes is the order in which the
files from different subvols are fed into the scanner.  They all scan
files in inode number order, from low to high offset within each inode,
the same way that a program like `cat` would read files (but skipping
over old data from earlier btrfs transactions).

If a filesystem has only one subvolume with data in it, then all of
the subvol scan modes are equivalent.  In this case, there is only one
subvolume to scan, so every possible ordering of subvols is the same.

The `--workaround-btrfs-send` option pauses scanning subvols that are
read-only.  If the subvol is made read-write (e.g. with `btrfs prop set
$subvol ro false`), or if the `--workaround-btrfs-send` option is removed,
then the scan of that subvol is unpaused and dedupe proceeds normally.
Space will only be recovered when the last read-only subvol is deleted.

Subvol scan modes cannot efficiently or accurately calculate an ETA for
completion or estimate progress through the data.  They simply request
"the next new inode" from btrfs, and they are completed when btrfs says
there is no next new inode.

Between subvols, there are several scheduling algorithms with different
trade-offs:

Scan mode 0, "lockstep", scans the same inode number in each subvol at
close to the same time.  This is useful if the subvols are snapshots
with a common ancestor, since the same inode number in each subvol will
have similar or identical contents.  This maximizes the likelihood that
all of the references to a snapshot of a file are scanned at close to
the same time, improving dedupe hit rate.  If the subvols are unrelated
(i.e. not snapshots of a single subvol) then this mode does not provide
any significant advantage.  This mode uses smaller amounts of temporary
space for shorter periods of time when most subvols are snapshots.  When a
new snapshot is created, this mode will stop scanning other subvols and
scan the new snapshot until the same inode number is reached in each
subvol, which will effectively stop dedupe temporarily as this data has
already been scanned and deduped in the other snapshots.

Scan mode 1, "independent", scans the next inode with new data in
each subvol.  There is no coordination between the subvols, other than
round-robin distribution of files from each subvol to each worker thread.
This mode makes continuous forward progress in all subvols.  When a new
snapshot is created, previous subvol scans continue as before, but the
worker threads are now divided among one more subvol.

Scan mode 2, "sequential", scans one subvol at a time, in numerical subvol
ID order, processing each subvol completely before proceeding to the next
subvol.  This avoids spending time scanning short-lived snapshots that
will be deleted before they can be fully deduped (e.g. those used for
`btrfs send`).  Scanning starts on older subvols that are more likely
to be origin subvols for future snapshots, eliminating the need to
dedupe future snapshots separately.  This mode uses the largest amount
of temporary space for the longest time, and typically requires a larger
hash table to maintain dedupe hit rate.

Scan mode 3, "recent", scans the subvols with the highest `min_transid`
value first (i.e. the ones that were most recently completely scanned),
then falls back to "independent" mode to break ties.  This interrupts
long scans of old subvols to give a rapid dedupe response to new data
in previously scanned subvols, then returns to the old subvols after
the new data is scanned.

Extent scan mode
----------------

Scan mode 4, "extent", scans the extent tree instead of the subvol trees.
Extent scan mode reads each extent once, regardless of the number of
reflinks or snapshots.  It adapts to the creation of new snapshots
and reflinks immediately, without having to revisit old data.

In the extent scan mode, extents are separated into multiple size tiers
to prioritize large extents over small ones.  Deduping large extents
keeps the metadata update cost low per block saved, resulting in faster
dedupe at the start of a scan cycle.  This is important for maximizing
performance in use cases where bees runs for a limited time, such as
during an overnight maintenance window.

Once the larger size tiers are completed, dedupe space recovery speeds
slow down significantly.  It may be desirable to stop bees running once
the larger size tiers are finished, then start bees running some time
later after new data has appeared.

Each extent is mapped in physical address order, and all extent references
are submitted to the scanner at the same time, resulting in much better
cache behavior and dedupe performance compared to the subvol scan modes.

The "extent" scan mode is not usable on kernels before 4.15 because
it relies on the `LOGICAL_INO_V2` ioctl added in that kernel release.
When using bees with an older kernel, only subvol scan modes will work.

Extents are divided into virtual subvols by size, using reserved btrfs
subvol IDs 250..255.  The size tier groups are:
 * 250: 32M+1 and larger
 * 251: 8M+1..32M
 * 252: 2M+1..8M
 * 253: 512K+1..2M
 * 254: 128K+1..512K
 * 255: 128K and smaller (includes all compressed extents)

Extent scan mode can efficiently calculate dedupe progress within
the filesystem and estimate an ETA for completion within each size
tier; however, the accuracy of the ETA can be questionable due to the
non-uniform distribution of block addresses in a typical user filesystem.

Older versions of bees do not recognize the virtual subvols, so running
an old bees version after running a new bees version will reset the
"extent" scan mode's progress in `beescrawl.dat` to the beginning.
This may change in future bees releases, i.e. extent scans will store
their checkpoint data somewhere else.

The `--workaround-btrfs-send` option behaves differently in extent
scan modes:  In extent scan mode, dedupe proceeds on all subvols that are
read-write, but all subvols that are read-only are excluded from dedupe.
Space will only be recovered when the last read-only subvol is deleted.

During `btrfs send` all duplicate extents in the sent subvol will not be
removed (the kernel will reject dedupe commands while send is active,
and bees currently will not re-issue them after the send is complete).
It may be preferable to terminate the bees process while running `btrfs
send` in extent scan mode, and restart bees after the `send` is complete.

Threads and load management
---------------------------

By default, bees creates one worker thread for each CPU detected.  These
threads then perform scanning and dedupe operations.  bees attempts to
maximize the amount of productive work each thread does, until either the
threads are all continuously busy, or there is no remaining work to do.

In many cases it is not desirable to continually run bees at maximum
performance.  Maximum performance is not necessary if bees can dedupe
new data faster than it appears on the filesystem.  If it only takes
bees 10 minutes per day to dedupe all new data on a filesystem, then
bees doesn't need to run for more than 10 minutes per day.

bees supports a number of options for reducing system load:

 * Run bees for a few hours per day, at an off-peak time (i.e. during
 a maintenace window), instead of running bees continuously.  Any data
 added to the filesystem while bees is not running will be scanned when
 bees restarts.  At the end of the maintenance window, terminate the
 bees process with SIGTERM to write the hash table and scan position
 for the next maintenance window.

 * Temporarily pause bees operation by sending the bees process SIGUSR1,
 and resume operation with SIGUSR2.  This is preferable to freezing
 and thawing the process, e.g. with freezer cgroups or SIGSTOP/SIGCONT
 signals, because it allows bees to close open file handles that would
 otherwise prevent those files from being deleted while bees is frozen.

 * Reduce the number of worker threads with the [`--thread-count` or
`--thread-factor` options](options.md).  This simply leaves CPU cores
 idle so that other applications on the host can use them, or to save
 power.

 * Allow bees to automatically track system load and increase or decrease
 the number of threads to reach a target system load.  This reduces
 impact on the rest of the system by pausing bees when other CPU and IO
 intensive loads are active on the system, and resumes bees when the other
 loads are inactive.  This is configured with the [`--loadavg-target`
 and `--thread-min` options](options.md).

 * Allow bees to self-throttle operations that enqueue delayed work
 within btrfs.  These operations are not well controlled by Linux
 features such as process priority or IO priority or IO rate-limiting,
 because the enqueued work is submitted to btrfs several seconds before
 btrfs performs the work.  By the time btrfs performs the work, it's too
 late for external throttling to be effective.  The [`--throttle-factor`
 option](options.md) tracks how long it takes btrfs to complete queued
 operations, and reduces bees's queued work submission rate to match
 btrfs's queued work completion rate (or a fraction thereof, to reduce
 system load).

Log verbosity
-------------

bees can be made less chatty with the [`--verbose` option](options.md).
