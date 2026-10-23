// Compile the actual Lane implementation with deterministic OS/libpq/clock seams.
// Engine, queues, publication and recovery remain real; this is not a DB integration test.
#include "pg/engine.hpp"
#include "pg/sql_sections.hpp"
#include "pg/statement.hpp"
#include "pg/stream.hpp"
#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <mutex>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

namespace socket_test {
enum class Case {
	cancel_entry,
	timeout_entry,
	shutdown_entry,
	transport,
	transport_cancel,
	transport_timeout,
	transport_both,
	invalid_socket_cancel,
#ifndef _WIN32
	fd_limit_cancel,
#endif
	eintr_retry,
	eintr_cancel,
	eintr_timeout,
	ready_cancel,
	ready_timeout,
	poll_timeout,
	response_cancel,
	completed_cancel,
	completed_timeout,
	predispatch_cancel,
	flush_cancel,
	consume_cancel
};
struct Control {
	Case which;
	std::mutex mutex;
	std::condition_variable published;
	pg::Job *job = nullptr;
	pg::Lane *lane = nullptr;
	std::atomic<int> milliseconds{0};
	int sends = 0, busy = 0, selects = 0, results = 0;
};
// Set before constructing the Engine; cleared only after its workers join.
Control *control = nullptr;
void cancel() {
	control->job->cancel.store(true);
}
void expire() {
	control->milliseconds.fetch_add(100);
}
PGconn *connect(const char *const *, const char *const *, int) {
	// A valid opaque PGconn for the real RAII deleter, rejected before any network I/O.
	return PQconnectStart("invalid_socket_test_option=1");
}
PostgresPollingStatusType poll(PGconn *) {
	return PGRES_POLLING_OK;
}
int nonblocking(PGconn *, int) {
	return 0;
}
PQnoticeProcessor notice(PGconn *, PQnoticeProcessor, void *) {
	return nullptr;
}
ConnStatusType status(const PGconn *) {
	std::unique_lock lock(control->mutex);
	control->published.wait(lock, [] {
		return control->job != nullptr;
	});
	if (control->which == Case::predispatch_cancel) {
		cancel();
	}
	return CONNECTION_OK;
}
int flush(PGconn *) {
	if (control->which == Case::flush_cancel) {
		cancel();
		return -1;
	}
	return 0;
}
int consume(PGconn *) {
	if (control->which == Case::consume_cancel) {
		cancel();
		return 0;
	}
	return 1;
}
int busy(PGconn *) {
	const auto n = ++control->busy;
	if (n != 1) {
		return 0;
	}
	switch (control->which) {
	case Case::cancel_entry:
		cancel();
		break;
	case Case::timeout_entry:
		expire();
		break;
	case Case::shutdown_entry:
		control->lane->close();
		break;
	case Case::response_cancel:
	case Case::completed_cancel:
	case Case::completed_timeout:
		return 0;
	default:
		break;
	}
	return 1;
}
int socket(const PGconn *) {
	if (control->which == Case::invalid_socket_cancel) {
		cancel();
		return -1;
	}
#ifndef _WIN32
	if (control->which == Case::fd_limit_cancel) {
		cancel();
		return FD_SETSIZE;
	}
#endif
	return 42;
}
int send(PGconn *, const char *, int, const Oid *, const char *const *, const int *, const int *,
         int) {
	++control->sends;
	return 1;
}
int single_row(PGconn *) {
	return 1;
}
PGTransactionStatusType transaction(const PGconn *) {
	return PQTRANS_IDLE;
}
PGresult *result(PGconn *) {
	if (++control->results == 1) {
		if (control->which == Case::response_cancel) {
			cancel();
		}
		return PQmakeEmptyPGresult(nullptr, PGRES_COMMAND_OK);
	}
	if (control->which == Case::completed_cancel) {
		cancel();
	}
	if (control->which == Case::completed_timeout) {
		expire();
	}
	return nullptr;
}
int select(int, fd_set *, fd_set *, fd_set *, const timeval *) {
	++control->selects;
	switch (control->which) {
	case Case::transport_cancel:
	case Case::transport_both:
	case Case::eintr_cancel:
	case Case::ready_cancel:
		cancel();
		break;
	default:
		break;
	}
	switch (control->which) {
	case Case::transport_timeout:
	case Case::transport_both:
	case Case::eintr_timeout:
	case Case::ready_timeout:
	case Case::poll_timeout:
		expire();
		break;
	default:
		break;
	}
	if (control->which == Case::ready_cancel || control->which == Case::ready_timeout) {
		return 1;
	}
	if (control->which == Case::poll_timeout) {
		return 0;
	}
	const bool interrupted = control->which == Case::eintr_retry ||
	                         control->which == Case::eintr_cancel ||
	                         control->which == Case::eintr_timeout;
#ifdef _WIN32
	WSASetLastError(interrupted ? WSAEINTR : WSAENETDOWN);
#else
	errno = interrupted ? EINTR : EBADF;
#endif
	return -1;
}
} // namespace socket_test

namespace pg {
struct SocketTestClock {
	using time_point = Clock::time_point;
	static Clock::time_point now() {
		return Clock::time_point{} +
		       std::chrono::milliseconds(socket_test::control->milliseconds.load());
	}
};
} // namespace pg

// All headers are already included: substitution affects only the production body.
// No test hooks or fake clock are compiled into the shipped pg_core/component.
#define PQconnectStartParams socket_test::connect
#define PQconnectPoll socket_test::poll
#define PQsetnonblocking socket_test::nonblocking
#define PQsetNoticeProcessor socket_test::notice
#define PQstatus socket_test::status
#define PQflush socket_test::flush
#define PQconsumeInput socket_test::consume
#define PQisBusy socket_test::busy
#define PQsocket socket_test::socket
#define PQsendQueryParams socket_test::send
#define PQsetSingleRowMode socket_test::single_row
#define PQtransactionStatus socket_test::transaction
#define PQgetResult socket_test::result
#define PG_SOCKET_SELECT socket_test::select
#define Clock SocketTestClock
#include "../src/session.cpp"
#undef Clock
#undef PG_SOCKET_SELECT
#undef PQgetResult
#undef PQtransactionStatus
#undef PQsetSingleRowMode
#undef PQsendQueryParams
#undef PQsocket
#undef PQisBusy
#undef PQconsumeInput
#undef PQflush
#undef PQstatus
#undef PQsetNoticeProcessor
#undef PQsetnonblocking
#undef PQconnectPoll
#undef PQconnectStartParams

namespace {
void check(bool ok, const char *why) {
	if (!ok) {
		throw std::runtime_error(why);
	}
}
template <class F> void until(F f) {
	const auto deadline = pg::Clock::now() + std::chrono::seconds(5);
	while (!f()) {
		check(pg::Clock::now() < deadline, "Socket test watchdog");
		std::this_thread::yield();
	}
}
void exercise(socket_test::Case which, const char *name, pg::Status expected, pg::Error error,
              int selects, int results = 0) {
	socket_test::Control state{which};
	socket_test::control = &state;
	{
		pg::Limits limits;
		limits.query_ms = 40;
		pg::Engine engine(limits);
		const auto c = engine.connect(91, "socket_stub", 1);
		until([&] {
			return engine.connection(91, c).state() == pg::State::connected;
		});
		const auto id = engine.query(91, c, "SELECT 1", 0, 0, 0, true);
		auto job = engine.job(91, id);
		{
			std::lock_guard lock(state.mutex);
			state.lane = engine.connection(91, c).lanes[0].get();
			state.job = job.get();
		}
		state.published.notify_one();
		int callbacks = 0;
		auto poll = [&] {
			for (const auto &j : engine.poll().jobs) {
				check(j->id == id, "Unexpected callback");
				++callbacks;
			}
		};
		until([&] {
			poll();
			return job->delivered;
		});
		std::cout << name << " observed sends=" << state.sends << " select=" << state.selects
		          << " results=" << state.results << " status=" << int(job->status.load())
		          << " error=" << int(job->error) << std::endl;
		check(job->status.load() == expected && job->error == error, "Wrong terminal cause/status");
		const auto item = expected == pg::Status::confirmed   ? pg::ItemStatus::confirmed
		                  : expected == pg::Status::cancelled ? pg::ItemStatus::cancelled
		                                                      : pg::ItemStatus::uncertain;
		check(job->items_status == std::vector{item} &&
		          job->completed == (expected == pg::Status::confirmed ? 1u : 0u),
		      "Wrong item metadata");
		check(state.selects == selects && state.results == results, "Intended phase not exercised");
		const int sends = which == socket_test::Case::predispatch_cancel ? 0 : 1;
		check(state.sends == sends, "Dispatch proof or no-replay invariant failed");
		if (expected == pg::Status::uncertain && which != socket_test::Case::shutdown_entry) {
			until([&] {
				return engine.connection(91, c).state() == pg::State::connected &&
				       engine.connection(91, c).lanes[0]->snapshot().reconnects == 1;
			});
			check(engine.connection(91, c).lanes[0]->snapshot().reconnects == 1,
			      "Interrupted session not replaced");
		}
		for (int n = 0; n != 4; ++n) {
			poll();
		}
		check(callbacks == 1 && state.sends == sends, "Duplicate completion or SQL replay");
		engine.close_job(91, id);
		std::cout << name << " PASS sends=" << state.sends << " select=" << state.selects
		          << " results=" << state.results << " callbacks=" << callbacks
		          << " status=" << int(expected) << " error=" << int(error) << '\n';
	}
	socket_test::control = nullptr;
}
} // namespace

int main(int argc, char **argv) {
	using C = socket_test::Case;
	using S = pg::Status;
	using E = pg::Error;
	try {
		int count = 0;
		auto run = [&](C c, const char *name, S s, E e, int selects, int results = 0) {
			if (argc == 1 || std::string_view(argv[1]) == name) {
				exercise(c, name, s, e, selects, results);
				++count;
			}
		};
		run(C::cancel_entry, "cancel_entry", S::uncertain, E::cancelled, 0);
		run(C::timeout_entry, "timeout_entry", S::uncertain, E::timeout, 0);
		run(C::shutdown_entry, "shutdown_entry", S::uncertain, E::cancelled, 0);
		run(C::transport, "transport", S::uncertain, E::connection, 1);
		run(C::transport_cancel, "transport_cancel", S::uncertain, E::connection, 1);
		run(C::transport_timeout, "transport_timeout", S::uncertain, E::connection, 1);
		run(C::transport_both, "transport_both", S::uncertain, E::connection, 1);
		run(C::invalid_socket_cancel, "invalid_socket_cancel", S::uncertain, E::connection, 0);
#ifndef _WIN32
		run(C::fd_limit_cancel, "fd_limit_cancel", S::uncertain, E::connection, 0);
#endif
		run(C::eintr_retry, "eintr_retry", S::confirmed, E::none, 1, 2);
		run(C::eintr_cancel, "eintr_cancel", S::uncertain, E::cancelled, 1);
		run(C::eintr_timeout, "eintr_timeout", S::uncertain, E::timeout, 1);
		run(C::ready_cancel, "ready_cancel", S::uncertain, E::cancelled, 1);
		run(C::ready_timeout, "ready_timeout", S::uncertain, E::timeout, 1);
		run(C::poll_timeout, "poll_timeout", S::uncertain, E::timeout, 1);
		run(C::response_cancel, "response_cancel", S::uncertain, E::cancelled, 0, 1);
		run(C::completed_cancel, "completed_cancel", S::confirmed, E::none, 0, 2);
		run(C::completed_timeout, "completed_timeout", S::confirmed, E::none, 0, 2);
		run(C::predispatch_cancel, "predispatch_cancel", S::cancelled, E::cancelled, 0);
		run(C::flush_cancel, "flush_cancel", S::uncertain, E::connection, 0);
		run(C::consume_cancel, "consume_cancel", S::uncertain, E::connection, 0);
		check(count != 0, "Unknown test case");
		std::cout << "socket_wait PASS cases=" << count << '\n';
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "socket_wait FAIL " << e.what() << '\n';
		return 1;
	}
}
