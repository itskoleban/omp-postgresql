#include "pg/engine.hpp"
#include "pg/stream.hpp"
#include <algorithm>
#include <limits>

namespace pg {
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

Engine::Engine(Limits limits, std::filesystem::path root)
    : limits_(limits), root_(std::move(root)) {
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
	if (next_ == std::numeric_limits<Id>::max()) {
		throw Failure(Error::limit, "Handle space exhausted");
	}
	return next_++;
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
	c->owner = owner;
	c->reservations.resize(size);
	for (int n = 0; n < size; ++n) {
		c->lanes.push_back(std::make_unique<Lane>(profile, limits_, root_, completed_));
	}
	auto handle = id();
	connections_.emplace(handle, std::move(c));
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
	Statement s{owner, conn, std::move(sql), file, persistent, {}, {}, {}, 0};
	s.bindings.resize(parameters);
	auto handle = id();
	statements_.emplace(handle, std::move(s));
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

Id Engine::execute(Owner owner, Id handle, int lane, Id tx, int context, bool notify) {
	return execute_statement(owner, handle, lane, tx, context, notify, {});
}

Id Engine::execute_stream(Owner owner, Id handle, int chunk_rows, int lane, Id tx, int context,
                          bool notify) {
	if (chunk_rows < 1 || chunk_rows > 1024) {
		throw Failure(Error::invalid, "Stream block size must be 1..1024 rows");
	}
	if (statement(owner, handle).batch.size() != 1) {
		throw Failure(Error::invalid, "Streaming requires exactly one added execution");
	}
	return execute_statement(owner, handle, lane, tx, context, notify,
	                         std::make_shared<Stream>(static_cast<std::size_t>(chunk_rows)));
}

Id Engine::execute_statement(Owner owner, Id handle, int lane, Id tx, int context, bool notify,
                             std::shared_ptr<Stream> stream) {
	auto &s = statement(owner, handle);
	if (s.batch.empty()) {
		throw Failure(Error::invalid, "Cannot execute empty batch; call pg_add first");
	}
	lane = transaction_lane(owner, s.connection, tx, lane);
	auto j = std::make_shared<Job>();
	j->owner = owner;
	j->connection = s.connection;
	j->lane = lane;
	j->transaction = tx;
	j->context = context;
	j->notify = notify;
	j->stream = std::move(stream);
	j->sql = s.sql;
	j->file = s.file;
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

Id Engine::query(Owner owner, Id conn, std::string sql, int lane, Id tx, int context, bool notify) {
	if (sql.size() > limits_.sql_bytes) {
		throw Failure(Error::limit, "SQL exceeds size limit");
	}
	validate_sql(sql);
	auto j = std::make_shared<Job>();
	j->owner = owner;
	j->connection = conn;
	j->lane = transaction_lane(owner, conn, tx, lane);
	j->transaction = tx;
	j->context = context;
	j->notify = notify;
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
	const auto overhead = source.sql.size() + sizeof(Operation);
	if (added_bytes > limits_.batch_bytes - destination.bytes ||
	    overhead > (limits_.batch_bytes - destination.bytes - added_bytes) / source.batch.size()) {
		throw Failure(Error::limit, "Batch memory limit exceeded");
	}
	added_bytes += overhead * source.batch.size();
	// Allocate and copy before consuming anything: rejection preserves both handles.
	std::vector<Operation> appended;
	appended.reserve(source.batch.size());
	for (const auto &params : source.batch) {
		appended.push_back({source.sql, source.file, source.prepared, params});
	}
	destination.operations.reserve(destination.operations.size() + appended.size());
	for (auto &operation : appended) {
		destination.operations.push_back(std::move(operation));
	}
	destination.bytes += added_bytes;
	source.batch.clear();
	source.bytes = 0;
}

Id Engine::execute_batch(Owner owner, Id handle, int lane, Id tx, int context, bool notify) {
	auto &source = batch(owner, handle);
	if (source.operations.empty()) {
		throw Failure(Error::invalid, "Cannot execute an empty batch");
	}
	auto j = std::make_shared<Job>();
	j->owner = owner;
	j->connection = source.connection;
	j->lane = transaction_lane(owner, source.connection, tx, lane);
	j->transaction = tx;
	j->context = context;
	j->notify = notify;
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
                 bool read_only) {
	if (isolation < Isolation::server_default || isolation > Isolation::serializable) {
		throw Failure(Error::invalid, "Invalid transaction isolation level");
	}
	auto j = std::make_shared<Job>();
	j->owner = owner;
	j->connection = conn;
	j->lane = lane;
	j->kind = Kind::begin;
	j->context = context;
	j->notify = notify;
	j->isolation = isolation;
	j->read_only = read_only;
	auto handle = submit(j);
	connection(owner, conn).reservations[lane] = handle;
	return handle;
}

Id Engine::savepoint(Owner owner, Id tx, std::string name, Kind action, int context, bool notify) {
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
	auto j = std::make_shared<Job>();
	j->owner = owner;
	j->connection = b->connection;
	j->lane = transaction_lane(owner, b->connection, tx, b->lane);
	j->transaction = tx;
	j->kind = action;
	j->savepoint = std::move(name);
	j->context = context;
	j->notify = notify;
	return submit(std::move(j));
}

Id Engine::finish(Owner owner, Id tx, bool commit, int context, bool notify) {
	auto b = job(owner, tx);
	if (b->kind != Kind::begin || b->status.load() != Status::confirmed || ending_.contains(tx)) {
		throw Failure(Error::transaction, "Transaction cannot be finished");
	}
	auto j = std::make_shared<Job>();
	j->owner = owner;
	j->connection = b->connection;
	j->lane = b->lane;
	j->transaction = tx;
	j->kind = commit ? Kind::commit : Kind::rollback;
	j->context = context;
	j->notify = notify;
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
}

void Engine::unload(Owner owner) {
	unloaded_.push_back(owner);
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
			j->cancel.store(true);
			j->notify = false;
		}
	}
}

Dispatch Engine::poll() {
	Dispatch out;
	// Keep a closed handle through its final callback. Reap it on the next tick.
	std::erase_if(connections_, [](auto &item) {
		return item.second->close_delivered && item.second->state() == State::disconnected;
	});
	for (auto handle : completed_.drain()) {
		auto it = jobs_.find(handle);
		if (it == jobs_.end()) {
			continue;
		}
		auto &j = it->second;
		j->delivered = true;
		j->delivery_us =
		    std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - j->accepted_at)
		        .count();
		auto c = connections_.find(j->connection);
		if (c != connections_.end() &&
		    ((j->kind == Kind::begin && j->status.load() != Status::confirmed) ||
		     j->kind == Kind::commit || j->kind == Kind::rollback)) {
			c->second->reservations[j->lane] = 0;
			ending_.erase(j->transaction);
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
		return terminal(item.second->status.load()) &&
		       std::find(unloaded_.begin(), unloaded_.end(), item.second->owner) != unloaded_.end();
	});
	// Owner generations never repeat. Remove tombstones once every resource is gone.
	std::erase_if(unloaded_, [&](Owner owner) {
		return std::none_of(jobs_.begin(), jobs_.end(),
		                    [=](auto &p) {
			                    return p.second->owner == owner;
		                    }) &&
		       std::none_of(connections_.begin(), connections_.end(), [=](auto &p) {
			       return p.second->owner == owner;
		       });
	});
	return out;
}
} // namespace pg
