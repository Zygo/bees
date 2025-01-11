What to do when something goes wrong with bees
==============================================

Hangs and excessive slowness
----------------------------

### Use load-throttling options

  If bees is just more aggressive than you would like, consider using
  [load throttling options](options.md).  These are usually more effective
  than `ionice`, `schedtool`, and the `blkio` cgroup (though you can
  certainly use those too) because they limit work that bees queues up
  for later execution inside btrfs.

### Check `$BEESSTATUS`

  If bees or the filesystem seems to be stuck, check the contents of
  `$BEESSTATUS`.  bees describes what it is doing (and how long it has
  been trying to do it) through this file.

  Sample:

<pre>
THREADS (work queue 68 tasks):
	tid 20939: crawl_5986: dedup BeesRangePair: 512K src[0x9933f000..0x993bf000] dst[0x9933f000..0x993bf000]
src = 147 /run/bees/ede84fbd-cb59-0c60-9ea7-376fa4984887/data/home/builder/linux/.git/objects/pack/pack-09f06f8759ac7fd163df320b7f7671f06ac2a747.pack
dst = 15 /run/bees/ede84fbd-cb59-0c60-9ea7-376fa4984887/data.new/home/builder/linux/.git/objects/pack/pack-09f06f8759ac7fd163df320b7f7671f06ac2a747.pack
	tid 20940: crawl_5986: dedup BeesRangePair: 512K src[0x992bf000..0x9933f000] dst[0x992bf000..0x9933f000]
src = 147 /run/bees/ede84fbd-cb59-0c60-9ea7-376fa4984887/data/home/builder/linux/.git/objects/pack/pack-09f06f8759ac7fd163df320b7f7671f06ac2a747.pack
dst = 15 /run/bees/ede84fbd-cb59-0c60-9ea7-376fa4984887/data.new/home/builder/linux/.git/objects/pack/pack-09f06f8759ac7fd163df320b7f7671f06ac2a747.pack
	tid 21177: crawl_5986: dedup BeesRangePair: 512K src[0x9923f000..0x992bf000] dst[0x9923f000..0x992bf000]
src = 147 /run/bees/ede84fbd-cb59-0c60-9ea7-376fa4984887/data/home/builder/linux/.git/objects/pack/pack-09f06f8759ac7fd163df320b7f7671f06ac2a747.pack
dst = 15 /run/bees/ede84fbd-cb59-0c60-9ea7-376fa4984887/data.new/home/builder/linux/.git/objects/pack/pack-09f06f8759ac7fd163df320b7f7671f06ac2a747.pack
	tid 21677: bees: [68493.1s] main
	tid 21689: crawl_transid: [236.508s] waiting 332.575s for next 10 transid RateEstimator { count = 87179, raw = 969.066 / 32229.2, ratio = 969.066 / 32465.7, rate = 0.0298489, duration(1) = 33.5021, seconds_for(1) = 1 }
	tid 21690: status: writing status to file '/run/bees.status'
	tid 21691: crawl_writeback: [203.456s] idle, dirty
	tid 21692: hash_writeback: [12.466s] flush rate limited after extent #17 of 64 extents
	tid 21693: hash_prefetch: [2896.61s] idle 3600s
</pre>

 The time in square brackets indicates how long the thread has been
 executing the current task (if this time is below 5 seconds then it
 is omitted).  We can see here that the main thread (and therefore the
 bees process as a whole) has been running for 68493.1 seconds, the
 last hash table write was 12.5 seconds ago, and the last transid poll
 was 236.5 seconds ago.  Three worker threads are currently performing
 dedupe on extents.

 Thread names of note:

 * `bees`: main thread (doesn't do anything after startup, but its task execution time is that of the whole bees process)
 * `crawl_master`: task that finds new extents in the filesystem and populates the work queue
 * `crawl_transid`: btrfs transid (generation number) tracker and polling thread
 * `status`: the thread that writes the status reports to `$BEESSTATUS`
 * `crawl_writeback`: writes the scanner progress to `beescrawl.dat`
 * `hash_writeback`: trickle-writes the hash table back to `beeshash.dat`
 * `hash_prefetch`: prefetches the hash table at startup and updates `beesstats.txt` hourly

Most other threads have names that are derived from the current dedupe
task that they are executing:

 * `ref_205ad76b1000_24K_50`:  extent scan performing dedupe of btrfs extent bytenr `205ad76b1000`, which is 24 KiB long and has 50 references
 * `extent_250_32M_16E`:  extent scan searching for extents between 32 MiB + 1 and 16 EiB bytes long, tracking scan position in virtual subvol `250`.
 * `crawl_378_18916`:  subvol scan searching for extent refs in subvol `378`, inode `18916`.

### Dump kernel stacks of hung processes

Check the kernel stacks of all blocked kernel processes:

	ps xar | while read -r x y; do ps "$x"; head -50 --verbose /proc/"$x"/task/*/stack; done | tee lockup-stacks.txt

Submit the above information in your bug report.

### Check dmesg for btrfs stack dumps

Sometimes these are relevant too.


bees Crashes
------------

 * If you have a core dump, run these commands in gdb and include
 the output in your report (you may need to post it as a compressed
 attachment, as it can be quite large):

        (gdb) set pagination off
        (gdb) info shared
        (gdb) bt
        (gdb) thread apply all bt
        (gdb) thread apply all bt full

  The last line generates megabytes of output and will often crash gdb.
  Submit whatever output gdb can produce.

  **Note that this output may include filenames or data from your
  filesystem.**

 * If you have `systemd-coredump` installed, you can use `coredumpctl`:

        (echo set pagination off;
        echo info shared;
        echo bt;
        echo thread apply all bt;
        echo thread apply all bt full) | coredumpctl gdb bees

 * If the crash happens often (or don't want to use coredumpctl),
   you can run automate the gdb data collection with this wrapper script:

<pre>
#!/bin/sh
set -x

# Move aside old core files for analysis
for x in core*; do
	if [ -e "$x" ]; then
		mv -vf "$x" "old-$x.$(date +%Y-%m-%d-%H-%M-%S)"
	fi
done

# Delete old core files after a week
find old-core* -type f -mtime +7 -exec rm -vf {} + &

# Turn on the cores (FIXME: may need to change other system parameters
# that capture or redirect core files)
ulimit -c unlimited

# Run the command
"$@"
rv="$?"

# Don't clobber our core when gdb crashes
ulimit -c 0

# If there were core files, generate reports for them
for x in core*; do
	if [ -e "$x" ]; then
		gdb --core="$x" \
		--eval-command='set pagination off' \
		--eval-command='info shared' \
		--eval-command='bt' \
		--eval-command='thread apply all bt' \
		--eval-command='thread apply all bt full' \
		--eval-command='quit' \
		--args "$@" 2>&1 | tee -a "$x.txt"
	fi
done

# Return process exit status to caller
exit "$rv"
</pre>

  To use the wrapper script, insert it just before the `bees` command,
  as in:

    gdb-wrapper bees /path/to/fs/


Kernel crashes, corruption, and filesystem damage
-------------------------------------------------

bees doesn't do anything that _should_ cause corruption or data loss;
however, [btrfs has kernel bugs](btrfs-kernel.md), so corruption is
not impossible.

Issues with the btrfs filesystem kernel code or other block device layers
should be reported to their respective maintainers.
