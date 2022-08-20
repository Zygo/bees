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

* btrfs-progs

  Needed at runtime by the service wrapper script.

* [Linux kernel version](btrfs-kernel.md) gets its own page.

* markdown for documentation

* util-linux version that provides `blkid` command for the helper
  script `scripts/beesd` to work

Installation
============

bees can be installed by following one these instructions:

Arch package
------------

bees is available for Arch Linux in the community repository. Install with:

`$ pacman -S bees`

or build a live version from git master using AUR:

`$ git clone https://aur.archlinux.org/bees-git.git && cd bees-git && makepkg -si`

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

Build from source with meson
-----------------------------

The quick steps to build and test:

```sh
$ cd /path/to/source/bees
$ meson builddir && cd builddir
$ meson compile
$ meson test
```

The build produces `src/bees` in the `builddir`.

It will also generate `scripts/beesd@.service` for systemd users. This
service makes use of a helper script `scripts/beesd` to boot the service.
Both of the latter use the filesystem UUID to mount the root subvolume
within a temporary runtime directory.

You can install these files to the configured prefix using `meson install`
or manually copy them as you like.

Depending on your Linux distribution, you may need to install some
dependencies first:

### Ubuntu 16.04 - 17.04:
```sh
$ apt -y install build-essential btrfs-tools markdown meson
```

### Ubuntu 18.10:
```sh
$ apt -y install build-essential btrfs-progs markdown meson
```

Build from source with make
---------------------------

Build with `make`. The build produces `bin/bees` which must be copied
to somewhere in `$PATH` on the target system respectively.

It will also generate `scripts/beesd@.service` for systemd users. This
service makes use of a helper script `scripts/beesd` to boot the service.
Both of the latter use the filesystem UUID to mount the root subvolume
within a temporary runtime directory.

### Ubuntu 16.04 - 17.04:
`$ apt -y install build-essential btrfs-tools markdown && make`

### Ubuntu 18.10:
`$ apt -y install build-essential btrfs-progs markdown && make`

Packaging
---------

See 'Dependencies' above. Package maintainers can pick ideas for building and
configuring the source package from the Gentoo ebuild:

<https://github.com/gentoo/gentoo/tree/master/sys-fs/bees>

When using the `make` build system, you can configure some build options by
creating a file `localconf` and adjust settings for your distribution
environment there. Please also review the Makefile for additional hints.
