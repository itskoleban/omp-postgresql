#if defined _omp_postgresql_included
#endinput
#endif

#define _omp_postgresql_included

#define PG_INVALID (0)

enum a {
	PG_LOG_OFF,
	PG_LOG_ERROR,
	PG_LOG_WARN,
	PG_LOG_INFO,
	PG_LOG_DEBUG,
	PG_LOG_TRACE
};

enum b {
	PG_LOG_CONSOLE,
	PG_LOG_FILE,
	PG_LOG_BOTH
};

// Component-global; accepted configuration, not disk persistence. Last valid call wins.
// File paths: empty = logs/omp-postgresql.log; custom [./]logs/<basename>.log.
// Console requires empty path. Async sink failures: bounded numeric console fallback.
native bool:pg_log_config(level, destination = PG_LOG_CONSOLE, const path[] = "");

#define PG_BOOL (16)
#define PG_BYTEA (17)
#define PG_INT64 (20)
#define PG_SMALLINT (21)
#define PG_INT (23)
#define PG_TEXT (25)
#define PG_FLOAT (700)
#define PG_DOUBLE (701)
#define PG_DATE (1082)
#define PG_TIME (1083)
#define PG_TIMESTAMP (1114)
#define PG_TIMESTAMPTZ (1184)
#define PG_INTERVAL (1186)
#define PG_NUMERIC (1700)
#define PG_BOOL_ARRAY (1000)
#define PG_BYTEA_ARRAY (1001)
#define PG_SMALLINT_ARRAY (1005)
#define PG_INT_ARRAY (1007)
#define PG_TEXT_ARRAY (1009)
#define PG_INT64_ARRAY (1016)
#define PG_FLOAT_ARRAY (1021)
#define PG_DOUBLE_ARRAY (1022)
#define PG_DATE_ARRAY (1182)
#define PG_TIME_ARRAY (1183)
#define PG_TIMESTAMP_ARRAY (1115)
#define PG_TIMESTAMPTZ_ARRAY (1185)
#define PG_INTERVAL_ARRAY (1187)
#define PG_NUMERIC_ARRAY (1231)
#define PG_UUID (2950)
#define PG_UUID_ARRAY (2951)
#define PG_JSONB (3802)
#define PG_JSONB_ARRAY (3807)

enum {
	PG_DISCONNECTED,
	PG_CONNECTING,
	PG_CONNECTED,
	PG_RECONNECTING,
	PG_CONNECTION_FAILED,
	PG_CLOSING
};

enum {
	PG_QUEUED,
	PG_RUNNING,
	PG_CONFIRMED,
	PG_FAILED,
	PG_UNCERTAIN,
	PG_CANCELLED
};

enum {
	PG_ITEM_NOT_EXECUTED,
	PG_ITEM_CONFIRMED,
	PG_ITEM_FAILED,
	PG_ITEM_UNCERTAIN,
	PG_ITEM_CANCELLED
};

enum {
	PG_READ_COMMITTED,
	PG_REPEATABLE_READ,
	PG_SERIALIZABLE
};

enum {
	PG_STAT_COMPLETED,
	PG_STAT_CONFIRMED,
	PG_STAT_FAILED,
	PG_STAT_UNCERTAIN,
	PG_STAT_CANCELLED,
	PG_STAT_CONNECTION_ATTEMPTS,
	PG_STAT_RECONNECTS,
	PG_STAT_SQL_COMMANDS,
	PG_STAT_ROWS_RECEIVED,
	PG_STAT_DURATION_US
};

enum {
	PG_ERROR_NONE,
	PG_ERROR_INVALID,
	PG_ERROR_LIMIT,
	PG_ERROR_NOT_READY,
	PG_ERROR_SQL,
	PG_ERROR_CONNECTION,
	PG_ERROR_TIMEOUT,
	PG_ERROR_CANCELLED,
	PG_ERROR_FILE,
	PG_ERROR_TRANSACTION,
	PG_ERROR_INTERNAL
};

native pg_connect(const profile[], pool_size = 1);
native bool:pg_connection_close(connection);
native pg_connection_state(connection);
native pg_connection_ready(connection);
native bool:pg_connection_message(connection, lane, output[], size = sizeof output);

native pg_lane_state(connection, lane);
native pg_lane_pending(connection, lane);
native pg_lane_active(connection, lane);
native bool:pg_lane_stat(connection, lane, metric, output[], size = sizeof output);

native pg_query(connection, const sql[], lane = 0, transaction = 0, context = 0, bool:notify = true, const callback[] = "", const format[] = "", {Float,_}:...);

native pg_prepare(connection, const sql[], parameters = 0, bool:prepared = true);
native pg_prepare_file(connection, const filename[], parameters = 0, bool:prepared = true, const section[] = "");

native bool:pg_bind_int(statement, index, value);
native bool:pg_bind_int64(statement, index, const decimal[]);
native bool:pg_bind_float(statement, index, Float:value);
native bool:pg_bind_bool(statement, index, bool:value);
native bool:pg_bind_null(statement, index, type);
native bool:pg_bind_string(statement, index, const value[], type = PG_TEXT);
native bool:pg_bind_int_array(statement, index, const values[], count = sizeof values);
// Flat unpacked string slots, row i starts at values[i * width].
native bool:pg_bind_string_array(statement, index, const values[], count, width);
// One byte per cell, including NUL; no string terminator is read or added.
native bool:pg_bind_bytes(statement, index, const bytes[], count = sizeof bytes);
// Flat textual slots and a 0/1 NULL mask; element_type is a scalar PG_* OID.
native bool:pg_bind_array(statement, index, const values[], count, width, element_type, const nulls[]);
native bool:pg_add(statement);

native pg_execute(statement, lane = 0, transaction = 0, context = 0, bool:notify = true, const callback[] = "", const format[] = "", {Float,_}:...);

native bool:pg_statement_close(statement);

native pg_batch_create(connection);
native bool:pg_batch_add(batch, statement);
native pg_batch_execute(batch, lane = 0, transaction = 0, context = 0, bool:notify = true, const callback[] = "", const format[] = "", {Float,_}:...);
native bool:pg_batch_close(batch);

native pg_stream_execute(statement, chunk_rows = 128, lane = 0, transaction = 0, context = 0, bool:notify = true, const callback[] = "", const format[] = "", {Float,_}:...);
native bool:pg_stream_next(job);
native bool:pg_stream_release(job);

native pg_begin(connection, lane = 0, context = 0, bool:notify = true, const callback[] = "", const format[] = "", {Float,_}:...);
native pg_begin_ex(connection, isolation, bool:read_only, lane = 0, context = 0, bool:notify = true, const callback[] = "", const format[] = "", {Float,_}:...);
native pg_savepoint(transaction, const name[], context = 0, bool:notify = true, const callback[] = "", const format[] = "", {Float,_}:...);
native pg_rollback_to(transaction, const name[], context = 0, bool:notify = true, const callback[] = "", const format[] = "", {Float,_}:...);
native pg_release_savepoint(transaction, const name[], context = 0, bool:notify = true, const callback[] = "", const format[] = "", {Float,_}:...);
native pg_commit(transaction, context = 0, bool:notify = true, const callback[] = "", const format[] = "", {Float,_}:...);
native pg_rollback(transaction, context = 0, bool:notify = true, const callback[] = "", const format[] = "", {Float,_}:...);

native pg_job_status(job);
native bool:pg_job_close(job);
native bool:pg_cancel(job);
native pg_job_completed(job);
native pg_job_size(job);
native pg_job_item_status(job, item);
native pg_job_error_index(job);
native pg_job_error(job);
native bool:pg_job_message(job, output[], size = sizeof output);
native bool:pg_job_duration(job, microseconds[], size = sizeof microseconds);
native bool:pg_job_queue_time(job, microseconds[], size = sizeof microseconds);
native bool:pg_job_latency(job, microseconds[], size = sizeof microseconds);
native pg_job_connection(job);

native pg_result_count(job);
native pg_result_rows(job, result = 0);
native pg_result_columns(job, result = 0);
native pg_result_column(job, result, const name[]);
native bool:pg_result_column_name(job, result, column, output[], size = sizeof output);
native pg_result_column_type(job, result, column);
native bool:pg_result_is_null(job, result, row, column);
native bool:pg_result_string(job, result, row, column, output[], size = sizeof output);
native bool:pg_result_bytes(job, result, row, column, output[], &written, capacity = sizeof output);
native bool:pg_result_int(job, result, row, column, &value);
native bool:pg_result_float(job, result, row, column, &Float:value);
native bool:pg_result_bool(job, result, row, column, &bool:value);
native bool:pg_result_affected(job, result, output[], size = sizeof output);
native bool:pg_result_sqlstate(job, result, output[], size = sizeof output);
native bool:pg_result_message(job, result, output[], size = sizeof output);

native bool:pg_last_error(output[], size = sizeof output);
forward OnPgConnection(connection, connection_state);
forward OnPgComplete(job, context);
forward OnPgRows(job, context);
