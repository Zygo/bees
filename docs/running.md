Running bees
============

Setup
-----

If you don't want to use the helper script `scripts/beesd` to setup and
configure bees, here's how you manually setup bees.

Create a directory for bees state files:

        export BEESHOME=/some/path
        mkdir -p "$BEESHOME"

Create an empty hash table ([your choice of size](config.md), but it
must be a multiple of 128KB).  This example creates a 1GB hash table:

        truncate -s 1g "$BEESHOME/beeshash.dat"
        chmod 700 "$BEESHOME/beeshash.dat"

bees can _only_ process the root subvol of a btrfs with nothing mounted
over top.  If the bees argument is not the root subvol directory, bees
will just throw an exception and stop.

Use a separate mount point, and let only bees access it:

        UUID=3399e413-695a-4b0b-9384-1b0ef8f6c4cd
        mkdir -p /var/lib/bees/$UUID
        mount /dev/disk/by-uuid/$UUID /var/lib/bees/$UUID -osubvol=/

If you don't set BEESHOME, the path "`.beeshome`" will be used relative
to the root subvol of the filesystem.  For example:

        btrfs sub create /var/lib/bees/$UUID/.beeshome
        truncate -s 1g /var/lib/bees/$UUID/.beeshome/beeshash.dat
        chmod 700 /var/lib/bees/$UUID/.beeshome/beeshash.dat

You can use any relative path in `BEESHOME`.  The path will be taken
relative to the root of the deduped filesystem (in other words it can
be the name of a subvol):

        export BEESHOME=@my-beeshome
        btrfs sub create /var/lib/bees/$UUID/$BEESHOME
        truncate -s 1g /var/lib/bees/$UUID/$BEESHOME/beeshash.dat
        chmod 700 /var/lib/bees/$UUID/$BEESHOME/beeshash.dat

Configuration
-------------

There are some runtime configurable options using environment variables:

* BEESHOME: Directory containing bees state files:
	* beeshash.dat  | persistent hash table.  Must be a multiple of 128KB, and must be created before bees starts.
	* beescrawl.dat | state of SEARCH_V2 crawlers.  ASCII text.  bees will create this.
	* beesstats.txt | statistics and performance counters.  ASCII text.  bees will create this.
* BEESSTATUS: File containing a snapshot of current bees state:  performance
  counters and current status of each thread.  The file is meant to be
  human readable, but understanding it probably requires reading the source.
  You can watch bees run in realtime with a command like:

        watch -n1 cat $BEESSTATUS

Other options (e.g. interval between filesystem crawls) can be configured
in `src/bees.h` or [on the command line](options.md).

Running
-------

Reduce CPU and IO priority to be kinder to other applications sharing
this host (or raise them for more aggressive disk space recovery).  If you
use cgroups, put `bees` in its own cgroup, then reduce the `blkio.weight`
and `cpu.shares` parameters.  You can also use `schedtool` and `ionice`
in the shell script that launches `bees`:

        schedtool -D -n20 $$
        ionice -c3 -p $$

You can also use the [load management options](options.md) to further
control the impact of bees on the rest of the system.

Let the bees fly:

        for fs in /var/lib/bees/*-*-*-*-*/; do
                bees "$fs" >> "$fs/.beeshome/bees.log" 2>&1 &
        done

You'll probably want to arrange for `/var/log/bees.log` to be rotated
periodically.  You may also want to set umask to 077 to prevent disclosure
of information about the contents of the filesystem through the log file.

There are also some shell wrappers in the `scripts/` directory.
