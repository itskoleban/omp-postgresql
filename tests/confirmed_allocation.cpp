// Named exceptions at the actual Lane result insertions preserve allocator interceptors.
#include "pg/engine.hpp"
#include "pg/stream.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
enum class Site { none, execute_result, transaction_result, stream_delivery_result };
std::atomic<Site> selected_site{Site::none};
std::atomic<unsigned> selected_ordinal{0}, site_calls{0}, injected_faults{0};

void allocation_checkpoint(const char *name) {
    const auto site = selected_site.load();
    if ((site == Site::execute_result && std::string_view(name) == "execute_result") ||
        (site == Site::transaction_result && std::string_view(name) == "transaction_result") ||
        (site == Site::stream_delivery_result && std::string_view(name) == "stream_delivery_result")) {
        if (site_calls.fetch_add(1) + 1 == selected_ordinal.load()) {
            injected_faults.fetch_add(1);
            throw std::bad_alloc();
        }
    }
}
}
#define PG_ALLOCATION_CHECKPOINT(site) allocation_checkpoint(site)
#include "../src/session.cpp"
#undef PG_ALLOCATION_CHECKPOINT

namespace {
constexpr pg::Owner owner = 919;
void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Predicate> void until(Predicate predicate) {
    const auto deadline = pg::Clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
        check(pg::Clock::now() < deadline, "Confirmed-result allocation deadline");
        std::this_thread::yield();
    }
}

struct Database {
    std::unique_ptr<PGconn, decltype(&PQfinish)> connection{nullptr, PQfinish};
    std::string table;
    explicit Database(const char *service) {
        const char *keys[] = {"service", nullptr};
        const char *values[] = {service, nullptr};
        connection.reset(PQconnectdbParams(keys, values, 0));
        check(connection && PQstatus(connection.get()) == CONNECTION_OK,
              "Independent observer connection failed");
        table = "confirmed_allocation_" + scalar("SELECT pg_backend_pid()");
        execute("CREATE TABLE " + table + " (item integer)");
    }
    ~Database() {
        if (!table.empty()) {
            PQclear(PQexec(connection.get(), ("DROP TABLE IF EXISTS " + table).c_str()));
        }
    }
    void execute(const std::string &sql) {
        std::unique_ptr<PGresult, decltype(&PQclear)> result(PQexec(connection.get(), sql.c_str()), PQclear);
        check(result && PQresultStatus(result.get()) == PGRES_COMMAND_OK,
              "Independent observer command failed");
    }
    std::string scalar(const std::string &sql) {
        std::unique_ptr<PGresult, decltype(&PQclear)> result(PQexec(connection.get(), sql.c_str()), PQclear);
        check(result && PQresultStatus(result.get()) == PGRES_TUPLES_OK &&
                  PQntuples(result.get()) == 1 && PQnfields(result.get()) == 1 &&
                  !PQgetisnull(result.get(), 0, 0), "Independent observer query failed");
        return PQgetvalue(result.get(), 0, 0);
    }
    std::string insert(int item) const {
        return "INSERT INTO " + table + " VALUES (" + std::to_string(item) + ") RETURNING item";
    }
};

struct Harness {
    pg::Engine engine;
    pg::Id connection;
    unsigned deliveries = 0;
    std::size_t stream_rows = 0;
    std::vector<std::string> stream_values;
    explicit Harness(const char *service, pg::Limits limits = {})
        : engine(limits), connection(engine.connect(owner, service, 1)) {
        ready();
    }
    void ready() {
        until([&] {
            engine.poll();
            return engine.connection(owner, connection).state() == pg::State::connected;
        });
    }
    pg::LaneStats stats() { return engine.connection(owner, connection).lanes.front()->snapshot(); }
    std::shared_ptr<pg::Job> wait(pg::Id id, bool consume_live_stream = true) {
        auto job = engine.job(owner, id);
        deliveries = 0;
        stream_rows = 0;
        stream_values.clear();
        auto collect = [&] {
            auto dispatch = engine.poll();
            for (const auto &delivered : dispatch.jobs) if (delivered == job) ++deliveries;
            if (job->stream && (consume_live_stream || pg::terminal(job->status.load())) &&
                job->stream->next()) {
                for (const auto &row : job->stream->current()->rows) {
                    ++stream_rows;
                    check(row.size() == 1 && row[0].has_value(), "Malformed retained stream row");
                    stream_values.push_back(*row[0]);
                }
                job->stream->release();
            }
        };
        until([&] {
            collect();
            return job->delivered && (!job->stream || !job->stream->available());
        });
        collect();
        check(deliveries == 1, "Missing or duplicate terminal completion");
        return job;
    }
    std::shared_ptr<pg::Job> success(pg::Id id) {
        auto job = wait(id);
        check(job->status.load() == pg::Status::confirmed && job->error == pg::Error::none,
              "Setup or recovery SQL did not confirm");
        return job;
    }
    pg::Id query(std::string sql, pg::Id transaction = 0) {
        return engine.query(owner, connection, std::move(sql), 0, transaction, 0, true);
    }
    void close(const std::shared_ptr<pg::Job> &job) { engine.close_job(owner, job->id); }
    void recover(std::uint64_t attempts) {
        ready();
        check(stats().connection_attempts == attempts + 1,
              "Allocation failure did not recover exactly one physical session");
        auto fresh = success(query("SELECT 42"));
        check(fresh->results.size() == 1 && fresh->results[0].rows[0][0] == "42",
              "Explicit new SQL did not recover after local allocation failure");
        close(fresh);
        auto begin = success(engine.begin(owner, connection, 0, 0, true));
        auto rollback = success(engine.finish(owner, begin->id, false, 0, true));
        check(engine.connection(owner, connection).reservations[0] == 0,
              "Recovery retained a transaction reservation");
        close(rollback);
        close(begin);
    }
};

void arm(Site site, unsigned ordinal, bool omit) {
    site_calls.store(0);
    injected_faults.store(0);
    selected_ordinal.store(ordinal);
    selected_site.store(omit ? Site::none : site);
}
void observed(std::string_view scenario, const std::shared_ptr<pg::Job> &job, unsigned ordinal) {
    selected_site.store(Site::none);
    std::printf("confirmed_result scenario=%.*s calls=%u faults=%u status=%d error=%d completed=%zu results=%zu item0=%d\n",
                static_cast<int>(scenario.size()), scenario.data(), site_calls.load(), injected_faults.load(),
                int(job->status.load()), int(job->error), job->completed, job->results.size(),
                int(job->items_status.at(0)));
    std::fflush(stdout);
    check(site_calls.load() == ordinal && injected_faults.load() == 1,
          "Required scenario result-insertion exception was not injected exactly once");
    check(job->status.load() == pg::Status::failed && job->error == pg::Error::internal,
          "Local result allocation failure lost its structured terminal error");
}

void execute_case(const char *service, std::string_view scenario, bool omit) {
    Database database(service);
    const bool delivery = scenario == "stream_delivery";
    pg::Limits limits;
    if (delivery) limits.query_ms = 2000;
    Harness h(service, limits);
    const bool stream = scenario == "stream" || delivery;
    const bool batch = scenario == "batch";
    const unsigned ordinal = batch ? 2 : 1;
    pg::Id statement = 0;
    if (stream || batch) {
        statement = h.engine.prepare(owner, h.connection, delivery
            ? "INSERT INTO " + database.table + " SELECT generate_series(1,3) RETURNING item"
            : "INSERT INTO " + database.table + " VALUES ($1) RETURNING item", delivery ? 0 : 1);
        auto &prepared = h.engine.statement(owner, statement);
        for (int i = 1; i <= (batch ? 3 : 1); ++i) {
            if (!delivery) prepared.bind(1, pg::Param{23, std::to_string(i)}, h.engine.limits());
            prepared.add(h.engine.limits());
        }
    }
    const auto before = h.stats();
    arm(delivery ? Site::stream_delivery_result : Site::execute_result, ordinal, omit);
    const auto id = stream ? h.engine.execute_stream(owner, statement, 2, 0, 0, 0, true)
                  : batch ? h.engine.execute(owner, statement, 0, 0, 0, true)
                          : h.query(database.insert(1));
    // Keeping the first full block pending forces the confirmed trailing partial
    // block to hit its delivery deadline. The exception targets metadata retention
    // after SQL confirmation, rather than an unconfirmed mid-stream interruption.
    auto job = h.wait(id, !delivery);
    observed(scenario, job, ordinal);
    check(job->completed == ordinal && job->error_index == static_cast<int>(ordinal - 1),
          "Confirmed current item was lost or counted twice");
    const auto expected_items = batch
        ? std::vector{pg::ItemStatus::confirmed, pg::ItemStatus::confirmed, pg::ItemStatus::not_executed}
        : std::vector{pg::ItemStatus::confirmed};
    check(job->items_status == expected_items, "Confirmed prefix or unexecuted suffix metadata changed");
    check(job->results.size() == ordinal - 1, "Missing current payload was fabricated or prior payload lost");
    if (batch) check(job->results[0].rows[0][0] == "1", "Earlier confirmed batch payload was not retained");
    if (stream) check(h.stream_rows == (delivery ? 2u : 1u) && h.stream_values ==
                          (delivery ? std::vector<std::string>{"1", "2"} : std::vector<std::string>{"1"}),
                      "Already published confirmed stream block was lost or duplicated");
    const auto expected_rows = delivery ? "3" : batch ? "2" : "1";
    check(database.scalar("SELECT count(*) FROM " + database.table) == expected_rows,
          "Independent row count shows missing, replayed or later dispatched SQL");
    check(database.scalar("SELECT sum(item) FROM " + database.table) == (delivery ? "6" : batch ? "3" : "1"),
          "Independent row values changed");
    check(h.stats().sql_commands == before.sql_commands + ordinal, "SQL was replayed after local failure");
    h.recover(before.connection_attempts);
    check(database.scalar("SELECT count(*) FROM " + database.table) == expected_rows,
          "Recovery replayed previously confirmed SQL");
    h.close(job);
    if (statement) h.engine.close_statement(owner, statement);
}

void transaction_case(const char *service, std::string_view scenario, bool omit) {
    Database database(service);
    Harness h(service);
    const bool rejected = scenario == "rejected_commit";
    const bool commit = scenario != "rollback";
    auto begin = h.success(h.engine.begin(owner, h.connection, 0, 0, true));
    auto inserted = h.success(h.query(database.insert(1), begin->id));
    h.close(inserted);
    check(database.scalar("SELECT count(*) FROM " + database.table) == "0",
          "Uncommitted transaction write became visible");
    if (rejected) {
        auto failed = h.wait(h.query("SELECT 1 / 0", begin->id));
        check(failed->status.load() == pg::Status::failed && failed->error == pg::Error::sql &&
                  failed->completed == 0 && failed->results[0].sqlstate == "22012",
              "Failed-transaction control did not establish the SQL rejection");
        h.close(failed);
    }
    const auto before = h.stats();
    arm(Site::transaction_result, 1, omit);
    auto finish = h.wait(h.engine.finish(owner, begin->id, commit, 0, true));
    observed(scenario, finish, 1);
    check(finish->results.empty() && finish->error_index == 0,
          "Failed finish insertion fabricated a retained result");
    check(finish->completed == (rejected ? 0u : 1u),
          "Requested transaction command confirmation was lost or fabricated");
    check(finish->items_status == std::vector{rejected ? pg::ItemStatus::failed : pg::ItemStatus::confirmed},
          "Physical ROLLBACK was confused with requested COMMIT confirmation");
    check(h.engine.connection(owner, h.connection).reservations[0] == 0,
          "Failed result insertion retained the logical transaction reservation");
    check(database.scalar("SELECT count(*) FROM " + database.table) == (commit && !rejected ? "1" : "0"),
          "Independent transaction visibility disagrees with confirmed outcome");
    check(h.stats().sql_commands == before.sql_commands + 1, "Transaction finish SQL was replayed");
    h.recover(before.connection_attempts);
    check(database.scalar("SELECT count(*) FROM " + database.table) == (commit && !rejected ? "1" : "0"),
          "Recovery changed the committed or rolled-back database effect");
    h.close(finish);
    h.close(begin);
}
}

int main(int argc, char **argv) {
    std::string_view only;
    bool omit = false;
    if (argc == 3 && (std::string_view(argv[1]) == "--only" || std::string_view(argv[1]) == "--omit")) {
        only = argv[2];
        omit = std::string_view(argv[1]) == "--omit";
    } else if (argc != 1) return 2;
    const auto service = std::getenv("OMP_PG_TEST_SERVICE");
    if (!service) return 77;
    unsigned scenarios = 0;
    try {
        for (const auto scenario : {"query", "stream", "stream_delivery", "batch", "commit", "rollback", "rejected_commit"}) {
            if (!only.empty() && only != scenario) continue;
            if (std::string_view(scenario) == "query" || std::string_view(scenario) == "stream" ||
                std::string_view(scenario) == "stream_delivery" ||
                std::string_view(scenario) == "batch") execute_case(service, scenario, omit);
            else transaction_case(service, scenario, omit);
            ++scenarios;
        }
        check(scenarios != 0, "Unknown result-insertion scenario");
        std::printf("confirmed_result_allocation PASS scenarios=%u injection=NAMED_SITE allocator_interceptors=RETAINED\n",
                    scenarios);
        return 0;
    } catch (const std::exception &error) {
        selected_site.store(Site::none);
        std::fprintf(stderr, "confirmed_result_allocation FAIL %s\n", error.what());
        return 1;
    }
}
