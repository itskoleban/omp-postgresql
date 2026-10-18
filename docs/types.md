# PostgreSQL types and precision

The component sends bound parameters through libpq's extended query protocol. A parameter
has an explicit PostgreSQL OID and either a UTF-8 text representation or SQL NULL. SQL text
and parameter values are always separate. The server validates formats and applies column
constraints during execution; successful binding alone does not prove a value is valid SQL
input for its declared type.

Run [values.pwn](../examples/values.pwn) for scalar binding and NULL/false/exact BIGINT
reads, then [extensions.pwn](../examples/extensions.pwn) for nullable arrays and BYTEA.
The [example catalogue](examples.md) lists fixture and cleanup requirements.

## Scalar values

| PostgreSQL type       | Pawn binder                           | Representation and validation                                                                                           |
| --------------------- | ------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| `integer`             | `pg_bind_int`                         | Signed 32-bit Pawn cell.                                                                                                |
| `bigint`              | `pg_bind_int64`                       | Signed decimal string; checked locally against the int64 range. No leading `+`, whitespace, decimal point, or exponent. |
| `real`                | `pg_bind_float`                       | Finite 32-bit Pawn float; encoded with sufficient digits for a float32 round trip.                                      |
| `boolean`             | `pg_bind_bool`                        | Pawn `false` or `true`.                                                                                                 |
| `text`                | `pg_bind_string(..., PG_TEXT)`        | Valid UTF-8 without embedded NUL.                                                                                       |
| `smallint`            | `pg_bind_string(..., PG_SMALLINT)`    | Decimal text; the server checks the signed 16-bit range.                                                                |
| `numeric` / `decimal` | `pg_bind_string(..., PG_NUMERIC)`     | Decimal text preserves precision before PostgreSQL applies the destination scale.                                       |
| `double precision`    | `pg_bind_string(..., PG_DOUBLE)`      | Decimal text avoids an intermediate conversion through a Pawn float. PostgreSQL parses float64.                         |
| `uuid`                | `pg_bind_string(..., PG_UUID)`        | UUID text; PostgreSQL validates and normalizes it.                                                                      |
| `jsonb`               | `pg_bind_string(..., PG_JSONB)`       | JSON text; PostgreSQL validates and normalizes it.                                                                      |
| `bytea`               | `pg_bind_bytes`                       | One byte per Pawn cell, each in `0..255`; all byte values, including NUL, are supported.                                |
| `date`                | `pg_bind_string(..., PG_DATE)`        | Prefer unambiguous ISO dates, such as `2024-02-29`.                                                                     |
| `time`                | `pg_bind_string(..., PG_TIME)`        | Time without time zone, such as `01:02:03.123456`.                                                                      |
| `timestamp`           | `pg_bind_string(..., PG_TIMESTAMP)`   | Date and time without time zone.                                                                                        |
| `timestamptz`         | `pg_bind_string(..., PG_TIMESTAMPTZ)` | Prefer an explicit UTC offset, such as `2026-10-12T15:40:30-05:00`.                                                     |
| `interval`            | `pg_bind_string(..., PG_INTERVAL)`    | PostgreSQL interval text, such as `2 days 03:04:05`.                                                                    |

Read BIGINT, NUMERIC, DOUBLE, dates, times, UUIDs, JSONB, and intervals with
`pg_result_string`. This returns the server's text output without narrowing it through a
Pawn numeric cell. Output formatting can differ from input: JSONB normalizes JSON, and
TIMESTAMPTZ output uses the session's `TimeZone`. `DateStyle`, `IntervalStyle`, and
`extra_float_digits` also affect output. Set a consistent policy in the database role or
service configuration. A SQL column's declared numeric scale can still round an input;
preserving its transport representation does not override PostgreSQL's column rules.

`pg_result_int` rejects values outside int32. `pg_result_float` explicitly requests a
float32 conversion and may round a wider floating-point value. Use the string getter when
that rounding is unacceptable. Typed result getters reject NULL and invalid values rather
than substituting zero. Inspect `pg_result_is_null` first when NULL is allowed. Column names
and server type OIDs are available through `pg_result_column_name` and
`pg_result_column_type`; output OIDs may differ from input OIDs after SQL casts or expressions.

BOOLEAN output needs a separate distinction: `SELECT true` returns OID 16 and text `t`,
whereas `SELECT true::text` returns OID 25 and text `true` (`false` likewise becomes `false`
instead of native `f`). `pg_result_bool` accepts native `t`/`f`; use the string getter for
an explicit text cast. Array subscripting followed by `::text` follows the same cast rule.

See PostgreSQL's [numeric type reference](https://www.postgresql.org/docs/current/datatype-numeric.html)
for server precision, scale, and special-value semantics.

## Binary values

`pg_bind_bytes(statement, index, bytes, count)` reads exactly `count` cells. It neither reads
nor appends a string terminator. The component encodes these bytes as BYTEA hex internally;
the encoded parameter uses `2 + 2 * count` bytes of the parameter limit. Existing
`pg_bind_string(..., PG_BYTEA)` calls remain supported for applications already supplying
PostgreSQL BYTEA text themselves.

`pg_result_bytes(job, result, row, column, output, written, capacity)` requires a BYTEA column
and `bytea_output=hex`, PostgreSQL's default. It decodes all bytes into cells and sets
`written` to their count. Empty BYTEA succeeds with `written = 0`; SQL NULL is different and
is rejected by this getter. A buffer that is too small, an invalid address, an overlapping
length output, or malformed hex fails before either output is changed. No truncation or
terminator is added. Setting `bytea_output=escape` requires using the string getter instead.

## Arrays and individual NULL elements

`pg_bind_int_array` and `pg_bind_string_array` retain their existing behavior. The generic
`pg_bind_array(statement, index, values, count, width, element_type, nulls)` additionally
supports each scalar type listed above, with the corresponding built-in array OID. Supply
the scalar OID, such as `PG_INT64`, not `PG_INT64_ARRAY`.

`values` is one flat array of unpacked Pawn string slots. Slot `i` begins at `i * width` and
must contain a NUL terminator within that width when its value is not NULL. `nulls` contains
exactly one `0` or `1` per element. A `1` sends SQL NULL without reading that string slot.
This layout avoids compiler-dependent Pawn multidimensional offsets. At most 16384 elements
and 262144 total slot cells are accepted; encoded parameters must also fit the configured
parameter and batch limits.

When filling a slot through an indexed cell, pass the slot width explicitly to string
functions; their default `sizeof` does not describe that slot. For example:

```pawn
new values[72], nulls[] = {0, 1, 0};
strcat(values, "9223372036854775807", 24);
strcat(values[48], "-9223372036854775808", 24);
pg_bind_array(statement, 1, values, 3, 24, PG_INT64, nulls);
```

All non-NULL elements use textual input, including booleans (`true`), floats, BIGINT decimal
strings, JSONB, and BYTEA (`\\x00ff`). BIGINT elements are range-checked locally; other element
formats are checked by PostgreSQL. Quotes, backslashes, commas, and braces are escaped by
the component. Text equal to `NULL` is preserved as text, and an empty string remains an
empty string. No multi-dimensional array constructor or array-text decoder is included.
Use SQL `unnest`, subscripting, or another SQL expression to retrieve elements as columns.

These three inputs remain distinct:

- `pg_bind_null(statement, index, PG_INT64_ARRAY)` sends a NULL array.
- `pg_bind_array` with `count = 0` sends an empty array; `width` must still be positive.
- A positive count with one or more `nulls[i] = 1` sends an array containing NULL elements.

PostgreSQL's [array reference](https://www.postgresql.org/docs/current/arrays.html) describes
array semantics. In bulk SQL, validate related array lengths in the application or SQL.
Different-length `unnest` inputs can pad with NULL, and duplicate update identifiers need
an explicit application policy; neither condition is silently corrected by the binder.

## Validation coverage

`tests/type_tests.cpp` checks the supported scalar and array OIDs, UTF-8 rejection,
BIGINT boundaries, all 256 byte values, empty binary values, binary validation errors,
array escaping, and the distinction between NULL and literal text. Its PostgreSQL
integration mode covers every supported array type in prepared, direct, and heterogeneous
batch execution, individual NULL elements, scalar/array result OIDs, typed
whole-array NULLs, numeric precision, date/time normalization, invalid server formats,
and BYTEA round trips. The open.mp extension gamemode also exercises the Pawn binary and
array interfaces; see [validation](validation.md) for the actual runs and limitations.

## Callback argument cells

The callback format is independent of PostgreSQL OIDs: `i` copies an int32 cell, `f` copies
the Float bit pattern, `b` accepts only 0/1, and `s` owns a UTF-8 string. No SQL encoding or
numeric conversion occurs. Callback strings arrive unpacked and exist for that callback
invocation. Arrays/references are unsupported. See [callback limits](callbacks.md#formats-and-bounds).
