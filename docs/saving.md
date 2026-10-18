# Saving account data without blocking the game

Start with [saving.pwn](../examples/saving.pwn), which uses a temporary table and checks
final values. Copy [tutorial-select.sql](../examples/sql/tutorial-select.sql) into
`scriptfiles/sql` before running it. `PG SAVING PASS` means the three save strategies and
ordinary SQL-file read returned the expected results in that session.
Its repeating timer selects one bounded snapshot wave after the individual save confirms;
a due flag prevents duplicate in-flight waves. This finite fixture then advances to bulk
verification and stops the timer. Choose a measured, much slower cadence for a real server.

## Choose the unit of work

| Strategy | Submission | SQL executions | Appropriate use |
| --- | --- | --- | --- |
| Individual | One bind/add/execute per account | One per job | Immediate independent save with its own context/result. |
| Repeated snapshots | Bind/add in a loop, execute once | One per snapshot, FIFO on one lane | A bounded group using the same statement; each gets its own result. |
| Set-based UNNEST | Build related arrays, bind/add once | One SQL statement updating many rows | Bulk statistics, with explicit input/affected-row validation. |
| Heterogeneous batch | Transfer staged statements with `pg_batch_add` | One per flattened operation | Different SQL/parameter sets in a chosen order. |

Twenty calls to `pg_add` create twenty immutable parameter snapshots, not twenty jobs or
one UPDATE of twenty rows. Every successful add clears temporary bindings; bind all
parameters again for the next snapshot. Acceptance consumes staged snapshots. A rejected
enqueue preserves them, which allows a later **submission** attempt without having sent SQL.
Creating another statement for a blind replay of accepted work is a different and unsafe action.

The example reuses a logical statement after the previous job confirms. Different SQL can
replace a lane's unnamed physical plan; logical reuse does not promise a permanent server
plan. Type OIDs stay fixed after the first successful `pg_add`. Close and recreate the
logical statement when parameter types must change.

## Build a bounded periodic save

Use a timer to select a bounded number of dirty accounts, copy their IDs/values/versions
into a save record, and submit only when ready and capacity is available. Keep one in-flight
save per account or define versioned conflict handling. A later timer must not clear a
dirty flag merely because an earlier captured value confirmed. Record the captured version
and clear only the corresponding version on success. Queuing every player on every tick
can exceed job/result limits and stall useful work.

The timer's loop body is the same for a normal `for` or an application-provided
`foreach (new playerid : Player)`. YSI is not a component dependency. Select only loaded,
authenticated accounts; copy database IDs before player-slot data can be reused. For a
disconnected player, retain the save record independently of the player's slot.

For `UNNEST`, pass the actual count, not array capacity. Check related lengths, positive IDs,
duplicate IDs and NULL policy before binding. Skip an empty group instead of submitting an
empty statement. PostgreSQL pads unequal UNNEST arrays with NULL, and an UPDATE joining
multiple source rows to one target has no predictable winner; see
[PostgreSQL UPDATE](https://www.postgresql.org/docs/17/sql-update.html).
The binder cannot infer these application rules.

The simple fixture replaces absolute scores. Concurrent production saves should normally
guard a version (`WHERE id=$1 AND version=$2`) or use appropriately locked/atomic changes.
An affected count of zero is successful SQL, but may indicate a missing account or version
conflict. RETURNING can identify exactly which account/version was updated; keep returned
rows bounded. BIGINT IDs or versions outside int32 must travel as decimal strings.

## Failure and atomicity

Repeated snapshots and heterogeneous batches stop on the first failure. Outside an explicit
transaction, earlier operations may already have committed. Check `pg_job_completed`, each
`pg_job_item_status`, `pg_job_error_index`, available results and affected-row strings. The
number of SQL executions is different from the number of rows changed. Use a transaction
when the whole group must succeed together; wait for COMMIT before marking a save durable
under the database's configured durability policy.

An uncertain write requires reconciliation using an application operation ID or expected
version. Cancellation is a request, not an undo. A connection closing while a job runs does
not prove that the database ignored it. Close jobs after completion dispatch; retain enough
application state to reconcile before releasing their results. On shutdown, stop creating
saves and drain with a bounded deadline. A callback in `OnGameModeExit` cannot wait for the
component to finish network work.
