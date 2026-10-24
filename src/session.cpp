#include "pg/session.hpp"
#include "pg/sql_sections.hpp"
#include "pg/statement.hpp"
#include "pg/stream.hpp"
#include <algorithm>
#include <cerrno>
#include <limits>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

// Compile-time syscall seam used by the deterministic socket regression only.
#ifndef PG_SOCKET_SELECT
#define PG_SOCKET_SELECT ::select
#endif

namespace pg {

namespace {

struct ResultDelete {
	void operator()(PGresult *p) const {
		PQclear(p);
	}
};

using PgResult = std::unique_ptr<PGresult, ResultDelete>;

std::string field(PGresult *r, int key) {
	auto p = PQresultErrorField(r, key);
	return p ? p : "";
}

void quiet_notice(void *, const char *) {}

bool valid_savepoint(const std::string &name) {
	if (name.empty() || name.size() > 63) {
		return false;
	}
	for (std::size_t index = 0; index < name.size(); ++index) {
		const auto c = static_cast<unsigned char>(name[index]);
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
		      (index && c >= '0' && c <= '9'))) {
			return false;
		}
	}
	return true;
}

} // namespace

Lane::Lane(std::string service, Limits limits, std::filesystem::path root,
           CompletionQueue &completed, Diagnostics *diagnostics, Owner owner, Id connection,
           int lane)
    : service_(std::move(service)), limits_(limits), sql_root_(std::move(root)),
      completed_(completed), diagnostics_(diagnostics), owner_(owner), connection_(connection),
      lane_(lane), thread_([this] {
	      run();
      }) {}

Lane::~Lane() {
	close();
	if (thread_.joinable()) {
		thread_.join();
	}
}

void Lane::close() {
	stop_.store(true);
	state.store(State::closing);
	cv_.notify_all();
}

std::string Lane::diagnostic() {
	std::lock_guard lock(mutex_);
	return diagnostic_;
}

LaneStats Lane::snapshot() const {
	return {queued_jobs_.load(std::memory_order_relaxed),
	        running_jobs_.load(std::memory_order_relaxed),
	        completed_jobs_.load(std::memory_order_relaxed),
	        confirmed_jobs_.load(std::memory_order_relaxed),
	        failed_jobs_.load(std::memory_order_relaxed),
	        uncertain_jobs_.load(std::memory_order_relaxed),
	        cancelled_jobs_.load(std::memory_order_relaxed),
	        connection_attempts_.load(std::memory_order_relaxed),
	        reconnects_.load(std::memory_order_relaxed),
	        sql_commands_.load(std::memory_order_relaxed),
	        rows_received_.load(std::memory_order_relaxed),
	        duration_us_.load(std::memory_order_relaxed)};
}

void Lane::log(LogEvent event, std::uint64_t value) noexcept {
	if (!diagnostics_) {
		return;
	}
	LogRecord record{event, owner_, connection_};
	record.lane = lane_;
	record.value = value;
	diagnostics_->emit(record);
}

void Lane::record_completion(const Job &job, Status status) {
	if (diagnostics_) {
		auto record = job_record(LogEvent::job_terminal, job);
		record.status = status;
		record.error = job.error;
		record.queue_us = job.queue_us;
		record.execution_us = job.duration_us;
		diagnostics_->emit(record);
	}
	completed_jobs_.fetch_add(1, std::memory_order_relaxed);
	duration_us_.fetch_add(static_cast<std::uint64_t>(job.duration_us), std::memory_order_relaxed);
	switch (status) {
	case Status::confirmed:
		confirmed_jobs_.fetch_add(1, std::memory_order_relaxed);
		break;
	case Status::failed:
		failed_jobs_.fetch_add(1, std::memory_order_relaxed);
		break;
	case Status::uncertain:
		uncertain_jobs_.fetch_add(1, std::memory_order_relaxed);
		break;
	case Status::cancelled:
		cancelled_jobs_.fetch_add(1, std::memory_order_relaxed);
		break;
	default:
		break;
	}
}

void Lane::enqueue(std::shared_ptr<Job> job) {
	std::lock_guard lock(mutex_);
	queue_.push_back(std::move(job));
	if (diagnostics_) {
		diagnostics_->emit(job_record(LogEvent::job_accepted, *queue_.back()));
	}
	queued_jobs_.fetch_add(1, std::memory_order_relaxed);
	cv_.notify_one();
}

void Lane::drop() {
	conn_.reset();
	prepared_sql_.clear();
	prepared_types_.clear();
	transaction_ = 0;
	transaction_failed_ = false;
}

Error Lane::wait_socket(bool writing, Clock::time_point deadline, const Job *job) {
	if (stop_.load() || (job && job->cancel.load())) {
		return Error::cancelled;
	}
	if (Clock::now() >= deadline) {
		return Error::timeout;
	}
	int socket = PQsocket(conn_.get());
	if (socket < 0) {
		return Error::connection;
	}
#ifndef _WIN32
	if (socket >= FD_SETSIZE) {
		return Error::connection;
	}
#endif
	fd_set fds;
	FD_ZERO(&fds);
#ifdef _WIN32
	FD_SET(static_cast<SOCKET>(socket), &fds);
#else
	FD_SET(socket, &fds);
#endif
	timeval timeout{0, 10000};
	int rc = PG_SOCKET_SELECT(socket + 1, writing ? nullptr : &fds, writing ? &fds : nullptr,
	                          nullptr, &timeout);
	if (rc < 0) {
		// Capture the wait's result before inspecting any later cancel/deadline.
		// A signal interruption is retryable; the caller rechecks its stop conditions.
#ifdef _WIN32
		return WSAGetLastError() == WSAEINTR ? Error::none : Error::connection;
#else
		return errno == EINTR ? Error::none : Error::connection;
#endif
	}
	return Error::none;
}

bool Lane::connect() {
	log(LogEvent::lane_connecting);
	drop();
	connection_attempts_.fetch_add(1, std::memory_order_relaxed);
	const char *keys[] = {"service", "client_encoding", "application_name", nullptr};
	const char *values[] = {service_.c_str(), "UTF8", "omp-postgresql", nullptr};
	conn_.reset(PQconnectStartParams(keys, values, 0));
	if (!conn_) {
		return false;
	}
	PQsetNoticeProcessor(conn_.get(), quiet_notice, nullptr);
	auto deadline = Clock::now() + std::chrono::milliseconds(limits_.connect_ms);
	for (;;) {
		if (stop_.load() || Clock::now() >= deadline) {
			break;
		}
		auto status = PQconnectPoll(conn_.get());
		if (status == PGRES_POLLING_OK) {
			if (PQsetnonblocking(conn_.get(), 1) != 0) {
				break;
			}
			{
				std::lock_guard lock(mutex_);
				diagnostic_.clear();
			}
			state.store(State::connected);
			if (connected_once_) {
				reconnects_.fetch_add(1, std::memory_order_relaxed);
			}
			connected_once_ = true;
			log(LogEvent::lane_ready);
			return true;
		}
		if (status == PGRES_POLLING_FAILED) {
			break;
		}
		if (wait_socket(status == PGRES_POLLING_WRITING, deadline, nullptr) != Error::none) {
			break;
		}
	}
	{
		std::lock_guard lock(mutex_);
		diagnostic_ = PQerrorMessage(conn_.get());
		diagnostic_.resize(std::min<std::size_t>(diagnostic_.size(), 4096));
		if (diagnostic_.empty()) {
			diagnostic_ = "Connection timed out or was closed";
		}
	}
	drop();
	return false;
}

Result Lane::receive(Job &job, Clock::time_point deadline, std::size_t &bytes, bool stream_rows) {
	Result out;
	bool final = false;
	bool metadata_set = false;
	std::size_t metadata_bytes = 0;
	auto publish = [&] {
		Result next;
		next.columns = out.columns;
		next.column_types = out.column_types;
		next.affected = out.affected;
		auto block = std::make_shared<const Result>(std::move(out));
		if (!job.stream->publish(std::move(block), job.cancel, stop_, deadline)) {
			const bool cancelled = job.cancel.load() || stop_.load();
			if (sql_confirmed_) {
				job.results.push_back(std::move(next));
			}
			throw Failure(
			    cancelled ? Error::cancelled : Error::timeout,
			    sql_confirmed_
			        ? "SQL completed; stream delivery interrupted and incomplete"
			        : "Stream interrupted before definitive SQL response; outcome may be unknown");
		}
		out = std::move(next);
		bytes = metadata_bytes;
	};
	for (;;) {
		if (stop_.load() || job.cancel.load() || Clock::now() >= deadline) {
			throw Failure(job.cancel.load() || stop_.load() ? Error::cancelled : Error::timeout,
			              "Interrupted after dispatch; outcome may be unknown");
		}
		int flush = PQflush(conn_.get());
		if (flush < 0 || !PQconsumeInput(conn_.get())) {
			throw Failure(Error::connection, "Connection lost after dispatch");
		}
		if (PQisBusy(conn_.get())) {
			const auto cause = wait_socket(flush == 1, deadline, &job);
			if (cause != Error::none) {
				throw Failure(cause, cause == Error::connection
				                         ? "Interrupted connection"
				                         : "Interrupted after dispatch; outcome may be unknown");
			}
			continue;
		}
		PgResult r(PQgetResult(conn_.get()));
		if (!r) {
			break;
		}
		auto status = PQresultStatus(r.get());
		rows_received_.fetch_add(static_cast<std::uint64_t>(PQntuples(r.get())),
		                         std::memory_order_relaxed);
		if (status == PGRES_COPY_IN || status == PGRES_COPY_OUT || status == PGRES_COPY_BOTH) {
			throw Failure(Error::invalid, "COPY protocol is unsupported");
		}
		if (status != PGRES_SINGLE_TUPLE && status != PGRES_TUPLES_OK &&
		    status != PGRES_COMMAND_OK) {
			out.sqlstate = field(r.get(), PG_DIAG_SQLSTATE);
			out.message = field(r.get(), PG_DIAG_MESSAGE_PRIMARY);
			if (out.message.empty()) {
				out.message = "PostgreSQL rejected command";
			}
			final = true;
			continue;
		}
		if (!metadata_set) {
			for (int c = 0; c < PQnfields(r.get()); ++c) {
				std::string name = PQfname(r.get(), c);
				bytes += name.size() + sizeof(std::string) + sizeof(std::uint32_t);
				if (bytes > limits_.result_bytes) {
					throw Failure(Error::limit, "Result memory limit after dispatch");
				}
				out.columns.push_back(std::move(name));
				out.column_types.push_back(PQftype(r.get(), c));
			}
			metadata_bytes = bytes;
			metadata_set = true;
		}
		for (int row = 0; row < PQntuples(r.get()); ++row) {
			if (!stream_rows && out.rows.size() >= limits_.result_rows) {
				throw Failure(Error::limit, "Result row limit after dispatch");
			}
			std::vector<std::optional<std::string>> values;
			bytes += sizeof(values) + out.columns.size() * sizeof(std::optional<std::string>);
			if (bytes > limits_.result_bytes) {
				throw Failure(Error::limit, "Result memory limit after dispatch");
			}
			for (int c = 0; c < PQnfields(r.get()); ++c) {
				if (PQgetisnull(r.get(), row, c)) {
					values.emplace_back(std::nullopt);
				} else {
					auto len = static_cast<std::size_t>(PQgetlength(r.get(), row, c));
					bytes += len;
					if (bytes > limits_.result_bytes) {
						throw Failure(Error::limit, "Result memory limit after dispatch");
					}
					values.emplace_back(std::string(PQgetvalue(r.get(), row, c), len));
				}
			}
			out.rows.push_back(std::move(values));
			if (stream_rows && out.rows.size() >= job.stream->chunk_rows) {
				publish();
			}
		}
		if (status != PGRES_SINGLE_TUPLE) {
			final = true;
			auto count = PQcmdTuples(r.get());
			out.affected = *count ? count : "0";
		}
	}
	if (!final || PQstatus(conn_.get()) != CONNECTION_OK) {
		throw Failure(Error::connection, "No definitive PostgreSQL response");
	}
	if (query_dispatched_ && out.message.empty()) {
		sql_confirmed_ = true;
	}
	if (stream_rows) {
		if (out.message.empty() && !out.rows.empty()) {
			// Drain the definitive response before transferring a trailing partial
			// block, so delivery failures do not erase known SQL confirmation.
			publish();
		} else {
			out.rows.clear();
		}
	}
	return out;
}

Result Lane::command(Job &job, const std::string &sql, const Params &params, bool prepared,
                     std::size_t &bytes) {
	auto deadline = Clock::now() + std::chrono::milliseconds(limits_.query_ms);
	if (transaction_) {
		deadline = std::min(deadline, transaction_deadline_);
	}
	std::vector<Oid> types;
	std::vector<const char *> values;
	for (auto &p : params) {
		types.push_back(p.oid);
		values.push_back(p.value ? p.value->c_str() : nullptr);
	}
	if (prepared && (prepared_sql_ != sql || prepared_types_ != types)) {
		// Parse replaces the unnamed statement even if the new SQL is invalid.
		prepared_sql_.clear();
		prepared_types_.clear();
		if (!PQsendPrepare(conn_.get(), "", sql.c_str(), static_cast<int>(types.size()),
		                   types.data())) {
			throw Failure(Error::connection, "Prepare dispatch failed");
		}
		auto r = receive(job, deadline, bytes);
		if (!r.message.empty()) {
			return r;
		}
		prepared_sql_ = sql;
		prepared_types_ = types;
	}
	auto n = static_cast<int>(params.size());
	// Treat a failed dispatch conservatively: libpq may have flushed part of it.
	query_dispatched_ = true;
	int sent = prepared
	               ? PQsendQueryPrepared(conn_.get(), "", n, values.data(), nullptr, nullptr, 0)
	               : PQsendQueryParams(conn_.get(), sql.c_str(), n, types.data(), values.data(),
	                                   nullptr, nullptr, 0);
	// An unnamed Parse from direct mode replaces the unnamed prepared statement.
	if (!prepared) {
		prepared_sql_.clear();
		prepared_types_.clear();
	}
	if (!sent) {
		throw Failure(Error::connection, "Query dispatch failed; outcome cannot be guaranteed");
	}
	sql_commands_.fetch_add(1, std::memory_order_relaxed);
	if (!PQsetSingleRowMode(conn_.get())) {
		throw Failure(Error::internal, "Cannot enable bounded result retrieval after dispatch");
	}
	auto result = receive(job, deadline, bytes, job.stream != nullptr);
	// A changed result shape or a removed server plan requires a fresh Parse on
	// the next explicit execution. Preserve this failure; never replay the job.
	if (prepared && (result.sqlstate == "0A000" || result.sqlstate == "26000")) {
		prepared_sql_.clear();
		prepared_types_.clear();
	}
	return result;
}

void Lane::execute(Job &job) {
	if (!conn_ || PQstatus(conn_.get()) != CONNECTION_OK) {
		throw Failure(Error::not_ready, "Physical connection unavailable");
	}
	if (job.kind != Kind::begin && job.transaction != transaction_) {
		throw Failure(Error::transaction, "Transaction no longer owns this session");
	}
	if (transaction_ && Clock::now() >= transaction_deadline_) {
		drop();
		throw Failure(Error::transaction, "Transaction expired; session closed");
	}
	std::size_t bytes = 0;
	if (job.kind == Kind::execute) {
		if (transaction_failed_) {
			throw Failure(Error::transaction, "Transaction requires rollback");
		}
		// Capture each named file once per job, before dispatching any of its operations.
		// This local index is bounded and is destroyed with the worker's execution scope.
		std::map<std::string, SqlSections> documents;
		std::map<std::pair<std::string, std::string>, std::string> selected;
		std::size_t captured_bytes = 0;
		auto section_sql = [&](const std::string &filename,
		                       const std::string &section) -> const std::string & {
			if (job.cancel.load() || stop_.load()) {
				throw Failure(Error::cancelled, "Cancelled before SQL section capture");
			}
			const auto setting = PQparameterStatus(conn_.get(), "standard_conforming_strings");
			if (!setting || std::string_view(setting) != "on") {
				throw Failure(Error::invalid,
				              sql_file_label(filename) + ": section '" + section +
				                  "': SQL sections require standard_conforming_strings=on");
			}
			auto key = std::make_pair(filename, section);
			if (auto found = selected.find(key); found != selected.end()) {
				return found->second;
			}
			auto charge = [&](std::size_t size) {
				if (size > limits_.batch_bytes - captured_bytes) {
					throw Failure(
					    Error::limit,
					    sql_file_label(filename) + ": section '" + section +
					        "': SQL section capture exceeds batch byte limit; split the batch");
				}
				captured_bytes += size;
			};
			auto document = documents.find(filename);
			if (document == documents.end()) {
				auto text = read_sql_contents(sql_root_, filename, limits_.sql_bytes);
				charge(text.size());
				document =
				    documents.emplace(filename, parse_sql_sections(std::move(text), filename))
				        .first;
			}
			auto sql = document->second.select(section);
			charge(sql.size());
			try {
				validate_sql(sql);
			} catch (const Failure &failure) {
				throw Failure(failure.code, sql_file_label(filename) + ": section '" + section +
				                                "': " + failure.detail);
			}
			return selected.emplace(std::move(key), std::move(sql)).first->second;
		};
		if (!job.operations.empty()) {
			for (std::size_t index = 0; index < job.operations.size(); ++index) {
				const auto &operation = job.operations[index];
				if (operation.source == SqlSourceKind::section) {
					job.error_index = static_cast<int>(index);
					section_sql(operation.sql, operation.section);
				}
			}
		} else if (job.source == SqlSourceKind::section) {
			job.error_index = 0;
			section_sql(job.sql, job.section);
		}
		auto item = [&](std::size_t index, const std::string &sql, const Params &params,
		                bool prepared) {
			job.error_index = static_cast<int>(index);
			query_dispatched_ = false;
			sql_confirmed_ = false;
			if (job.cancel.load() || stop_.load()) {
				throw Failure(Error::cancelled, "Batch cancelled between statements");
			}
			validate_sql(sql);
			auto r = command(job, sql, params, prepared, bytes);
			bool failed = !r.message.empty();
			job.results.push_back(std::move(r));
			if (failed) {
				transaction_failed_ = transaction_ != 0;
				throw Failure(Error::sql, "PostgreSQL rejected batch item");
			}
			job.items_status[index] = ItemStatus::confirmed;
			++job.completed;
			auto transaction_status = PQtransactionStatus(conn_.get());
			if ((!transaction_ && transaction_status != PQTRANS_IDLE) ||
			    (transaction_ && transaction_status != PQTRANS_INTRANS)) {
				drop();
				throw Failure(Error::transaction,
				              "SQL changed transaction state outside the transaction API");
			}
		};
		if (!job.operations.empty()) {
			for (std::size_t index = 0; index < job.operations.size(); ++index) {
				job.error_index = static_cast<int>(index);
				query_dispatched_ = false;
				sql_confirmed_ = false;
				const auto &operation = job.operations[index];
				if (operation.source == SqlSourceKind::section) {
					item(index, section_sql(operation.sql, operation.section), operation.params,
					     operation.prepared);
				} else {
					auto sql = operation.source == SqlSourceKind::file
					               ? read_sql(sql_root_, operation.sql, limits_.sql_bytes)
					               : operation.sql;
					item(index, sql, operation.params, operation.prepared);
				}
			}
		} else {
			job.error_index = 0;
			auto sql = job.source != SqlSourceKind::section
			               ? (job.source == SqlSourceKind::file
			                      ? read_sql(sql_root_, job.sql, limits_.sql_bytes)
			                      : job.sql)
			               : std::string{};
			const auto &captured =
			    job.source != SqlSourceKind::section ? sql : section_sql(job.sql, job.section);
			for (std::size_t index = 0; index < job.batch.size(); ++index) {
				item(index, captured, job.batch[index], job.prepared);
			}
		}
		job.error_index = -1;
	} else {
		job.error_index = 0;
		bool reject_commit = job.kind == Kind::commit && transaction_failed_;
		std::string sql;
		switch (job.kind) {
		case Kind::begin:
			if (transaction_) {
				throw Failure(Error::transaction, "Session already owns a transaction");
			}
			switch (job.isolation) {
			case Isolation::server_default:
				sql = "BEGIN";
				break;
			case Isolation::read_committed:
				sql = "BEGIN ISOLATION LEVEL READ COMMITTED";
				break;
			case Isolation::repeatable_read:
				sql = "BEGIN ISOLATION LEVEL REPEATABLE READ";
				break;
			case Isolation::serializable:
				sql = "BEGIN ISOLATION LEVEL SERIALIZABLE";
				break;
			default:
				throw Failure(Error::invalid, "Invalid transaction isolation");
			}
			if (job.isolation != Isolation::server_default) {
				sql += job.read_only ? " READ ONLY" : " READ WRITE";
			}
			break;
		case Kind::commit:
			sql = reject_commit ? "ROLLBACK" : "COMMIT";
			break;
		case Kind::rollback:
			sql = "ROLLBACK";
			break;
		case Kind::savepoint:
		case Kind::release_savepoint:
			if (transaction_failed_) {
				throw Failure(Error::transaction, "Transaction requires rollback");
			}
			[[fallthrough]];
		case Kind::rollback_to:
			if (!valid_savepoint(job.savepoint)) {
				throw Failure(Error::invalid, "Invalid savepoint name");
			}
			sql = job.kind == Kind::savepoint     ? "SAVEPOINT "
			      : job.kind == Kind::rollback_to ? "ROLLBACK TO SAVEPOINT "
			                                      : "RELEASE SAVEPOINT ";
			sql += '"' + job.savepoint + '"';
			break;
		default:
			throw Failure(Error::invalid, "Invalid transaction command");
		}
		auto r = command(job, sql, {}, false, bytes);
		bool failed = !r.message.empty();
		job.results.push_back(std::move(r));
		if (failed) {
			if (job.kind == Kind::commit || job.kind == Kind::rollback) {
				// A rejected COMMIT (for example a deferred constraint) can end the
				// server transaction. Reconcile worker ownership before poll releases
				// the logical reservation; preserve the original SQL failure, never retry.
				if (PQtransactionStatus(conn_.get()) == PQTRANS_IDLE) {
					transaction_ = 0;
					transaction_failed_ = false;
				} else {
					drop();
				}
			}
			throw Failure(Error::sql, "Transaction command failed");
		}
		if (job.kind == Kind::begin) {
			transaction_ = job.id;
			transaction_failed_ = false;
			transaction_deadline_ =
			    Clock::now() + std::chrono::milliseconds(limits_.transaction_ms);
		} else if (job.kind == Kind::commit || job.kind == Kind::rollback) {
			transaction_ = 0;
			transaction_failed_ = false;
		} else if (job.kind == Kind::rollback_to) {
			transaction_failed_ = false;
		}
		if (reject_commit) {
			// The confirmed ROLLBACK does not confirm the requested COMMIT.
			sql_confirmed_ = false;
			query_dispatched_ = false;
			throw Failure(Error::transaction, "Failed transaction rolled back; COMMIT not sent");
		}
		if (transaction_ && PQtransactionStatus(conn_.get()) != PQTRANS_INTRANS) {
			drop();
			throw Failure(Error::transaction, "Transaction command left an invalid session state");
		}
		job.items_status[0] = ItemStatus::confirmed;
		++job.completed;
		job.error_index = -1;
	}
}

void Lane::run() {
	bool connected = false;
	auto recover = [&] {
		for (int attempt = 0; attempt <= limits_.reconnect_attempts && !stop_.load(); ++attempt) {
			if (attempt) {
				log(LogEvent::lane_retry, static_cast<std::uint64_t>(attempt));
				std::unique_lock lock(mutex_);
				cv_.wait_for(lock,
				             std::chrono::milliseconds(limits_.reconnect_base_ms *
				                                       (1 << std::min(attempt - 1, 10))),
				             [&] {
					             return stop_.load();
				             });
			}
			if (connect()) {
				return true;
			}
		}
		if (!stop_.load()) {
			state.store(State::failed);
			log(LogEvent::lane_failed);
		}
		return false;
	};
	try {
		connected = recover();
		for (;;) {
			std::shared_ptr<Job> job;
			{
				std::unique_lock lock(mutex_);
				cv_.wait_for(lock, std::chrono::milliseconds(20), [&] {
					return stop_.load() || !queue_.empty();
				});
				if (queue_.empty()) {
					if (stop_.load()) {
						break;
					}
					if (transaction_ && Clock::now() >= transaction_deadline_) {
						log(LogEvent::transaction_expired);
						drop();
						connected = false;
					}
					if (!connected && state.load() != State::failed) {
						lock.unlock();
						state.store(State::reconnecting);
						connected = recover();
					}
					continue;
				}
				job = std::move(queue_.front());
				queue_.pop_front();
				queued_jobs_.fetch_sub(1, std::memory_order_relaxed);
			}
			auto start = Clock::now();
			job->queue_us =
			    std::chrono::duration_cast<std::chrono::microseconds>(start - job->accepted_at)
			        .count();
			if (stop_.load() || job->cancel.load()) {
				if (job->transaction && job->transaction == transaction_) {
					transaction_failed_ = true;
					if (job->kind == Kind::commit || job->kind == Kind::rollback) {
						drop();
						connected = false;
						state.store(State::reconnecting);
					}
				}
				job->error = Error::cancelled;
				job->message = "Cancelled before dispatch";
				std::fill(job->items_status.begin(), job->items_status.end(),
				          ItemStatus::cancelled);
				record_completion(*job, Status::cancelled);
				job->completed_at = start;
				job->status.store(Status::cancelled);
				completed_.push(job);
				continue;
			}
			query_dispatched_ = false;
			sql_confirmed_ = false;
			running_jobs_.store(1, std::memory_order_relaxed);
			job->status.store(Status::running);
			if (diagnostics_) {
				auto record = job_record(LogEvent::job_running, *job);
				record.status = Status::running;
				record.queue_us = job->queue_us;
				diagnostics_->emit(record);
			}
			Status status = Status::confirmed;
			try {
				execute(*job);
			} catch (const Failure &e) {
				job->error = e.code;
				job->message = e.detail;
				const bool interrupted = e.code == Error::connection || e.code == Error::timeout ||
				                         e.code == Error::limit || e.code == Error::cancelled ||
				                         e.code == Error::internal;
				const bool uncertain = interrupted && query_dispatched_ && !sql_confirmed_;
				status = uncertain                                       ? Status::uncertain
				         : e.code == Error::cancelled && !sql_confirmed_ ? Status::cancelled
				                                                         : Status::failed;
				if (job->error_index >= 0 &&
				    static_cast<std::size_t>(job->error_index) < job->items_status.size()) {
					auto &item = job->items_status[static_cast<std::size_t>(job->error_index)];
					if (item != ItemStatus::confirmed) {
						item = sql_confirmed_                ? ItemStatus::confirmed
						       : uncertain                   ? ItemStatus::uncertain
						       : status == Status::cancelled ? ItemStatus::cancelled
						                                     : ItemStatus::failed;
						if (sql_confirmed_) {
							++job->completed;
						}
					}
				}
				if (transaction_) {
					transaction_failed_ = true;
				}
				if (uncertain || e.code == Error::connection ||
				    (!sql_confirmed_ && (e.code == Error::timeout || e.code == Error::cancelled))) {
					drop();
					connected = false;
				}
				if (!conn_ || PQstatus(conn_.get()) != CONNECTION_OK) {
					drop();
					connected = false;
				}
			} catch (...) {
				job->error = Error::internal;
				job->message = "Worker failure";
				status = query_dispatched_ && !sql_confirmed_ ? Status::uncertain : Status::failed;
				if (job->error_index >= 0 &&
				    static_cast<std::size_t>(job->error_index) < job->items_status.size()) {
					auto &item = job->items_status[static_cast<std::size_t>(job->error_index)];
					if (item != ItemStatus::confirmed) {
						item = sql_confirmed_                ? ItemStatus::confirmed
						       : status == Status::uncertain ? ItemStatus::uncertain
						                                     : ItemStatus::failed;
						if (sql_confirmed_) {
							++job->completed;
						}
					}
				}
				drop();
				connected = false;
			}
			job->batch.clear();
			job->operations.clear();
			job->sql.clear();
			job->completed_at = Clock::now();
			job->duration_us =
			    std::chrono::duration_cast<std::chrono::microseconds>(job->completed_at - start)
			        .count();
			running_jobs_.store(0, std::memory_order_relaxed);
			record_completion(*job, status);
			if (!connected && !stop_.load()) {
				state.store(State::reconnecting);
			}
			job->status.store(status);
			completed_.push(job);
			if (!connected && !stop_.load()) {
				state.store(State::reconnecting);
				// Existing queued jobs must not silently execute in a new session.
				std::deque<std::shared_ptr<Job>> failed;
				{
					std::lock_guard lock(mutex_);
					failed.swap(queue_);
					queued_jobs_.fetch_sub(failed.size(), std::memory_order_relaxed);
				}
				for (auto &pending : failed) {
					pending->error = Error::connection;
					pending->message = "Not dispatched: session interrupted";
					pending->completed_at = Clock::now();
					pending->queue_us = std::chrono::duration_cast<std::chrono::microseconds>(
					                        pending->completed_at - pending->accepted_at)
					                        .count();
					record_completion(*pending, Status::failed);
					pending->status.store(Status::failed);
					completed_.push(pending);
				}
				connected = recover();
			}
		}
	} catch (...) {
		state.store(State::failed);
		log(LogEvent::worker_failed);
	}
	drop();
	std::deque<std::shared_ptr<Job>> abandoned;
	{
		std::lock_guard lock(mutex_);
		abandoned.swap(queue_);
		queued_jobs_.fetch_sub(abandoned.size(), std::memory_order_relaxed);
	}
	for (auto &job : abandoned) {
		job->error = Error::cancelled;
		job->message = "Worker stopped before dispatch";
		std::fill(job->items_status.begin(), job->items_status.end(), ItemStatus::cancelled);
		job->completed_at = Clock::now();
		job->queue_us = std::chrono::duration_cast<std::chrono::microseconds>(job->completed_at -
		                                                                      job->accepted_at)
		                    .count();
		record_completion(*job, Status::cancelled);
		job->status.store(Status::cancelled);
		completed_.push(job);
	}
	state.store(State::disconnected);
	log(LogEvent::lane_stopped);
	stopped.store(true);
}

} // namespace pg
