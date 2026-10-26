#pragma once

#include "session.hpp"
#include "statement.hpp"
#include <unordered_map>

namespace pg {

struct Connection {
	Owner owner;
	std::vector<std::unique_ptr<Lane>> lanes;
	std::vector<Id> reservations;
	State observed = State::disconnected;
	bool closing = false;
	bool close_delivered = false;
	State state() const;
	int ready() const;
};

struct Event {
	Owner owner;
	Id connection;
	State state;
};

struct Dispatch {
	std::vector<Event> connections;
	std::vector<std::shared_ptr<Job>> jobs;
	std::vector<std::shared_ptr<Job>> streams;
};

// All public methods must be called from the owning (open.mp main) thread.
class Engine {
public:
	explicit Engine(Limits limits = {}, std::filesystem::path root = "scriptfiles/sql",
	                LogLevel log_level = LogLevel::off);
	~Engine();
	Id connect(Owner owner, std::string profile, int pool_size);
	Connection &connection(Owner owner, Id id);
	void disconnect(Owner owner, Id id);
	Id prepare(Owner owner, Id connection, std::string sql, int parameters, bool file = false,
	           bool persistent = true);
	Id prepare_file(Owner owner, Id connection, std::string filename, int parameters,
	                bool persistent = true, std::string section = {});
	Statement &statement(Owner owner, Id id);
	void close_statement(Owner owner, Id id);
	Id execute(Owner owner, Id statement, int lane, Id transaction, int context, bool notify,
	           std::unique_ptr<CallbackDescriptor> callback = {});
	Id execute_stream(Owner owner, Id statement, int chunk_rows, int lane, Id transaction,
	                  int context, bool notify, std::unique_ptr<CallbackDescriptor> callback = {});
	Id query(Owner owner, Id connection, std::string sql, int lane, Id transaction, int context,
	         bool notify, std::unique_ptr<CallbackDescriptor> callback = {});
	Id create_batch(Owner owner, Id connection);
	void add_batch(Owner owner, Id batch, Id statement);
	Id execute_batch(Owner owner, Id batch, int lane, Id transaction, int context, bool notify,
	                 std::unique_ptr<CallbackDescriptor> callback = {});
	void close_batch(Owner owner, Id batch);
	Id begin(Owner owner, Id connection, int lane, int context, bool notify,
	         Isolation isolation = Isolation::server_default, bool read_only = false,
	         std::unique_ptr<CallbackDescriptor> callback = {});
	Id savepoint(Owner owner, Id transaction, std::string name, Kind action, int context,
	             bool notify, std::unique_ptr<CallbackDescriptor> callback = {});
	Id finish(Owner owner, Id transaction, bool commit, int context, bool notify,
	          std::unique_ptr<CallbackDescriptor> callback = {});
	std::shared_ptr<Job> job(Owner owner, Id id);
	void close_job(Owner owner, Id id);
	void unload(Owner owner);
	Dispatch poll();
	Diagnostics &diagnostics() noexcept {
		return diagnostics_;
	}

	const Limits &limits() const {
		return limits_;
	}

private:
	// Controlled lifecycle tests seed publication/reaping snapshots without timing hooks.
	friend struct EngineTestAccess;
	Limits limits_;
	std::filesystem::path root_;
	Id next_ = 1;
	Diagnostics diagnostics_;   // Outlives lanes, including during join/destruction.
	CompletionQueue completed_; // outlives lanes
	std::unordered_map<Id, std::unique_ptr<Connection>> connections_;
	std::unordered_map<Id, Statement> statements_;
	std::unordered_map<Id, Batch> batches_;
	std::unordered_map<Id, std::shared_ptr<Job>> jobs_;
	std::unordered_map<Id, bool> ending_;
	Id id();
	Id submit(std::shared_ptr<Job> job);
	Batch &batch(Owner owner, Id id);
	int transaction_lane(Owner owner, Id connection, Id transaction, int lane);
	Id execute_statement(Owner owner, Id statement, int lane, Id transaction, int context,
	                     bool notify, std::shared_ptr<Stream> stream,
	                     std::unique_ptr<CallbackDescriptor> callback);
};

} // namespace pg
