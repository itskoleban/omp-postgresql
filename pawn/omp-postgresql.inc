#if defined _omp_postgresql_included
#endinput
#endif
#define _omp_postgresql_included

#define PG_INVALID (0)
#define PG_BOOL (16)
#define PG_BYTEA (17)
#define PG_INT64 (20)
#define PG_INT (23)
#define PG_TEXT (25)
#define PG_FLOAT (700)
#define PG_INT_ARRAY (1007)
#define PG_TEXT_ARRAY (1009)
#define PG_UUID (2950)
#define PG_JSONB (3802)
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
native pg_prepare(connection, const sql[], parameters = 0, bool:prepared = true);
native pg_prepare_file(connection, const filename[], parameters = 0, bool:prepared = true);
native bool:pg_bind_int(statement, index, value);
native bool:pg_bind_int64(statement, index, const decimal[]);
native bool:pg_bind_float(statement, index, Float:value);
native bool:pg_bind_bool(statement, index, bool:value);
native bool:pg_bind_null(statement, index, type);
native bool:pg_bind_string(statement, index, const value[], type = PG_TEXT);
native bool:pg_bind_int_array(statement, index, const values[], count = sizeof values);
// Flat unpacked string slots, row i starts at values[i * width].
native bool:pg_bind_string_array(statement, index, const values[], count, width);
native bool:pg_add(statement);
native pg_execute(statement, lane = 0, transaction = 0, context = 0, bool:notify = true);
native bool:pg_statement_close(statement);
native pg_begin(connection, lane = 0, context = 0, bool:notify = true);
native pg_commit(transaction, context = 0, bool:notify = true);
native pg_rollback(transaction, context = 0, bool:notify = true);
native pg_job_status(job);
native bool:pg_job_close(job);
native bool:pg_cancel(job);
native pg_job_completed(job);
native pg_job_error(job);
native bool:pg_job_message(job, output[], size = sizeof output);
native bool:pg_job_duration(job, microseconds[], size = sizeof microseconds);
native pg_job_connection(job);
native pg_result_count(job);
native pg_result_rows(job, result = 0);
native pg_result_columns(job, result = 0);
native pg_result_column(job, result, const name[]);
native bool:pg_result_is_null(job, result, row, column);
native bool:pg_result_string(job, result, row, column, output[], size = sizeof output);
native bool:pg_result_int(job, result, row, column, &value);
native bool:pg_result_float(job, result, row, column, &Float:value);
native bool:pg_result_bool(job, result, row, column, &bool:value);
native bool:pg_result_affected(job, result, output[], size = sizeof output);
native bool:pg_result_sqlstate(job, result, output[], size = sizeof output);
native bool:pg_result_message(job, result, output[], size = sizeof output);
native bool:pg_last_error(output[], size = sizeof output);
forward OnPgConnection(connection, connection_state);
forward OnPgComplete(job, context);
