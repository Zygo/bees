#pragma once

/// @file bees-fwd.h
/// Forward declarations for the primary bees daemon classes.
/// Include this header to break include cycles; include bees.h for full definitions.

class BeesBlockData;  ///< A single block of data with lazy hash and address resolution.
class BeesConfig;     ///< Parsed daemon configuration (INI hierarchy).
class BeesContext;    ///< Central coordinator: owns all subsystems and their lifetimes.
class BeesFileRange;  ///< A half-open byte range [begin, end) within a file, with optional open Fd.
class BeesRoots;      ///< Manages btrfs subvolume roots and drives the scan/crawl loop.
class BeesTempFile;   ///< Temporary file used for partial-extent deduplication staging.
