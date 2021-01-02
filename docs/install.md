Building bees
=============

Dependencies
------------

* C++11 compiler (tested with GCC 4.9, 6.3.0, 8.1.0)

  Sorry.  I really like closures and shared_ptr, so support
  for earlier compiler versions is unlikely.

  Note that the C++ standard--and GCC's implementation of it--is evolving.
  There may be problems when building with newer compiler versions.
  Build failure reports welcome!

* btrfs-progs (tested with 4.1..4.20.2) or libbtrfs-dev
  (tested with version 4.20.2)

  Needed for btrfs.h and ctree.h during compile.
  Also needed by the service wrapper script.

* libuuid-dev

  This library is only required for a feature that was removed after v0.1.
  The lingering support code can be removed.

* [Linux kernel version](btrfs-kernel.md) gets its own page.

* markdown for documentation

* util-linux version that provides `blkid` command for the helper
  script `scripts/beesd` to work

Installation
============

bees can be installed by following one these instructions:

Arch package
------------

bees is available in Arch Linux AUR. Install with:

`$ pacaur -S bees-git`

Gentoo package
--------------

bees is officially available in Gentoo Portage. Just emerge a stable
version:

`$ emerge --ask bees`

or build a live version from git master:

`$ emerge --ask =bees-9999`

You can opt-out of building the support tools with

`USE="-tools" emerge ...`

If you want to start hacking on bees and contribute changes, just emerge
the live version which automatically pulls in all required development
packages.

Build from source
-----------------

Build with `make`. The build produces `bin/bees` which must be copied
to somewhere in `$PATH` on the target system respectively.

It will also generate `scripts/beesd@.service` for systemd users. This
service makes use of a helper script `scripts/beesd` to boot the service.
Both of the latter use the filesystem UUID to mount the root subvolume
within a temporary runtime directory.

### Ubuntu 16.04 - 17.04:
`$ apt -y install build-essential btrfs-tools uuid-dev markdown && make`

### Ubuntu 18.10:
`$ apt -y install build-essential btrfs-progs libbtrfs-dev uuid-dev markdown && make`

### Ubuntu 14.04:
You can try to carry on the work done here: <https://gist.github.com/dagelf/99ee07f5638b346adb8c058ab3d57492>

### openSUSE Tumbleweed:
> If you're running transactional server, first chroot into snapshot by issuing `$ transactional-update shell`. It's also recommended that you don't put bees executables into read-write area if you did.

    $ zypper install libbtrfs-devel uuid-devel
    $ zypper install -t pattern devel_C_C++
    $ make

By default `./scripts/beesd.in` isn't compatible with openSUSE. Fix it by editing:

    readonly CONFIG_DIR="# Path to configurations directory; Use absolue path #"
    readonly bees_bin="# Path to bees executable; Use absolue path #"

Packaging
---------

See 'Dependencies' below. Package maintainers can pick ideas for building and
configuring the source package from the Gentoo ebuild:

<https://github.com/gentoo/gentoo/tree/master/sys-fs/bees>

You can configure some build options by creating a file `localconf` and
adjust settings for your distribution environment there.

Please also review the Makefile for additional hints.
