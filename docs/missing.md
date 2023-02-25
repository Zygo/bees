Features You Might Expect That bees Doesn't Have
------------------------------------------------

* There's no configuration file (patches welcome!).  There are
some tunables hardcoded in the source (`src/bees.h`) that could eventually
become configuration options.  There's also an incomplete option parser
(patches welcome!).

* The bees process doesn't fork and writes its log to stdout/stderr.
A shell wrapper is required to make it behave more like a daemon.

* There's no facility to exclude any part of a filesystem or focus on
specific files (patches welcome).

* PREALLOC extents and extents containing blocks filled with zeros will
be replaced by holes.  There is no way to turn this off.

* The fundamental unit of deduplication is the extent _reference_, when
it should be the _extent_ itself.  This is an architectural limitation
that results in excess reads of extent data, even in the Extent scan mode.

* Block reads are currently more allocation- and CPU-intensive than they
should be, especially for filesystems on SSD where the IO overhead is
much smaller.  This is a problem for CPU-power-constrained environments
(e.g. laptops running from battery, or ARM devices with slow CPU).

* bees can currently fragment extents when required to remove duplicate
blocks, but has no defragmentation capability yet.  When possible, bees
will attempt to work with existing extent boundaries and choose the
largest fragments available, but it will not aggregate blocks together
from multiple extents to create larger ones.

* When bees fragments an extent, the copied data is compressed.  There
is currently no way (other than by modifying the source) to select a
compression method or not compress the data (patches welcome!).

* It is theoretically possible to resize the hash table without starting
over with a new full-filesystem scan; however, this feature has not been
implemented yet.

* btrfs maintains csums of data blocks which bees could use to improve
scan speeds, but bees doesn't use them yet.
