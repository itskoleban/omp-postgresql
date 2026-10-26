#include "pg/engine.hpp"
#include "pg/sql_sections.hpp"
#include "pg/stream.hpp"
#include "network_trace.hpp"
#include <algorithm>
#include <new>

// Only the focused allocation regression replaces these reserve checkpoints.
#ifndef PG_POLL_ALLOCATION_CHECKPOINT
#define PG_POLL_ALLOCATION_CHECKPOINT() ((void)0)
#endif

namespace pg {

namespace {
std::shared_ptr<Job> make_job(Owner owner, Id connection, int lane, Id transaction, int context,
                              bool notify, std::unique_ptr<CallbackDescriptor> callback) {
	auto job = std::make_shared<Job>();
	job->owner = owner;
	job->connection = connection;
	job->lane = lane;
	job->transaction = transaction;
	job->context = context;
	job->notify = notify;
	job->callback = notify ? std::move(callback) : nullptr;
	return job;
}
} // namespace

int Connection::ready() const {
	int n = 0;
	for (auto &lane : lanes) {
		if (lane->state.load() == State::connected) {
			++n;
		}
	}
	return n;
}

State Connection::state() const {
	if (closing) {
		return ready() == 0 && std::all_of(lanes.begin(), lanes.end(),
		                                   [](auto &l) {
			                                   return l->stopped.load();
		                                   })
		           ? State::disconnected
				   : State::closing;
	}
	bool reconnecting = false;
	for (auto &lane : lanes) {
		auto s = lane->state.load();
		if (s == State::failed || s == State::disconnected) {
			return State::failed;
		}
		if (s == State::reconnecting) {
			reconnecting = true;
		}
	}
	if (ready() == static_cast<int>(lanes.size())) {
		return State::connected;
	}
	return reconnecting ? State::reconnecting : State::connecting;
}

Engine::Engine(Limits limits, std::filesystem::path root, LogLevel log_level)
    : limits_(limits), root_(std::move(root)), diagnostics_(log_level) {
	if (!limits.jobs || !limits.connections || !limits.lanes || limits.connect_ms <= 0 ||
	    limits.query_ms <= 0 || limits.transaction_ms <= 0 || limits.reconnect_attempts < 0 ||
	    limits.reconnect_attempts > 10 || limits.reconnect_base_ms < 1 ||
	    limits.reconnect_base_ms > 60000) {
		throw Failure(Error::invalid, "Invalid engine limits");
	}
}

Engine::~Engine() {
	for (auto &[id, c] : connections_) {
		for (auto &lane : c->lanes) {
			lane->close();
		}
	}
	connections_.clear();
}

Id Engine::id() {
	return detail::take_handle(next_);
}

Id Engine::connect(Owner owner, std::string profile, int size) {
	if (connections_.size() >= limits_.connections || size < 1 ||
	    static_cast<std::size_t>(size) > limits_.lanes) {
		throw Failure(Error::limit, "Connection or pool limit");
	}
	if (profile.empty() || profile.size() > 128 ||
	    profile.find_first_not_of(
	        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
	        std::string::npos) {
		throw Failure(Error::invalid, "Expected libpq service profile name");
	}
	auto c = std::make_unique<Connection>();
	auto handle = id();
	c->owner = owner;
	c->reservations.resize(size);
	for (int n = 0; n < size; ++n) {
		c->lanes.push_back(std::make_unique<Lane>(profile, limits_, root_, completed_,
		                                          &diagnostics_, owner, handle, n));
	}
	connections_.emplace(handle, std::move(c));
	diagnostics_.emit({LogEvent::connection_created, owner, handle});
	return handle;
}

Connection &Engine::connection(Owner owner, Id handle) {
	auto it = connections_.find(handle);
	if (it == connections_.end() || it->second->owner != owner) {
		throw Failure(Error::invalid, "Invalid connection handle or owner");
	}
	return *it->second;
}

void Engine::disconnect(Owner owner, Id handle) {
	auto &c = connection(owner, handle);
	c.closing = true;
	diagnostics_.emit({LogEvent::connection_closing, owner, handle});
	for (auto &l : c.lanes) {
		l->close();
	}
}

Id Engine::prepare(Owner owner, Id conn, std::string sql, int parameters, bool file,
                   bool persistent) {
	connection(owner, conn);
	if (statements_.size() + batches_.size() >= limits_.statements ||
	    sql.size() > limits_.sql_bytes) {
		throw Failure(Error::limit, "Statement limit");
	}
	if (parameters < 0 || parameters > 64) {
		throw Failure(Error::invalid, "Parameter count must be 0..64");
	}
	if (!file) {
		validate_sql(sql);
	} else if (sql.empty() || sql.size() > 1024) {
		throw Failure(Error::file, "Invalid SQL filename");
	}
	Statement s{owner,
	            conn,
	            std::move(sql),
	            file ? SqlSourceKind::file : SqlSourceKind::inline_sql,
	            persistent,
	            {},
	            {},
	            {},
	            0,
	            {}};
	s.bindings.resize(parameters);
	auto handle = id();
	statements_.emplace(handle, std::move(s));
	return handle;
}

Id Engine::prepare_file(Owner owner, Id conn, std::string filename, int parameters, bool persistent,
                        std::string section) {
	if (!section.empty() && !valid_section_name(section)) {
		throw Failure(Error::invalid, "Invalid SQL section name (ASCII identifier, 1..64 bytes)");
	}
	auto handle = prepare(owner, conn, std::move(filename), parameters, true, persistent);
	auto &statement = statements_.at(handle);
	statement.source = section.empty() ? SqlSourceKind::file : SqlSourceKind::section;
	statement.section = std::move(section);
	return handle;
}

Statement &Engine::statement(Owner owner, Id handle) {
	auto it = statements_.find(handle);
	if (it == statements_.end() || it->second.owner != owner) {
		throw Failure(Error::invalid, "Invalid statement handle or owner");
	}
	return it->second;
}

void Engine::close_statement(Owner owner, Id handle) {
	statement(owner, handle);
	statements_.erase(handle);
}

Id Engine::submit(std::shared_ptr<Job> j) {
	auto &c = connection(j->owner, j->connection);
	if (c.state() != State::connected) {
		throw Failure(Error::not_ready, "All pool channels must be ready");
	}
	if (j->lane < 0 || static_cast<std::size_t>(j->lane) >= c.lanes.size()) {
		throw Failure(Error::invalid, "Channel out of range");
	}
	std::size_t finishing_slots = 0;
	for (const auto &[handle, connection] : connections_) {
		for (auto transaction : connection->reservations) {
			if (transaction && !ending_.contains(transaction)) {
				++finishing_slots;
			}
		}
	}
	// Every live BEGIN reserves capacity for its eventual COMMIT/ROLLBACK.
	// Other jobs cannot consume that capacity, even when all retained jobs are BEGINs.
	auto new_slots = j->kind == Kind::begin ? 2u : 1u;
	if (jobs_.size() + finishing_slots + new_slots > limits_.jobs) {
		auto record = job_record(LogEvent::admission_rejected, *j);
		record.error = Error::limit;
		diagnostics_.emit(record);
		throw Failure(Error::limit, "Job/result limit (including transaction finish reservations)");
	}
	auto reserved = c.reservations[j->lane];
	if (j->kind == Kind::begin ? reserved != 0 : reserved != j->transaction) {
		throw Failure(Error::transaction, "Channel is reserved for another transaction");
	}
	if (j->transaction && j->kind != Kind::commit && j->kind != Kind::rollback &&
	    ending_.contains(j->transaction)) {
		throw Failure(Error::transaction, "Transaction is ending");
	}
	j->id = id();
	j->accepted_at = Clock::now();
	PG_NETWORK_TRACE("submit", j.get(), c.lanes[j->lane].get(), nullptr, {}, 0, 0, j->accepted_at);
	j->items_status.assign(j->kind == Kind::execute
	                           ? (j->operations.empty() ? j->batch.size() : j->operations.size())
							   : 1,
	                       ItemStatus::not_executed);
	jobs_.emplace(j->id, j);
	try {
		c.lanes[j->lane]->enqueue(j);
	} catch (...) {
		jobs_.erase(j->id);
		throw;
	}
	return j->id;
}

int Engine::transaction_lane(Owner owner, Id connection, Id tx, int lane) {
	if (tx) {
		auto begin_job = job(owner, tx);
		if (begin_job->kind != Kind::begin || begin_job->status.load() != Status::confirmed ||
		    begin_job->connection != connection) {
			throw Failure(Error::transaction, "BEGIN has not succeeded on this connection");
		}
		lane = begin_job->lane;
	}
	return lane;
}

Id Engine::execute(Owner owner, Id handle, int lane, Id tx, int context, bool notify,
                   std::unique_ptr<CallbackDescriptor> callback) {
	return execute_statement(owner, handle, lane, tx, context, notify, {}, std::move(callback));
}

Id Engine::execute_stream(Owner owner, Id handle, int chunk_rows, int lane, Id tx, int context,
                          bool notify, std::unique_ptr<CallbackDescriptor> callback) {
	if (chunk_rows < 1 || chunk_rows > 1024) {
		throw Failure(Error::invalid, "Stream block size must be 1..1024 rows");
	}
	if (statement(owner, handle).batch.size() != 1) {
		throw Failure(Error::invalid, "Streaming requires exactly one added execution");
	}
	return execute_statement(owner, handle, lane, tx, context, notify,
	                         std::make_shared<Stream>(static_cast<std::size_t>(chunk_rows)),
	                         std::move(callback));
}

Id Engine::execute_statement(Owner owner, Id handle, int lane, Id tx, int context, bool notify,
                             std::shared_ptr<Stream> stream,
                             std::unique_ptr<CallbackDescriptor> callback) {
	auto &s = statement(owner, handle);
	if (s.batch.empty()) {
		throw Failure(Error::invalid, "Cannot execute empty batch; call pg_add first");
	}
	lane = transaction_lane(owner, s.connection, tx, lane);
	auto j = make_job(owner, s.connection, lane, tx, context, notify, std::move(callback));
	j->stream = std::move(stream);
	j->sql = s.sql;
	j->source = s.source;
	j->section = s.section;
	j->prepared = s.prepared;
	j->batch = std::move(s.batch);
	Id handle_job;
	try {
		handle_job = submit(j);
	} catch (...) {
		// Queue rejection is local: preserve the exact snapshots for a later
		// enqueue attempt, without duplicating the entire batch on the main thread.
		s.batch = std::move(j->batch);
		throw;
	}
	s.bytes = 0;
	return handle_job;
}

Id Engine::query(Owner owner, Id conn, std::string sql, int lane, Id tx, int context, bool notify,
                 std::unique_ptr<CallbackDescriptor> callback) {
	PG_NETWORK_TRACE("query_enter", nullptr, nullptr, nullptr, {}, int(conn), network_trace::hash(sql));
	if (sql.size() > limits_.sql_bytes) {
		throw Failure(Error::limit, "SQL exceeds size limit");
	}
	validate_sql(sql);
	auto j = make_job(owner, conn, transaction_lane(owner, conn, tx, lane), tx, context, notify,
	                  std::move(callback));
	j->sql = std::move(sql);
	j->prepared = false;
	j->batch.emplace_back();
	return submit(std::move(j));
}

Batch &Engine::batch(Owner owner, Id handle) {
	auto it = batches_.find(handle);
	if (it == batches_.end() || it->second.owner != owner) {
		throw Failure(Error::invalid, "Invalid batch handle or owner");
	}
	return it->second;
}

Id Engine::create_batch(Owner owner, Id conn) {
	connection(owner, conn);
	if (statements_.size() + batches_.size() >= limits_.statements) {
		throw Failure(Error::limit, "Statement and batch handle limit");
	}
	auto handle = id();
	batches_.emplace(handle, Batch{owner, conn, {}, 0});
	return handle;
}

void Engine::add_batch(Owner owner, Id handle, Id statement_handle) {
	auto &destination = batch(owner, handle);
	auto &source = statement(owner, statement_handle);
	if (source.connection != destination.connection || source.batch.empty()) {
		throw Failure(Error::invalid, "Batch needs added executions on the same connection");
	}
	if (source.batch.size() > limits_.batch_count - destination.operations.size()) {
		throw Failure(Error::limit, "Batch item limit exceeded");
	}
	std::size_t added_bytes = source.bytes;
	const auto overhead = source.sql.size() + source.section.size() + sizeof(Operation);
	if (added_bytes > limits_.batch_bytes - destination.bytes ||
	    overhead > (limits_.batch_bytes - destination.bytes - added_bytes) / source.batch.size()) {
		throw Failure(Error::limit, "Batch memory limit exceeded");
	}
	added_bytes += overhead * source.batch.size();
	// Allocate and copy before consuming anything: rejection preserves both handles.
	std::vector<Operation> appended;
	appended.reserve(source.batch.size());
	for (const auto &params : source.batch) {
		appended.push_back({source.sql, source.source, source.prepared, params, source.section});
	}
	destination.operations.reserve(destination.operations.size() + appended.size());
	for (auto &operation : appended) {
		destination.operations.push_back(std::move(operation));
	}
	destination.bytes += added_bytes;
	source.batch.clear();
	source.bytes = 0;
}

Id Engine::execute_batch(Owner owner, Id handle, int lane, Id tx, int context, bool notify,
                         std::unique_ptr<CallbackDescriptor> callback) {
	auto &source = batch(owner, handle);
	if (source.operations.empty()) {
		throw Failure(Error::invalid, "Cannot execute an empty batch");
	}
	auto j =
	    make_job(owner, source.connection, transaction_lane(owner, source.connection, tx, lane), tx,
		         context, notify, std::move(callback));
	j->operations = std::move(source.operations);
	Id handle_job;
	try {
		handle_job = submit(j);
	} catch (...) {
		source.operations = std::move(j->operations);
		throw;
	}
	source.bytes = 0;
	return handle_job;
}

void Engine::close_batch(Owner owner, Id handle) {
	batch(owner, handle);
	batches_.erase(handle);
}

Id Engine::begin(Owner owner, Id conn, int lane, int context, bool notify, Isolation isolation,
                 bool read_only, std::unique_ptr<CallbackDescriptor> callback) {
	if (isolation < Isolation::server_default || isolation > Isolation::serializable) {
		throw Failure(Error::invalid, "Invalid transaction isolation level");
	}
	auto j = make_job(owner, conn, lane, 0, context, notify, std::move(callback));
	j->kind = Kind::begin;
	j->isolation = isolation;
	j->read_only = read_only;
	auto handle = submit(j);
	connection(owner, conn).reservations[lane] = handle;
	return handle;
}

Id Engine::savepoint(Owner owner, Id tx, std::string name, Kind action, int context, bool notify,
                     std::unique_ptr<CallbackDescriptor> callback) {
	constexpr auto letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
	if (name.empty() || name.size() > 63 ||
	    std::string_view(letters).find(name[0]) == std::string_view::npos ||
	    name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789") !=
	        std::string::npos ||
	    (action != Kind::savepoint && action != Kind::rollback_to &&
	     action != Kind::release_savepoint)) {
		throw Failure(Error::invalid, "Invalid savepoint name or action");
	}
	auto b = job(owner, tx);
	auto j = make_job(owner, b->connection, transaction_lane(owner, b->connection, tx, b->lane), tx,
	                  context, notify, std::move(callback));
	j->kind = action;
	j->savepoint = std::move(name);
	return submit(std::move(j));
}

Id Engine::finish(Owner owner, Id tx, bool commit, int context, bool notify,
                  std::unique_ptr<CallbackDescriptor> callback) {
	auto b = job(owner, tx);
	if (b->kind != Kind::begin || b->status.load() != Status::confirmed || ending_.contains(tx)) {
		throw Failure(Error::transaction, "Transaction cannot be finished");
	}
	auto j = make_job(owner, b->connection, b->lane, tx, context, notify, std::move(callback));
	j->kind = commit ? Kind::commit : Kind::rollback;
	// Allocate the ending marker before accepting a network operation. If queueing
	// fails, restore the reservation; the accepted path cannot fail afterward.
	ending_.emplace(tx, true);
	try {
		return submit(j);
	} catch (...) {
		ending_.erase(tx);
		throw;
	}
}

std::shared_ptr<Job> Engine::job(Owner owner, Id handle) {
	auto it = jobs_.find(handle);
	if (it == jobs_.end() || it->second->owner != owner) {
		throw Failure(Error::invalid, "Invalid job handle or owner");
	}
	return it->second;
}

void Engine::close_job(Owner owner, Id handle) {
	auto j = job(owner, handle);
	if (!terminal(j->status.load()) || !j->delivered) {
		throw Failure(Error::not_ready, "Job completion has not been dispatched yet");
	}
	auto c = connections_.find(j->connection);
	if (c != connections_.end() && j->kind == Kind::begin &&
	    c->second->reservations[j->lane] == handle) {
		throw Failure(Error::transaction, "Release transaction after COMMIT/ROLLBACK completes");
	}
	jobs_.erase(handle);
	diagnostics_.emit(job_record(LogEvent::job_released, *j));
}

void Engine::unload(Owner owner) {
	diagnostics_.emit({LogEvent::owner_unloaded, owner});
	for (auto &[id, c] : connections_) {
		if (c->owner == owner) {
			disconnect(owner, id);
		}
	}
	std::erase_if(statements_, [=](auto &pair) {
		return pair.second.owner == owner;
	});
	std::erase_if(batches_, [=](auto &pair) {
		return pair.second.owner == owner;
	});
	for (auto &[id, j] : jobs_) {
		if (j->owner == owner) {
			j->owner_unloaded = true;
			j->cancel.store(true);
			j->notify = false;
			j->callback.reset();
		}
	}
}

Dispatch Engine::poll() {
	Dispatch out;
	// Registries are main-thread owned. These bounds cover every completion or
	// stream that workers can publish during this poll, and every connection event.
	// Reserve before popping terminal jobs or changing delivery/connection state;
	// an allocation failure leaves all events available for the next tick.
	try {
		PG_POLL_ALLOCATION_CHECKPOINT();
		out.jobs.reserve(jobs_.size());
		PG_POLL_ALLOCATION_CHECKPOINT();
		out.streams.reserve(jobs_.size());
		PG_POLL_ALLOCATION_CHECKPOINT();
		out.connections.reserve(connections_.size());
	} catch (const std::bad_alloc &) {
		return {};
	}
	// Keep a closed handle through its final callback. Reap it on the next tick.
	std::erase_if(connections_, [](auto &item) {
		return item.second->close_delivered && item.second->state() == State::disconnected;
	});
	for (auto completed = completed_.pop(); completed; completed = completed_.pop()) {
		// A closing connection or unloaded job may already have been reaped.
		// Finish bookkeeping belongs to the completion, independently of either registry.
		if (completed->kind == Kind::commit || completed->kind == Kind::rollback) {
			ending_.erase(completed->transaction);
		}
		auto it = jobs_.find(completed->id);
		if (it == jobs_.end()) {
			continue;
		}
		auto &j = it->second;
		j->delivered = true;
		PG_NETWORK_TRACE("poll_delivered", j.get());
		j->delivery_us =
		    std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - j->accepted_at)
		        .count();
		auto record = job_record(
		    j->notify ? LogEvent::completion_polled : LogEvent::notification_suppressed, *j);
		record.status = j->status.load();
		record.error = j->error;
		record.queue_us = j->queue_us;
		record.execution_us = j->duration_us;
		if (diagnostics_.enabled(event_level(record.event))) {
			record.completion_wait_us = std::chrono::duration_cast<std::chrono::microseconds>(
			                                Clock::now() - j->completed_at)
			                                .count();
			diagnostics_.emit(record);
		}
		auto c = connections_.find(j->connection);
		if (c != connections_.end() &&
		    ((j->kind == Kind::begin && j->status.load() != Status::confirmed) ||
		     j->kind == Kind::commit || j->kind == Kind::rollback)) {
			c->second->reservations[j->lane] = 0;
		}
		if (j->notify) {
			out.jobs.push_back(j);
		}
	}
	for (const auto &[handle, j] : jobs_) {
		if (j->notify && j->stream && j->stream->available()) {
			out.streams.push_back(j);
		}
	}
	for (auto &[handle, c] : connections_) {
		auto state = c->state();
		if (state != c->observed ||
		    (c->closing && state == State::disconnected && !c->close_delivered)) {
			c->observed = state;
			out.connections.push_back({c->owner, handle, state});
		}
		if (c->closing && state == State::disconnected) {
			c->close_delivered = true;
		}
	}
	std::erase_if(jobs_, [&](auto &item) {
		const auto &job = item.second;
		if (!terminal(job->status.load()) || !job->owner_unloaded) return false;
		// Terminal status can become visible before its queue publication.
		if (job->kind == Kind::commit || job->kind == Kind::rollback) {
			ending_.erase(job->transaction);
		}
		return true;
	});
	return out;
}

} // namespace pg
