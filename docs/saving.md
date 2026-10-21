# Saving account data without blocking the game

Start with [saving.pwn](../examples/saving.pwn), which uses a temporary table and checks
final values. Copy [tutorial-select.sql](../examples/sql/tutorial-select.sql) into
`scriptfiles/sql` before running it. `PG SAVING PASS` means the three save strategies and
ordinary SQL-file read returned the expected results in that session.
Its repeating timer selects one bounded snapshot wave after the individual save confirms;
a due flag prevents duplicate in-flight waves. This finite fixture then advances to bulk
verification and stops the timer. Choose a measured, much slower cadence for a real server.

## Choose the unit of work

| Strategy            | Submission                                     | SQL executions                       | Appropriate use                                                     |
| ------------------- | ---------------------------------------------- | ------------------------------------ | ------------------------------------------------------------------- |
| Individual          | One bind/add/execute per account               | One per job                          | Immediate independent save with its own context/result.             |
| Repeated snapshots  | Bind/add in a loop, execute once               | One per snapshot, FIFO on one lane   | A bounded group using the same statement; each gets its own result. |
| Set-based UNNEST    | Build related arrays, bind/add once            | One SQL statement updating many rows | Bulk statistics, with explicit input/affected-row validation.       |
| Heterogeneous batch | Transfer staged statements with `pg_batch_add` | One per flattened operation          | Different SQL/parameter sets in a chosen order.                     |

Twenty calls to `pg_add` create twenty immutable parameter snapshots, not twenty jobs or
one UPDATE of twenty rows. Every successful add clears temporary bindings; bind all
parameters again for the next snapshot. Acceptance consumes staged snapshots. A rejected
enqueue preserves them, which allows a later **submission** attempt without having sent SQL.
Creating another statement for a blind replay of accepted work is a different and unsafe action.

The example reuses a logical statement after the previous job confirms. Different SQL can
replace a lane's unnamed physical plan; logical reuse does not promise a permanent server
plan. Type OIDs stay fixed after the first successful `pg_add`. Close and recreate the
logical statement when parameter types must change.

## One account, repeated snapshots, or one set-based update

These fragments assume `#include <open.mp>`, `#include <omp-postgresql>`, a connection
confirmed ready by `OnPgConnection`, and the `tutorial_saves(id integer PRIMARY KEY,
score integer NOT NULL)` table from [saving.pwn](../examples/saving.pwn), seeded with IDs
1, 2 and 3. Use lane 0 for that example's temporary table. Each function returns an accepted
job or 0; the shared helpers below inspect completion and close resources. They are fragments
for a script, not separate gamemodes. Do not call all three strategies for the same save.

One account needs one captured parameter set. Player-supplied values remain parameters:

<!-- pawn-example: save.one -->
```pawn
stock SaveOne(connection, accountid, score) {
    new statement = pg_prepare(connection,
        "UPDATE tutorial_saves SET score=$2 WHERE id=$1", 2);
    if (!statement || !pg_bind_int(statement, 1, accountid) ||
        !pg_bind_int(statement, 2, score) || !pg_add(statement))
        return RejectSave(statement);
    new job = pg_execute(statement, 0, 0, 0, true, "OnScoresSaved", "ii", 1, 1);
    if (!job) return RejectSave(statement);
    pg_statement_close(statement); // The accepted job owns its snapshot.
    return job;
}
```

For repeated saves, keep a script-level `SaveStatement` handle (initially 0). Prepare it
once after connection readiness; this fragment belongs inside that initialization handler:

<!-- pawn-example: save.prepare -->
```pawn
SaveStatement = pg_prepare(connection,
    "UPDATE tutorial_saves SET score=$2 WHERE id=$1", 2);
if (!SaveStatement) return RejectSave(SaveStatement);
```

Call `SaveThree(SaveStatement)` for the next wave. Bind every parameter again before each
`pg_add`, then submit once; the caller retains the statement after acceptance:

<!-- pawn-example: save.snapshots -->
```pawn
stock SaveThree(&statement) {
    new ids[] = {1, 2, 3}, scores[] = {100, 200, 300};
    for (new i = 0; i < sizeof ids; i++) {
        if (!pg_bind_int(statement, 1, ids[i]) ||
            !pg_bind_int(statement, 2, scores[i]) || !pg_add(statement))
            return RejectSave(statement); // Discard all unsent snapshots.
    }
    new job = pg_execute(statement, 0, 0, 0, true, "OnScoresSaved", "ii", 3, 1);
    if (!job) return RejectSave(statement);
    return job;
}
```

This is one Pawn submission and **three SQL executions**, with three results and one
terminal callback. Call `SaveThree(SaveStatement)` again only after the previous completion
has been checked and the next save is due; it reuses the logical statement across jobs.
After acceptance its snapshots are consumed, so every call binds/adds the next wave afresh.
On local rejection, `RejectSave` closes and zeros the caller's handle: recreate it before
another wave. When retiring the save path or replacing its connection, stop submissions,
call `pg_statement_close(SaveStatement)` if nonzero, then set `SaveStatement = 0`.

To update all three accounts in one PostgreSQL execution, pair typed arrays in `FROM`:

<!-- sql-example: save.sql -->
```sql
UPDATE tutorial_saves AS a
SET score = v.score
FROM unnest($1::integer[], $2::integer[]) AS v(id, score)
WHERE a.id = v.id;
```

The SQL above is a parameterized statement template, not a standalone command with values.
Here both arrays have capacity four but only three entries belong to this save:

<!-- pawn-example: save.bulk -->
```pawn
stock SaveBulk(connection) {
    new ids[4] = {1, 2, 3, 0}, scores[4] = {100, 200, 300, 0};
    new count = 3; // Both populated lengths match; these IDs are unique.
    new statement = pg_prepare(connection,
        "UPDATE tutorial_saves AS a SET score=v.score FROM unnest($1::integer[],$2::integer[]) AS v(id,score) WHERE a.id=v.id", 2);
    if (!statement || !pg_bind_int_array(statement, 1, ids, count) ||
        !pg_bind_int_array(statement, 2, scores, count) || !pg_add(statement))
        return RejectSave(statement);
    new job = pg_execute(statement, 0, 0, 0, true, "OnScoresSaved", "ii", 1, count);
    if (!job) return RejectSave(statement);
    pg_statement_close(statement);
    return job;
}
```

For these inputs and existing rows, the successful outcomes differ as follows:

| Strategy                      | `pg_add` calls | `pg_execute` submissions / jobs | SQL executions / results | Expected affected rows | Atomicity outside an explicit transaction                                     |
| ----------------------------- | -------------- | ------------------------------- | ------------------------ | ---------------------- | ----------------------------------------------------------------------------- |
| `SaveOne(connection, 1, 100)` | 1              | 1                               | 1                        | 1                      | One statement.                                                                |
| `SaveThree(SaveStatement)`    | 3              | 1                               | 3                        | 1 per result, 3 total  | Each statement commits separately; earlier saves can survive a later failure. |
| `SaveBulk(connection)`        | 1              | 1                               | 1                        | 3 in one result        | All changes made by this statement commit or fail together.                   |

Statement atomicity does not require every supplied ID to exist. A confirmed bulk UPDATE
can affect fewer than `count` rows; checking afterward detects that mismatch but cannot
undo an autocommitted statement. To require all requested accounts, run it in a
[transaction](transactions.md), inspect the count, and commit only when it matches.

Use this shared completion callback for the three fragments. It validates every expected
result without converting PostgreSQL's affected-row strings to a potentially overflowing cell:

<!-- pawn-example: save.result -->
```pawn
forward OnScoresSaved(job, expectedResults, expectedRows);
public OnScoresSaved(job, expectedResults, expectedRows) {
    new status = pg_job_status(job);
    if (status == PG_CONFIRMED) {
        new expected[24], affected[24];
        format(expected, sizeof expected, "%d", expectedRows);
        new bool:matched = pg_result_count(job) == expectedResults;
        for (new i = 0; matched && i < expectedResults; i++) {
            matched = pg_result_affected(job, i, affected) && !strcmp(affected, expected);
        }
        if (!matched) print("Save count mismatch; inspect the captured account IDs");
    } else {
        printf("Save ended with status %d, error %d", status, pg_job_error(job));
        // FAILED/CANCELLED/UNCERTAIN: reconcile; earlier snapshots may have committed.
    }
    pg_job_close(job);
    return 1;
}
```

The local rejection helper reads the diagnostic before cleanup clears it:

<!-- pawn-example: save.reject -->
```pawn
stock RejectSave(&statement) {
    new detail[256];
    if (pg_last_error(detail)) {
        // Inspect detail through an operator-only diagnostic path if needed.
    }
    print("Save rejected locally; no job was accepted");
    if (statement) pg_statement_close(statement);
    statement = 0;
    return 0;
}
```

These callbacks deliberately do not modify player slots. In an application, retain a save
record keyed by accepted job (account IDs, captured values/versions, connection and session
identity); collect per-item diagnostics before closing a failed job. Preserve the record
for reconciliation on uncertainty or count mismatch. Apply success only to the matching
session/version, as described in [callbacks](callbacks.md). A rejected enqueue sends no
SQL; `RejectSave` discards its preserved snapshots when it closes the statement.

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
duplicate IDs and NULL policy before binding. The snippets use unique IDs and non-NULL
scores; reject duplicate IDs in a production save builder, or explicitly reduce them to one
chosen value per ID before binding. Skip an empty group instead of submitting an empty
statement. PostgreSQL [pads unequal UNNEST arrays with NULL](https://www.postgresql.org/docs/17/functions-array.html), and an UPDATE joining
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
