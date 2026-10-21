# Logging control design

This design records the configurable diagnostics implemented in owner-merged
[PR #19](https://github.com/itskoleban/omp-postgresql/pull/19), with additional qualification
in [PR #20](https://github.com/itskoleban/omp-postgresql/pull/20). Both are included in
the published testing `v0.1.0-rc.1`; neither integration nor publication certifies production safety.
See [current readiness](rc-readiness.md) and [qualification](logging-qualification.md).

Historical baseline inspected: master `1379ceded4a45f5e31df3f424de8d918319207ba`, clean
checkout, no open PR, master CI still running at that inspection. The contract and design
rationale below retain their original scope.

## Contract selected before implementation

`pg_log_config(level, destination=PG_LOG_CONSOLE, const path[]="")` controls the component,
not an AMX. All public Engine/native operations are main-thread operations. Last accepted
call wins; unloading/resetting any AMX never restores old settings. Startup retains
`OMP_PG_LOG_LEVEL` (WARN default) and console output. A valid Pawn call supersedes it.
Invalid enum/path/string calls leave the configuration untouched. Console requires an
empty path. File/both use `logs/omp-postgresql.log` for an empty path.

To keep the authorized directory small and portable, custom paths are exactly
`logs/<basename>.log`, optionally prefixed by `./`; no subdirectories. Basenames use ASCII
letters/digits/underscore/hyphen/dot, at most 96 bytes, no leading/trailing dots, repeated
dots or Windows reserved device names. Reject traversal, roots/drives/UNC, backslashes,
NUL/control/non-ASCII/invalid UTF-8, other extensions. No caller-supplied path is logged.

Windows file opens use the pinned directory as `NtCreateFile` RootDirectory,
with `FILE_OPEN_REPARSE_POINT`; directory write sharing permits child rotation while
delete sharing remains denied. [Microsoft API contract](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntcreatefile).

Success means accepted, **not opened, flushed or persisted**. Filesystem checks and all
open/write/rotation happen on a dedicated writer, never a libpq worker or tick. A failed
destination disables file delivery until another accepted configuration; a numeric,
rate-limited main-thread console fallback reports cumulative failure/drop counters.
OFF suppresses normal formatting/output and fallback. Job errors remain independent.

## Bounded handoff and lifetime

Keep the existing 256-record numeric try-lock/drop ring and 64-line tick budget. Add a
second 256-record numeric file queue; producers still only touch the original ring.
The tick copies numeric records to the file queue; the writer formats into a fixed
512-byte buffer. Console formatting remains outside all locks. No unbounded text queue,
per-record producer allocation or producer filesystem call. One pending configuration
slot coalesces requests. Reconfiguration discards queued records with drop accounting;
an already removed record may finish on its original destination. Never move an old
record into a new file. Serialized main-thread calls and generation checks protect
reentrant sinks. Worker I/O never holds producer/control locks.

Append UTF-8/ASCII one-line numeric events, writer UTC timestamps, maximum 511-byte event
plus bounded timestamp/newline. Rotate before exceeding 1 MiB to `<name>.log.1`, retain
one archive, no truncation of an existing active file except intentional bounded rotation.
Reject links/reparse points, non-regular files and multiple hard links. Detect removed or
replaced files; require explicit configuration to reopen. Local server directory and its
ancestors must be administrator-controlled; file logging is not for network filesystems.

Engine already joins lanes before Diagnostics destruction. Stop writer, discard pending
records with accounting, close on writer thread, join before code unload. This is best
effort delivery, no fsync/durable flush. Queue work is bounded; OS filesystem calls on
arbitrarily stalled devices cannot provide an absolute shutdown wall-time guarantee.
Do not detach a thread executing component code to pretend otherwise. This operational
limit must remain explicit in delivered documentation and qualification.

## SQL diagnostics

Extend the existing lexer without changing its grammar. Keep original file bytes/BOM,
track physical CR/LF/CRLF positions with a failure-only linear prefix scan (at most twice) (1-based byte columns;
initial UTF-8 BOM is metadata and consumes no displayed column). Capture declaration
positions and first duplicate line. Only bounded valid ASCII section names and sanitized
relative filenames enter owner-readable errors; never source fragments or absolute paths.
Missing file/section/no sections/invalid encoding have no invented source position.
Capture still validates all sections before dispatch, including unselected sections.
Whole-file mode stays separate. Local invalid section arguments stay pg_last_error.

Server position mapping is deferred for this delivery: prepared/cache/stream/result paths
currently discard statement-position fields, and character offsets require an explicitly
retained mapping for each submitted result. Preserve SQLSTATE and original result message;
never guess a file location from generic PostgreSQL errors. Parser locations ship first.

## Windows rotation refinement after source review

Rotate with `NtSetInformationFile` FileRenameInformation, holding DELETE access to the
still-open source and rooting the relative archive name at the pinned directory. Do not
close then re-resolve a Win32 path. This removes a directory-reparse substitution window.
Live readers must share DELETE access. Native rename layout offsets/length and the 128-WCHAR
buffer are asserted/bounded on x86/x64; the SDK's incomplete class enum is passed through
its 32-bit integer ABI representation. No per-event open was added.
