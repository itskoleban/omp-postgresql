#include "pg/session.hpp"
#include "pg/statement.hpp"
#include <algorithm>
#include <limits>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
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
} // namespace

Lane::Lane(std::string service, Limits limits, std::filesystem::path root,
           CompletionQueue &completed)
    : service_(std::move(service)), limits_(limits), sql_root_(std::move(root)),
      completed_(completed), thread_([this] {
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

void Lane::enqueue(std::shared_ptr<Job> job) {
	std::lock_guard lock(mutex_);
	queue_.push_back(std::move(job));
	cv_.notify_one();
}

void Lane::drop() {
	conn_.reset();
	prepared_sql_.clear();
	prepared_types_.clear();
	transaction_ = 0;
	transaction_failed_ = false;
}

bool Lane::wait_socket(bool writing, Clock::time_point deadline, const Job *job) {
	if (stop_.load() || (job && job->cancel.load()) || Clock::now() >= deadline) {
		return false;
	}
	int socket = PQsocket(conn_.get());
	if (socket < 0) {
		return false;
	}
#ifndef _WIN32
	if (socket >= FD_SETSIZE) {
		return false;
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
	int rc =
	    select(socket + 1, writing ? nullptr : &fds, writing ? &fds : nullptr, nullptr, &timeout);
	return rc >= 0;
}

bool Lane::connect() {
	drop();
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
			return true;
		}
		if (status == PGRES_POLLING_FAILED) {
			break;
		}
		if (!wait_socket(status == PGRES_POLLING_WRITING, deadline, nullptr)) {
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

Result Lane::receive(Job &job, Clock::time_point deadline, std::size_t &bytes) {
	Result out;
	bool final = false;
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
			if (!wait_socket(flush == 1, deadline, &job)) {
				throw Failure(Error::connection, "Interrupted connection");
			}
			continue;
		}
		PgResult r(PQgetResult(conn_.get()));
		if (!r) {
			break;
		}
		auto status = PQresultStatus(r.get());
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
		if (out.columns.empty()) {
			for (int c = 0; c < PQnfields(r.get()); ++c) {
				std::string name = PQfname(r.get(), c);
				bytes += name.size() + sizeof(std::string);
				if (bytes > limits_.result_bytes) {
					throw Failure(Error::limit, "Result memory limit after dispatch");
				}
				out.columns.push_back(std::move(name));
			}
		}
		for (int row = 0; row < PQntuples(r.get()); ++row) {
			if (out.rows.size() >= limits_.result_rows) {
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
	PQsetSingleRowMode(conn_.get());
	auto result = receive(job, deadline, bytes);
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
		auto sql = job.file ? read_sql(sql_root_, job.sql, limits_.sql_bytes) : job.sql;
		validate_sql(sql);
		for (auto &params : job.batch) {
			if (job.cancel.load() || stop_.load()) {
				throw Failure(Error::cancelled, "Batch cancelled between statements");
			}
			auto r = command(job, sql, params, job.prepared, bytes);
			bool failed = !r.message.empty();
			job.results.push_back(std::move(r));
			if (failed) {
				transaction_failed_ = transaction_ != 0;
				throw Failure(Error::sql, "PostgreSQL rejected batch item");
			}
			++job.completed;
			auto transaction_status = PQtransactionStatus(conn_.get());
			if ((!transaction_ && transaction_status != PQTRANS_IDLE) ||
			    (transaction_ && transaction_status != PQTRANS_INTRANS)) {
				drop();
				throw Failure(Error::transaction,
				              "SQL changed transaction state outside the transaction API");
			}
		}
	} else {
		bool reject_commit = job.kind == Kind::commit && transaction_failed_;
		std::string sql = job.kind == Kind::begin                      ? "BEGIN"
		                  : job.kind == Kind::commit && !reject_commit ? "COMMIT"
		                                                               : "ROLLBACK";
		auto r = command(job, sql, {}, false, bytes);
		bool failed = !r.message.empty();
		job.results.push_back(std::move(r));
		if (failed) {
			throw Failure(Error::sql, "Transaction command failed");
		}
		if (job.kind == Kind::begin) {
			transaction_ = job.id;
			transaction_failed_ = false;
			transaction_deadline_ =
			    Clock::now() + std::chrono::milliseconds(limits_.transaction_ms);
		} else {
			transaction_ = 0;
			transaction_failed_ = false;
		}
		if (reject_commit) {
			throw Failure(Error::transaction, "Failed transaction rolled back; COMMIT not sent");
		}
		++job.completed;
	}
}

void Lane::run() {
	bool connected = false;
	auto recover = [&] {
		for (int attempt = 0; attempt <= limits_.reconnect_attempts && !stop_.load(); ++attempt) {
			if (attempt) {
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
			}
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
				job->status.store(Status::cancelled);
				completed_.push(job->id);
				continue;
			}
			job->status.store(Status::running);
			auto start = Clock::now();
			Status status = Status::confirmed;
			try {
				execute(*job);
			} catch (const Failure &e) {
				job->error = e.code;
				job->message = e.detail;
				bool uncertain = e.code == Error::connection || e.code == Error::timeout ||
				                 e.code == Error::limit;
				if (e.code == Error::cancelled) {
					uncertain = true;
				}
				status = uncertain ? Status::uncertain : Status::failed;
				if (transaction_) {
					transaction_failed_ = true;
				}
				if (uncertain) {
					drop();
					connected = false;
				}
				if (!conn_) {
					connected = false;
				}
			} catch (...) {
				job->error = Error::internal;
				job->message = "Worker failure";
				status = Status::uncertain;
				drop();
				connected = false;
			}
			job->batch.clear();
			job->sql.clear();
			job->duration_us =
			    std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
			if (!connected && !stop_.load()) {
				state.store(State::reconnecting);
			}
			job->status.store(status);
			completed_.push(job->id);
			if (!connected && !stop_.load()) {
				state.store(State::reconnecting);
				// Existing queued jobs must not silently execute in a new session.
				std::deque<std::shared_ptr<Job>> failed;
				{
					std::lock_guard lock(mutex_);
					failed.swap(queue_);
				}
				for (auto &pending : failed) {
					pending->error = Error::connection;
					pending->message = "Not dispatched: session interrupted";
					pending->status.store(Status::failed);
					completed_.push(pending->id);
				}
				connected = recover();
			}
		}
	} catch (...) {
		state.store(State::failed);
	}
	drop();
	std::deque<std::shared_ptr<Job>> abandoned;
	{
		std::lock_guard lock(mutex_);
		abandoned.swap(queue_);
	}
	for (auto &job : abandoned) {
		job->error = Error::cancelled;
		job->message = "Worker stopped before dispatch";
		job->status.store(Status::cancelled);
		completed_.push(job->id);
	}
	state.store(State::disconnected);
	stopped.store(true);
}
} // namespace pg
