Building bees
=============

Dependencies
------------

* C++17 compiler (tested with GCC 11 through 14)

  bees uses `-Wall -Wextra -Werror -O3` and no other compiler flags.
  Be careful when adding extra flags such as `-flto` or
  `-D_GLIBCXX_ASSERTIONS` when building bees for distribution; these
  interact with known GCC bugs that produce false-positive warnings,
  breaking the build or crashing at runtime.  Known-broken combinations:

  | GCC version | Flag(s) | Notes |
  |---|---|---|
  | 12 | `-flto=auto` | False-positive `-Wmaybe-uninitialized`; toolchain bug, not a bees bug |
  | 12 | `-D_GLIBCXX_ASSERTIONS` | Separate bug, possibly in libstdc++ |
  | 13 | — | All flags work correctly |
  | 14 | `-flto=auto` | Same `-Wmaybe-uninitialized` regression as GCC 12 |

  If your distribution's build system adds LTO or assertion flags by
  default, you may need to disable them for bees (e.g. with a `%define
  _lto_cflags %nil` spec override on RPM-based distros).

* btrfs-progs

  Needed at runtime by the service wrapper script.

* [Linux kernel version](btrfs-kernel.md) gets its own page.

* markdown to build the documentation

* util-linux version that provides `blkid` command for the helper
  script `scripts/beesd` to work

Installation
============

bees can be installed by following one of these instructions:

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

Build from source
-----------------

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

You can configure some build options by creating a file `localconf` and
adjust settings for your distribution environment there.

Please also review the Makefile for additional hints.
