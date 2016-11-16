BEES
====

Best-Effort Extent-Same, a btrfs deduplicator.

TODO
----

Write some docs here:

* copyright (Zygo Blaxell 2015-2016), license (GPL3+)
* what it is
* what it isn't
* building it
* what works
* what doesn't work
* a brief history of btrfs kernel bugs
* things that could have been, and why they aren't
* roadmap (and anti-roadmap)
* how to report bugs
* how to contribute

Build
-----

Requirements:
	* C++11 compiler (I use GCC 4.9)
	* btrfs-progs (I've used 4.1..4.7) for /usr/include/btrfs/*
	* libuuid-dev (TODO: remove the one function we call from this library)

Build with `make`.

The build produces `bin/bees` and `lib/libcrucible.so`, which must be
copied to somewhere in `$PATH` and `$LD_LIBRARY_PATH` on the target
system respectively.

Setup
-----

Create a directory for bees state files:

	export BEESHOME=/some/path
	mkdir -p "$BEESHOME"

Create an empty hash table (your choice of size, but it must be a multiple
of 16M).  This example creates a 1GB hash table:

	truncate -s 1g "$BEESHOME/beeshash.dat"
	chmod 700 "$BEESHOME/beeshash.dat"

Configuration
-------------

The only runtime configurable options are environment variables:

* BEESHOME: Directory containing Bees state files:
 * beeshash.dat         | persistent hash table (must be a multiple of 16M)
 * beescrawl.`UUID`.dat | state of SEARCH_V2 crawlers
 * beesstats.txt        | statistics and performance counters
* BEESSTATS: File containing a snapshot of current Bees state (performance
  counters and current status of each thread).

Other options (e.g. interval between filesystem crawls) can be configured
in src/bees.h.

Running
-------

We created this directory in the previous section.

	export BEESHOME=/some/path

Use a tmpfs for BEESSTATUS, it updates once per second

	export BEESSTATUS=/run/bees.status

bees can only process the root subvol of a btrfs.
Use a bind mount, and let only bees access it.

	mount -osubvol=/ /dev/<your-filesystem> /var/lib/bees/root

Let the bees fly!

	bees /var/lib/bees/root >> /var/log/bees.log 2>&1

You'll probably want to arrange for /var/log/bees.log to be rotated
periodically.  You may also want to set umask to 077 to prevent disclosure
of information about the contents of the filesystem through the log file.
