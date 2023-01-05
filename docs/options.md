# bees Command Line Options

## Load management options

* `--thread-count COUNT` or `-c`

 Specify maximum number of worker threads.  Overrides `--thread-factor`
 (`-C`), default/autodetected values, and the hardcoded thread limit.

* `--thread-factor FACTOR` or `-C`

 Specify ratio of worker threads to detected CPU cores.  Overridden by
 `--thread-count` (`-c`).

 Default is 1.0, i.e. 1 worker thread per detected CPU.  Use values
 below 1.0 to leave some cores idle, or above 1.0 if there are more
 disks than CPUs in the filesystem.

* `--loadavg-target LOADAVG` or `-g`

 Specify load average target for dynamic worker threads.  Default is
 to run the maximum number of worker threads all the time.

 Worker threads will be started or stopped subject to the upper limit
 imposed by `--thread-factor`, `--thread-min` and `--thread-count`
 until the load average is within +/- 0.5 of `LOADAVG`.

* `--thread-min COUNT` or `-G`

 Specify minimum number of dynamic worker threads.  This can be used
 to force a minimum number of threads to continue running while using
 `--loadavg-target` to manage load.

 Default is 0, i.e. all bees worker threads will stop when the system
 load exceeds the target.

 Has no effect unless `--loadavg-target` is used to specify a target load.

## Filesystem tree traversal options

* `--scan-mode MODE` or `-m`

 Specify extent scanning algorithm.
 **EXPERIMENTAL** feature that may go away.

  * Mode 0: lockstep
  * Mode 1: independent
  * Mode 2: sequential
  * Mode 3: recent
  * Mode 4: extent

 For details of the different scanning modes and the default value of
 this option, see [bees configuration](config.md).

## Workarounds

* `--workaround-btrfs-send` or `-a`

 Pretend that read-only snapshots are empty and silently discard any
request to dedupe files referenced through them.  This is a workaround for
[problems with the kernel implementation of `btrfs send` and `btrfs send
-p`](btrfs-kernel.md) which make these btrfs features unusable with bees.

 This option should be used to avoid breaking `btrfs send` on the same
filesystem.

 **Note:** There is a _significant_ space tradeoff when using this option:
it is likely no space will be recovered--and possibly significant extra
space used--until the read-only snapshots are deleted.  On the other
hand, if snapshots are rotated frequently then bees will spend less time
scanning them.

## Logging options

* `--timestamps` or `-t`

 Enable timestamps in log output.

* `--no-timestamps` or `-T`

 Disable timestamps in log output.

* `--absolute-paths` or `-p`

 Paths in log output will be absolute.

* `--strip-paths` or `-P`

 Paths in log output will have the working directory at bees startup stripped.

* `--verbose` or `-v`

 Set log verbosity (0 = no output, 8 = all output, default 8).
