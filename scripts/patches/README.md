# Windows libpq mutex correction

`libpq-windows-mutex.patch` is a narrow downstream correction to PostgreSQL's Windows
mutex emulation. It changes neither libpq's public ABI nor the component execution model.
The modified PostgreSQL code remains under the PostgreSQL License; binary distributions
retain PostgreSQL's COPYRIGHT. This directory and its explanation are installed with
Windows packages so the dependency modification is visible and reproducible.

An initializer that observed state 0 can resume after another thread published state 1.
The original unconditional exchange writes 2 over that initialized state. If the owner
unlocks in that interval, unlock returns EINVAL without releasing the critical section.
All subsequent connection workers can remain blocked inside libpq, beyond cancellation
or the component's deadline. Use compare-exchange to claim only state 0, publish only
from the initializing thread, and use interlocked state reads. Preserve the struct layout.

This follows the independently reported mechanism and correction in Harrison Booth's
[PostgreSQL discussion](https://www.postgresql.org/message-id/CAAH-eSXFXSEtXpb4kUAGOKKdm3o3y-yWfjsJ7R7zUL6g-swuzw@mail.gmail.com).
On 2026-10-17 the [upstream patch](https://commitfest.postgresql.org/patch/7255/) was still
awaiting review. Do not describe this downstream fix as an upstream-released correction.
Recheck that status before upgrading or removing it. The ECPG implementation and
`pthread_once` are outside this libpq-only build and are not patched here.

The build helper accepts an exact forward or reverse patch application and rejects an
unrecognized implementation. Run it against a fresh official source tree, or rerun it
against that same patched tree. Linux clients are unaffected. Custom externally supplied
Windows clients must be independently checked for this race; their version alone is
insufficient evidence.

Set `PG_LIBPQ_SOURCE_DIR` to that PostgreSQL source root on Windows to register
`libpq_windows_mutex_unit`. It compiles the real dependency C file and header with a
minimal Windows frontend preamble. Real Windows interlocked operations, events, threads
and critical sections are used; hooks force a stale initializer to attempt its claim
while another thread owns the initialized mutex. Unpatched source deterministically
fails at the holder's unlock; patched source must pass that schedule, reject an
uninitialized unlock and retain 80,000 increments from eight contending threads.
The CI Windows x86 build enables this test on the exact source used to build its DLL.
It supplements actual Engine/TLS/process startup tests; it does not prove every historical
timeout had this cause or that other connection failures are impossible.
