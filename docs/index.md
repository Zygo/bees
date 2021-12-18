BEES
====

Best-Effort Extent-Same, a btrfs deduplication agent.

About bees
----------

bees is a block-oriented userspace deduplication agent designed for large
btrfs filesystems.  It is an offline dedupe combined with an incremental
data scan capability to minimize time data spends on disk from write
to dedupe.

Strengths
---------

 * Space-efficient hash table and matching algorithms - can use as little as 1 GB hash table per 10 TB unique data (0.1GB/TB)
 * Daemon incrementally dedupes new data using btrfs tree search
 * Works with btrfs compression - dedupe any combination of compressed and uncompressed files
 * **NEW** [Works around `btrfs send` problems with dedupe and incremental parent snapshots](options.md)
 * Works around btrfs filesystem structure to free more disk space
 * Persistent hash table for rapid restart after shutdown
 * Whole-filesystem dedupe - including snapshots
 * Constant hash table size - no increased RAM usage if data set becomes larger
 * Works on live data - no scheduled downtime required
 * Automatic self-throttling based on system load

Weaknesses
----------

 * Whole-filesystem dedupe - has no include/exclude filters, does not accept file lists
 * Requires root privilege (or `CAP_SYS_ADMIN`)
 * First run may require temporary disk space for extent reorganization
 * [First run may increase metadata space usage if many snapshots exist](gotchas.md)
 * Constant hash table size - no decreased RAM usage if data set becomes smaller
 * btrfs only

Installation and Usage
----------------------

 * [Installation](install.md)
 * [Configuration](config.md)
 * [Running](running.md)
 * [Command Line Options](options.md)

Recommended Reading
-------------------

 * [bees Gotchas](gotchas.md)
 * [btrfs kernel bugs](btrfs-kernel.md) - especially DATA CORRUPTION WARNING
 * [bees vs. other btrfs features](btrfs-other.md)
 * [What to do when something goes wrong](wrong.md)

More Information
----------------

 * [How bees works](how-it-works.md)
 * [Missing bees features](missing.md)
 * [Event counter descriptions](event-counters.md)

Bug Reports and Contributions
-----------------------------

Email bug reports and patches to Zygo Blaxell <bees@furryterror.org>.

You can also use Github:

        https://github.com/Zygo/bees

Copyright & License
-------------------

Copyright 2015-2018 Zygo Blaxell <bees@furryterror.org>.

GPL (version 3 or later).
