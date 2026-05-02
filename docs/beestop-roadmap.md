beestop Feature Roadmap
=======================

Goal
----

Add an optional command-line tool, tentatively named `beestop`, that gives
administrators a live view of bees activity in the style of `top`, `htop`,
or `btop`.

The tool should make it easier to answer the first operational question:

        Is bees doing useful work right now?

Agent Quick Start
-----------------

For an AI agent implementing this feature:

 1. Implement only one phase per task.
 2. The initial MVP is Phases 1 through 4 only.
 3. Do not touch dedupe logic.
 4. Do not touch bees daemon internals before Phase 5.
 5. Do not change default build, install, service, or runtime behavior.
 6. Do not introduce breaking changes or feature gates.
 7. The only intended gate is whether the optional `beestop` tool is built
    or installed.
 8. Stop for human review before adding dependencies, protocols, daemon
    outputs, install behavior, or state-changing controls.
 9. Finish each task with changed files, tests run, tests not run, and an
    explicit statement about dedupe logic.

This roadmap intentionally avoids changes to dedupe behavior.  The first
versions should read information that bees already publishes, especially
`$BEESSTATUS` and `beesstats.txt`.  Any later instrumentation inside bees
must be added as observation-only reporting code and must not alter matching,
rewriting, locking, or dedupe decisions.

Motivation
----------

The existing log levels do not give a comfortable middle ground for routine
operation.  High verbosity can flood the operator with scary-looking debug
messages that are often harmless, while low verbosity can make a healthy
daemon look idle.  Watching `$BEESSTATUS` live is already useful, but it is a
raw implementation-facing snapshot.

`beestop` should turn the existing status stream into a compact operational
view:

 * high-level counters and rates at the top,
 * current worker and queue state,
 * scan progress and ETA data,
 * recently visible activity, initially derived from status lines and process
   file descriptors where possible.

Non-Goals
---------

 * Do not change dedupe selection, hash table behavior, extent comparison,
   rewrite behavior, or `FIDEDUPERANGE` calls.
 * Do not require bees to run with verbose logging.
 * Do not add a state-mutating control protocol in the first implementation.
 * Do not make `beestop` part of the default installed daemon path until the
   interface is useful enough to support.
 * Do not promise exact per-file dedupe attribution before bees has a stable
   event source for that data.
 * Do not add feature gates other than the optional build and install path for
   `beestop`.
 * Do not make breaking changes to existing command-line options, environment
   variables, status files, logs, build targets, install targets, services, or
   runtime behavior.

Safety Boundary
---------------

The first implementation should be an external reader:

 * parse `$BEESSTATUS`,
 * optionally parse `$BEESHOME/beesstats.txt`,
 * optionally inspect `/proc/$pid/fd` as a best-effort hint,
 * render a terminal UI,
 * exit without leaving state behind.

Changes inside bees should be deferred until the external reader exposes the
limits of the current status format.  When internal changes become necessary,
they should be limited to a new reporting surface such as a status snapshot,
event file, pipe, or socket.  The reporting surface must copy already-known
facts out of the running daemon and must not feed back into the dedupe
pipeline.

MVP Scope
---------

The first implementation should stop after Phase 4.  At that point `beestop`
should be a useful read-only tool built from existing data sources.  Phases 5
and 6 are future work and require separate human approval because they may add
new daemon-side observation surfaces.

AI-Assisted Development Rules
-----------------------------

This feature is intended to be implemented with help from AI coding agents,
but development remains human reviewed.  Agents should treat this document as
the controlling plan and should keep changes small enough for a human reviewer
to understand in one sitting.

Before changing code, an agent must:

 * read this roadmap,
 * inspect the existing Makefiles and status-reporting code,
 * identify the smallest phase it is implementing,
 * state which files it expects to touch,
 * avoid unrelated cleanup, formatting, or refactoring.

Agents must not:

 * modify dedupe logic, hash table lookup/update behavior, extent matching,
   extent rewriting, resolver behavior, blacklist behavior, scan ordering, or
   `FIDEDUPERANGE` calls,
 * change daemon runtime defaults,
 * change default `make`, `make all`, `make install`, or service behavior while
   implementing the first read-only tool phases,
 * add a command channel that can pause, resume, rescan, delete, rewrite, or
   otherwise mutate daemon state,
 * replace existing status output without preserving compatibility,
 * add feature gates beyond optional `beestop` build/install targets,
 * introduce breaking changes to existing command-line options, environment
   variables, status files, logs, build targets, install targets, services, or
   runtime behavior,
 * introduce a broad dependency without a human-approved rationale,
 * hide test failures, skip relevant tests silently, or commit generated files
   that are not normally tracked by the project.

Agents should prefer:

 * read-only parsing and display over daemon changes,
 * fixtures and deterministic tests over tests requiring a live btrfs
   filesystem,
 * feature detection over strict parsing when reading current human-readable
   status files,
 * plain data structures with clear ownership over clever abstractions,
 * explicit error messages for missing files, permissions, and malformed input.

Human review gates:

 * Moving from Phase 0 to Phase 1 requires agreement on the implementation
   language and file layout.
 * Moving from Phase 4 to Phase 5 requires human approval because it touches
   bees daemon internals.
 * Any dependency, protocol, daemon output format, or install behavior change
   requires human approval before implementation.
 * Any patch that touches dedupe-related files must explain why the change is
   observation-only and must be reviewed separately from UI work.
 * Before merge, the complete implementation requires a final AI-assisted code
   review and a separate human-directed history review.

Build and Packaging
-------------------

`beestop` should be optional.

Suggested build shape:

 * keep the default `make`, `make all`, and `make install` behavior unchanged
   at first,
 * add an explicit target such as `make beestop` or `make tools`,
 * build the binary into `bin/beestop`,
 * install it only through an explicit target such as `make install_beestop`
   or a documented packaging option.

This keeps the operational daemon build stable while allowing packagers and
developers to opt in.

Compatibility rule:

 * the only gate introduced by this feature should be whether `beestop` is
   built or installed,
 * existing bees behavior should remain available without new flags,
   configuration switches, or compatibility modes,
 * existing scripts that build, install, configure, or run bees should continue
   to work unchanged.

Implementation Phases
---------------------

### Phase 0: Roadmap and Constraints

Document the intended direction, safety boundary, and first milestones.

Acceptance criteria:

 * roadmap is written in English,
 * no source behavior changes,
 * no dedupe code touched,
 * AI-agent guardrails and human-review gates are explicit.

Suggested commit:

        docs: add beestop feature roadmap

Human review gate:

 * confirm that the roadmap is acceptable before implementation starts.

### Phase 1: Read-Only Status Parser

Add a small parser for the existing `$BEESSTATUS` format.  The parser should
extract the sections bees already writes:

 * `TOTAL`,
 * `RATES`,
 * `THREADS`,
 * `PROGRESS`.

The parser should tolerate missing sections and unknown lines.  `$BEESSTATUS`
is human-readable and not yet a stable machine protocol, so the first parser
must degrade gracefully.

Acceptance criteria:

 * unit tests cover representative status samples,
 * malformed or partial files do not crash the parser,
 * no bees daemon source files need to change.

Suggested commit:

        beestop: parse existing status snapshots

Agent guardrails:

 * do not change `BeesContext::dump_status`,
 * do not require a stable schema from the current human-readable status file,
 * keep parser behavior documented by fixtures.

### Phase 2: Minimal Terminal View

Build a simple terminal UI around the parser.  A first version can redraw the
screen periodically without depending on a heavy UI framework.

Initial view:

 * header with refresh time and status file path,
 * queue size, worker count, current load, target load, and load average,
 * selected rates and counters,
 * progress table,
 * thread list sorted into a stable and readable order.

Acceptance criteria:

 * `beestop --status /path/to/status` works without guessing,
 * `beestop` can use `$BEESSTATUS` when the option is omitted,
 * refresh interval is configurable,
 * the screen fits on a typical 80x24 terminal by hiding lower-priority rows.

Suggested commit:

        beestop: add minimal live terminal display

Agent guardrails:

 * keep the first UI dependency-free unless a human approves a dependency,
 * keep terminal control simple enough to test with `--once`,
 * do not require root privileges unless the selected input file requires them.

### Phase 3: Optional Build Targets

Integrate the new tool with the build without making it mandatory.

Acceptance criteria:

 * `make beestop` builds `bin/beestop`,
 * default build targets remain unchanged unless the project later chooses to
   include tools by default,
 * generated dependency files and clean rules match the existing Makefile
   style,
 * documentation describes how to build and run the tool.

Suggested commit:

        build: add optional beestop target

Agent guardrails:

 * do not add `beestop` to default install targets in this phase,
 * do not make the daemon depend on the tool,
 * do not break existing `make`, `make all`, `make test`, or `make install`
   behavior.

### Phase 4: Operator-Focused Polish

Improve the display using only existing data sources.

Possible additions:

 * highlight active dedupe, scan, crawl, writeback, and idle threads,
 * show counter deltas between refreshes,
 * preserve the last good snapshot if bees is between atomic status file
   renames,
 * add `--once` for scripts and tests,
 * add plain text output for non-interactive terminals.

Acceptance criteria:

 * behavior is covered by parser and formatter tests,
 * `--once` output is deterministic enough for snapshot-style tests,
 * the UI remains useful when only part of the status file is present.

Suggested commit:

        beestop: improve live status presentation

Human review gate:

 * confirm whether the external read-only tool is useful enough before adding
   any new bees daemon reporting surface.

### Phase 5: Future Structured Observation Surface

Only after the external tool proves useful, consider adding a structured
read-only observation surface to bees.

Candidate formats:

 * a BEESSTATUS-like text file with extra sections,
 * JSON snapshots written atomically beside `$BEESSTATUS`,
 * a Unix socket that allows clients to subscribe to status events,
 * a pipe-friendly event stream for SSH tunneling and remote dashboards.

Start with subscription-only observation.  Commands that mutate daemon state,
such as pause, resume, or targeted scans, should be designed separately after
the read-only protocol is stable.

Acceptance criteria:

 * reporting code is isolated from dedupe decisions,
 * the new format has versioning or feature detection,
 * the existing `$BEESSTATUS` behavior remains compatible,
 * tests verify serialization without requiring a live btrfs dedupe run.

Suggested commits:

        bees: add read-only status snapshot writer
        beestop: read structured status snapshots

Agent guardrails:

 * do not touch dedupe decisions or scheduling,
 * write snapshots atomically like `$BEESSTATUS` where practical,
 * keep the old `$BEESSTATUS` output working,
 * make the new format explicitly read-only and versioned.

### Phase 6: Future Recent File Activity

Add a better "recent files" view when bees exposes enough information to make
it meaningful.

The `/proc/$pid/fd` list may be useful as a fallback, but it is only a rough
proxy.  It does not reliably say whether a file is open for scan, copy, or
dedupe, and it can look like an unweighted random sample of the filesystem.

A better future data source would report recent file activity with counters by
access type:

 * scan,
 * copy,
 * dedupe,
 * resolve,
 * temporary file work.

Acceptance criteria:

 * file activity is clearly marked as approximate unless backed by structured
   bees events,
 * deleted or unavailable file names are handled explicitly,
 * the UI does not imply exact dedupe attribution when the source data cannot
   support that claim.

Suggested commits:

        bees: report recent read-only file activity
        beestop: show recent file activity

Agent guardrails:

 * do not keep extra file descriptors open only for reporting,
 * do not retain unbounded path history,
 * do not report guessed access type as fact,
 * do not expose paths through a new default output without documenting the
   privacy implications.

Testing Strategy
----------------

Keep tests small and independent of a live btrfs filesystem wherever possible.

Test layers:

 * parser tests using committed status fixtures,
 * formatter tests for narrow and wide terminal sizes,
 * command-line tests for `--status`, `--interval`, `--once`, and missing
   files,
 * build tests for the optional target,
 * later serialization tests for any structured daemon output.

Manual testing should include:

 * running against a real `$BEESSTATUS`,
 * running while bees is stopped,
 * running during status file rename windows,
 * running without a TTY,
 * running as a non-root user that can read the status file.

Commit Plan
-----------

Keep commits as atomic and reviewable as practical during development:

 1. documentation-only roadmap,
 2. parser and fixtures,
 3. minimal display,
 4. optional build target,
 5. documentation for users,
 6. polish and non-interactive output,
 7. structured read-only daemon reporting, if needed,
 8. recent file activity reporting, if needed.

During active development, some commits may be exploratory, may contain bugs
fixed by later commits, or may reflect direction changes from review feedback.
That is acceptable while the branch is still under review.  Before merge, the
history should be rewritten into a coherent sequence of meaningful commits.

The final history should have these properties:

 * each commit has one clear behavioral purpose,
 * each commit builds and passes its relevant tests on its own,
 * commits are ordered from foundations to user-visible behavior,
 * fixup commits are squashed into the commits that introduced the issue,
 * abandoned approaches and review churn are removed,
 * documentation changes are placed with the behavior they describe unless a
   separate documentation-only commit is clearer,
 * any commit touching bees internals explains why the change is
   observation-only.

The history rewrite must be human directed.  AI assistance may propose a commit
stack, split points, commit messages, and verification commands, but a human
reviewer decides the final shape.

Final Review Phase
------------------

After implementation appears complete, perform a final AI-assisted code review
before merge.  This review should treat the entire branch as new code, not only
the last incremental patch.

The final AI review should check:

 * no dedupe behavior changed accidentally,
 * all daemon-facing changes are observation-only,
 * parser behavior is robust against partial, missing, and malformed input,
 * terminal output works in narrow terminals, non-interactive mode, and missing
   input cases,
 * build targets are optional as intended,
 * default daemon build, install, and runtime behavior remain compatible,
 * tests cover parser, formatter, command-line, and build behavior at the
   appropriate level,
 * user-visible errors are explicit and actionable,
 * path disclosure and permission behavior are documented where relevant.

The final AI review report should list:

 * findings ordered by severity,
 * false positives or assumptions,
 * tests inspected or run,
 * remaining risks,
 * explicit confirmation that dedupe logic was not changed, or the exact reason
   any touched internal code is observation-only.

History Review Phase
--------------------

Before merge, perform a separate history review after the final code review has
no blocking findings.  The goal is a maintainable commit stack, not a literal
record of every development turn.

The history review should:

 * inspect the branch commit-by-commit,
 * identify commits that do not build or test independently,
 * identify fixup commits that should be folded into earlier commits,
 * identify review-driven reversals or abandoned approaches to remove,
 * propose a final commit order and commit messages,
 * define the verification command for each final commit or group of commits,
 * keep documentation, tests, build integration, and behavior changes grouped
   where they are easiest to review.

The final pre-merge checklist is:

 * final AI code review completed,
 * blocking review findings resolved,
 * history review completed,
 * final commit stack rebuilt from the reviewed history plan,
 * relevant tests pass at the branch tip,
 * any high-risk internal-observation commits have been reviewed separately,
 * merge description records the final behavior and known limitations.

Agent Task Template
-------------------

Each AI-assisted implementation task should be framed like this:

        Implement Phase N only.
        MVP scope is Phases 1-4.  Phase 5+ requires separate approval.
        Do not touch dedupe logic.
        Expected files: ...
        Add or update tests: ...
        Verification command: ...
        Stop and ask for human review if daemon internals, dependencies,
        install behavior, or protocol design become necessary.

The agent's final report for each task should include:

 * files changed,
 * behavior added,
 * tests run,
 * any tests not run,
 * any user-visible compatibility risk,
 * confirmation that no dedupe logic was changed, or a precise explanation of
   why a touched internal file is observation-only.

Open Questions
--------------

 * Should the first implementation be C++ to match the existing codebase, or a
   script-like tool to iterate faster?
 * Should `beestop` live under `src/`, a new `tools/` directory, or another
   project-owned location?
 * Which terminal dependency, if any, is acceptable for packagers?
 * Should an eventual structured format be JSON, stable text, or a socket
   protocol?
 * What information is safe to expose by default, given that paths can reveal
   filesystem contents?
