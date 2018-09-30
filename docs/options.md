# bees Command Line Options

<table border>
<tr><th width="20%">--thread-count COUNT</th><th width="5%">-c</th>
<td>Specify maximum number of worker threads for scanning.  Overrides
--thread-factor (-C) and default/autodetected values.
</td></tr>
<tr><th>--thread-factor FACTOR</th><th>-C</th>
<td>Specify ratio of worker threads to CPU cores.  Overridden by --thread-count (-c).
    Default is 1.0, i.e. 1 worker thread per detected CPU.  Use values
    below 1.0 to leave some cores idle, or above 1.0 if there are more
    disks than CPUs in the filesystem.
</td></tr>

<tr><th>--loadavg-target LOADAVG</th><th>-g</th>
  <td>Specify load average target for dynamic worker threads.
    Threads will be started or stopped subject to the upper limit imposed
    by thread-factor, thread-min and thread-count until the load average
    is within +/- 0.5 of LOADAVG.
</td></tr>
<tr><th>--thread-min COUNT</th><th>-G</th>
<td>Specify minimum number of worker threads for scanning.
    Ignored unless -g option is used to specify a target load.</td></tr>

<tr><th>--scan-mode MODE</th><th>-m</th>
  <td>
Specify extent scanning algorithm.  Default mode is 0.
<em>EXPERIMENTAL</em> feature that may go away.
<ul>
<li> Mode 0: scan extents in ascending order of (inode, subvol, offset).
  Keeps shared extents between snapshots together.  Reads files sequentially.
Minimizes temporary space usage.</li>
<li> Mode 1: scan extents from all subvols in parallel.  Good performance
  on non-spinning media when subvols are unrelated.</li>
<li> Mode 2: scan all extents from one subvol at a time.  Good sequential
  read performance for spinning media.  Maximizes temporary space usage.</li>
</ul>
</td></tr>

<tr><th>--timestamps</th><th>-t</th>
  <td>Enable timestamps in log output.</td></tr>
<tr><th>--no-timestamps</th><th>-T</th>
  <td>Disable timestamps in log output.</td></tr>
<tr><th>--absolute-paths</th><th>-p</th>
  <td>Paths in log output will be absolute.</td></tr>
<tr><th>--strip-paths</th><th>-P</th>
  <td>Paths in log output will have the working directory at bees startup
    stripped.</td></tr>
<tr><th>--verbose</th><th>-v</th>
  <td>Set log verbosity (0 = no output, 8 = all output, default 8).</td></tr>

</table>
