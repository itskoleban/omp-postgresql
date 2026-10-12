// Standalone libpq transport benchmark; never used by the open.mp component.
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <libpq-fe.h>
#include <memory>
#include <stdexcept>
#include <string>

struct Clear {
	void operator()(PGresult *p) const {
		if (p) {
			PQclear(p);
		}
	}
};

using Result = std::unique_ptr<PGresult, Clear>;

void valid(PGresult *r) {
	if (!r || (PQresultStatus(r) != PGRES_COMMAND_OK && PQresultStatus(r) != PGRES_TUPLES_OK)) {
		throw std::runtime_error(r ? PQresultErrorMessage(r) : "Missing result");
	}
}

int main(int argc, char **argv) {
	try {
		auto profile = std::getenv("OMP_PG_TEST_SERVICE");
		if (!profile) {
			throw std::runtime_error("Set OMP_PG_TEST_SERVICE to a disposable database service");
		}
		int n = argc > 1 ? std::stoi(argv[1]) : 1000;
		if (n < 1 || n > 10000) {
			throw std::runtime_error("Row count must be 1..10000");
		}
		const char *keys[] = {"service", nullptr};
		const char *vals[] = {profile, nullptr};
		std::unique_ptr<PGconn, decltype(&PQfinish)> c(PQconnectdbParams(keys, vals, 0), PQfinish);
		if (!c || PQstatus(c.get()) != CONNECTION_OK) {
			throw std::runtime_error("Connection failed");
		}
		auto sql = [&](const std::string &s) {
			Result r(PQexec(c.get(), s.c_str()));
			valid(r.get());
		};
		sql("CREATE TEMP TABLE omp_pg_bench(id int PRIMARY KEY, value int)");
		sql("INSERT INTO omp_pg_bench SELECT i,0 FROM generate_series(1," + std::to_string(n) +
		    ") i");
		Result prepared(PQprepare(c.get(), "bench",
		                          "UPDATE omp_pg_bench SET value=value+1 WHERE id=$1::int", 1,
		                          nullptr));
		valid(prepared.get());
		auto measure = [&](const char *label, auto f) {
			auto start = std::chrono::steady_clock::now();
			f();
			auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
			                   std::chrono::steady_clock::now() - start)
			                   .count();
			std::cout << label << ',' << n << ',' << elapsed << '\n';
		};
		std::cout << "mode,rows,microseconds\n";
		measure("individual_prepared", [&] {
			for (int i = 1; i <= n; ++i) {
				auto id = std::to_string(i);
				const char *p[] = {id.c_str()};
				Result r(PQexecPrepared(c.get(), "bench", 1, p, nullptr, nullptr, 0));
				valid(r.get());
			}
		});
		measure("pipeline_sync_per_statement", [&] {
			if (!PQenterPipelineMode(c.get())) {
				throw std::runtime_error("Pipeline unavailable");
			}
			// Bounded window; Sync per item preserves independent implicit transactions.
			for (int base = 1; base <= n; base += 32) {
				int count = std::min(32, n - base + 1);
				for (int i = base; i < base + count; ++i) {
					auto id = std::to_string(i);
					const char *p[] = {id.c_str()};
					if (!PQsendQueryPrepared(c.get(), "bench", 1, p, nullptr, nullptr, 0) ||
					    !PQpipelineSync(c.get())) {
						throw std::runtime_error("Pipeline send failed");
					}
				}
				int syncs = 0, commands = 0;
				while (syncs < count) {
					Result r(PQgetResult(c.get()));
					if (!r) {
						continue;
					}
					if (PQresultStatus(r.get()) == PGRES_PIPELINE_SYNC) {
						++syncs;
					} else {
						valid(r.get());
						++commands;
					}
				}
				if (commands != count) {
					throw std::runtime_error("Pipeline result correlation mismatch");
				}
			}
			while (auto raw = PQgetResult(c.get())) {
				Result r(raw);
				valid(r.get());
			}
			if (!PQexitPipelineMode(c.get())) {
				throw std::runtime_error("Pipeline did not drain");
			}
		});
		measure("single_unnest", [&] {
			std::string array = "{";
			for (int i = 1; i <= n; ++i) {
				if (i > 1) {
					array += ',';
				}
				array += std::to_string(i);
			}
			array += '}';
			const char *p[] = {array.c_str()};
			Result r(PQexecParams(c.get(),
			                      "UPDATE omp_pg_bench a SET value=value+1 FROM unnest($1::int[]) "
			                      "v(id) WHERE a.id=v.id",
			                      1, nullptr, p, nullptr, nullptr, 0));
			valid(r.get());
		});
		Result verify(PQexec(c.get(), "SELECT count(*) FROM omp_pg_bench WHERE value=3"));
		valid(verify.get());
		if (std::stoi(PQgetvalue(verify.get(), 0, 0)) != n) {
			throw std::runtime_error("Benchmark correctness check failed");
		}
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
