// Included by native_tests.cpp: exercise the registered native boundary and tick dispatcher.
void callback_unit() {
	Harness h;
	h.vm.callbacks = true;
	h.vm.publics.emplace("Done", 1);
	h.text("Done");
	h.text("i", 128);
	h.text("SELECT 1", stack);
	h.vm.cells[60] = 42;
	h.reject("pg_query", {0, stack, 0, 0, 0, 1, 0, 128, 240}, "Invalid connection handle or owner");
	h.text("Missing");
	h.reject("pg_query", {0, stack, 0, 0, 0, 1, 0, 128, 240},
	         "Callback public not found in owning VM");
	h.text("bad name");
	h.reject("pg_query", {0, stack, 0, 0, 0, 1, 0, 128, 240}, "Invalid callback public name");
	h.text("Done");
	h.reject("pg_query", {0, stack, 0, 0, 0, 1, 0, 128}, "Callback format/argument count mismatch");
	h.reject("pg_query", {0, stack, 0, 0, 0, 1, 0, 128, -1}, "Invalid Pawn address");
	h.text("a", 128);
	h.reject("pg_query", {0, stack, 0, 0, 0, 1, 0, 128, 240},
	         "Callback format supports only i, f, b, s");
	h.text("b", 128);
	h.reject("pg_query", {0, stack, 0, 0, 0, 1, 0, 128, 240}, "Callback boolean must be 0 or 1");
	h.text("s", 128);
	h.vm.long_cells.fill('x');
	h.vm.long_cells[1025] = 0;
	h.reject("pg_query", {0, stack, 0, 0, 0, 1, 0, 128, 4096},
	         "Pawn string exceeds limit or is not byte encoded UTF-8");
	h.vm.long_cells[1024] = 0;
	h.text("ssss", 128);
	h.reject("pg_query", {0, stack, 0, 0, 0, 1, 0, 128, 4096, 4096, 4096, 4096},
	         "Callback payload exceeds 4096 bytes");
	h.text(std::string(16, 'i'), 128);
	std::vector<cell> p{(6 + 2 + 16) * 4, 0, stack, 0, 0, 0, 1, 0, 128};
	p.insert(p.end(), 16, 240);
	check(h.vm.natives.at("pg_query")(&h.vm.amx, p.data()) == 0 &&
	          h.error() == "Invalid connection handle or owner",
	      "Sixteen arguments rejected before Engine validation");
	p[0] += 4;
	p.push_back(240);
	check(h.vm.natives.at("pg_query")(&h.vm.amx, p.data()) == 0 &&
	          h.error() == "Native argument count mismatch",
	      "Seventeen arguments accepted");
	// New tails are admitted only for the file/async natives, never arbitrary fixed natives.
	h.reject("pg_prepare", {0, stack, 0, 1, 0}, "Native argument count mismatch");
	h.reject("pg_execute", {0, 0, 0, 0, 1, 0}, "Native argument count mismatch");
	h.text("", 128);
	h.reject("pg_query", {0, stack, 0, 0, 0, 1, -1, 128}, "Invalid Pawn address");
	check(h.vm.unexpected_calls == 0, "Unexpected callback SDK method");
	h.component.onAmxUnload(h.vm);
}

void callback_integration(const char *profile) {
	Harness h;
	h.vm.callbacks = true;
	h.vm.publics = {{"Done", 1}, {"OnPgComplete", 2}};
	h.text(profile);
	auto c = h.call("pg_connect", {0, 2});
	auto deadline = pg::Clock::now() + std::chrono::seconds(20);
	while (h.component.engine.connection(h.owner(), c).state() != pg::State::connected) {
		h.component.engine.poll();
		check(pg::Clock::now() < deadline, "Callback connection timeout");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	auto tick = [&](cell id) {
		auto j = h.component.engine.job(h.owner(), id);
		auto until = pg::Clock::now() + std::chrono::seconds(10);
		while (!j->delivered) {
			h.component.onTick(Microseconds{}, TimePoint{});
			check(pg::Clock::now() < until, "Callback job timeout");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return j;
	};
	auto submit = [&](bool notify = true, int lane = 0) {
		h.text("Done");
		h.text("ifbs", 128);
		h.text("SELECT 1", stack);
		h.vm.cells[60] = 42;
		h.vm.cells[61] = std::bit_cast<cell>(2.5f);
		h.vm.cells[62] = 1;
		return h.call("pg_query", {c, stack, lane, 0, 999, notify, 0, 128, 240, 244, 248, stack});
	};
	auto id = submit();
	check(id > 0, "Custom native submission rejected");
	// Prove value copying even if the caller overwrites every source cell.
	h.vm.cells.fill(0);
	auto j = tick(id);
	check(!j->callback && h.vm.calls.size() == 1, "Metadata retained or duplicate/global dispatch");
	auto &args = h.vm.calls.back().second;
	check(h.vm.calls.back().first == "Done" && args.size() == 5 && std::get<cell>(args[0]) == id &&
	          std::get<cell>(args[1]) == 42 &&
	          std::get<cell>(args[2]) == std::bit_cast<cell>(2.5f) &&
	          std::get<cell>(args[3]) == 1 && std::get<std::string>(args[4]) == "SELECT 1",
	      "Custom argument values/order changed");
	check(h.vm.amx.stk == 0 && h.vm.amx.hea == 0 && h.vm.amx.cip == 0 && h.vm.amx.frm == 0 &&
	          h.vm.amx.paramcount == 0 && h.vm.amx.reset_stk == 0 && h.vm.amx.reset_hea == 0 &&
	          h.vm.amx.pri == 0 && h.vm.amx.alt == 0,
	      "Callback did not restore full VM frame");
	h.component.engine.close_job(h.owner(), id);
	// Old native arity and context remain valid.
	h.text("SELECT 1", stack);
	id = h.call("pg_query", {c, stack, 0, 0, 77, 1});
	tick(id);
	check(h.vm.calls.back().first == "OnPgComplete" &&
	          std::get<cell>(h.vm.calls.back().second[1]) == 77,
	      "Legacy callback changed");
	h.component.engine.close_job(h.owner(), id);
	auto count = h.vm.calls.size();
	id = submit(false);
	j = tick(id);
	check(h.vm.calls.size() == count && !j->callback, "notify=false dispatched/retained metadata");
	h.component.engine.close_job(h.owner(), id);
	// A missing public at dispatch never falls back or implicitly closes the job.
	id = submit();
	h.vm.publics.erase("Done");
	j = tick(id);
	check(h.vm.calls.size() == count && !j->callback &&
	          h.error() == "Completion callback public no longer exists",
	      "Missing-public policy changed");
	h.component.engine.close_job(h.owner(), id);
	h.vm.publics.emplace("Done", 1);
	for (int error : {AMX_ERR_SLEEP, AMX_ERR_DIVIDE}) {
		h.vm.exec_error = error;
		id = submit();
		tick(id);
		check(h.vm.errors.back() == error && h.vm.amx.stk == 0 && h.vm.amx.hea == 0 &&
		          h.vm.amx.paramcount == 0,
		      "Callback error frame restoration failed");
		h.component.engine.close_job(h.owner(), id);
	}
	h.vm.exec_error = 0;
	h.vm.push_error_after = 1;
	id = submit();
	tick(id);
	check(h.vm.errors.back() == AMX_ERR_STACKERR && h.vm.amx.stk == 0 && h.vm.amx.hea == 0,
	      "Partial push did not restore heap/stack");
	h.component.engine.close_job(h.owner(), id);
	h.vm.push_error_after = -1;
	// A rejected custom descriptor cannot consume staged executions.
	auto statement = h.component.engine.prepare(h.owner(), c, "SELECT 1", 0);
	h.component.engine.statement(h.owner(), statement).add(h.component.engine.limits());
	h.text("Missing");
	h.text("", 128);
	h.reject("pg_execute", {statement, 0, 0, 0, 1, 0, 128},
	         "Callback public not found in owning VM");
	check(h.component.engine.statement(h.owner(), statement).batch.size() == 1,
	      "Rejected callback consumed staging");
	h.component.engine.close_statement(h.owner(), statement);
	// Real queued cancellation and real SQL failure still dispatch custom completion.
	auto blocker = h.component.engine.query(h.owner(), c, "SELECT pg_sleep(0.1)", 0, 0, 0, false);
	id = submit();
	j = h.component.engine.job(h.owner(), id);
	j->cancel.store(true);
	count = h.vm.calls.size();
	tick(id);
	check(j->status.load() == pg::Status::cancelled && h.vm.calls.size() == count + 1,
	      "Queued cancellation missed custom callback");
	h.component.engine.close_job(h.owner(), id);
	tick(blocker);
	h.component.engine.close_job(h.owner(), blocker);
	h.text("Done");
	h.text("", 128);
	h.text("SELECT 1/0", stack);
	id = h.call("pg_query", {c, stack, 0, 0, 0, 1, 0, 128});
	j = tick(id);
	check(j->status.load() == pg::Status::failed && h.vm.calls.back().first == "Done",
	      "SQL failure missed custom callback");
	h.component.engine.close_job(h.owner(), id);
	// All terminal outcomes use the same dispatch; failure/cancel/uncertainty are not filtered.
	for (auto status : {pg::Status::failed, pg::Status::cancelled, pg::Status::uncertain}) {
		id = submit();
		j = h.component.engine.job(h.owner(), id);
		while (!pg::terminal(j->status.load())) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		// Synthetic status fixture exercises dispatcher routing, not PostgreSQL outcome
		// classification.
		j->status.store(status);
		count = h.vm.calls.size();
		tick(id);
		check(h.vm.calls.size() == count + 1 && h.vm.calls.back().first == "Done",
		      "Terminal callback outcome was suppressed");
		h.component.engine.close_job(h.owner(), id);
	}
	// Close inside callback while string arguments are still active.
	h.vm.on_exec = [&] {
		h.component.engine.close_job(h.owner(), std::get<cell>(h.vm.calls.back().second[0]));
	};
	id = submit();
	tick(id);
	h.vm.on_exec = {};
	h.reject("pg_job_close", {id}, "Invalid job handle or owner");
	// Many simultaneous jobs, owner isolation, unload/reload and deterministic cleanup.
	std::vector<cell> ids;
	for (int i = 0; i < 24; ++i) {
		ids.push_back(submit(true, i % 2));
	}
	count = h.vm.calls.size();
	for (auto job : ids) {
		tick(job);
	}
	check(h.vm.calls.size() == count + ids.size(), "Concurrent callbacks lost/duplicated");
	for (auto job : ids) {
		h.component.engine.close_job(h.owner(), job);
	}
	if (auto lossy = std::getenv("OMP_PG_TEST_COMMIT_LOSS")) {
		auto connection = h.component.engine.connect(h.owner(), lossy, 1);
		auto until = pg::Clock::now() + std::chrono::seconds(10);
		while (h.component.engine.connection(h.owner(), connection).state() !=
		       pg::State::connected) {
			check(pg::Clock::now() < until, "Lossy callback connection timeout");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		auto tx = h.component.engine.begin(h.owner(), connection, 0, 0, false);
		tick(tx);
		h.text("Done");
		h.text("", 128);
		id = h.call("pg_commit", {tx, 0, 1, 0, 128});
		j = tick(id);
		check(j->status.load() == pg::Status::uncertain && h.vm.calls.back().first == "Done",
		      "Lost COMMIT response missed custom callback");
		h.component.engine.close_job(h.owner(), id);
		h.component.engine.close_job(h.owner(), tx);
		h.component.engine.disconnect(h.owner(), connection);
	}
	PawnMemory other;
	other.callbacks = true;
	other.publics.emplace("Done", 1);
	h.component.onAmxLoad(other);
	auto other_owner = h.component.scripts.at(&other.amx).owner;
	auto other_connection = h.component.engine.connect(other_owner, profile, 1);
	auto other_until = pg::Clock::now() + std::chrono::seconds(10);
	while (h.component.engine.connection(other_owner, other_connection).state() !=
	       pg::State::connected) {
		check(pg::Clock::now() < other_until, "Second VM connection timeout");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	for (std::size_t i = 0; i <= 8; ++i) {
		other.cells[i] = "SELECT 1"[i];
	}
	for (std::size_t i = 0; i <= 4; ++i) {
		other.cells[32 + i] = "Done"[i];
	}
	cell other_params[] = {8 * sizeof(cell), other_connection, 0, 0, 0, 0, 1, 128, 160};
	auto other_id = other.natives.at("pg_query")(&other.amx, other_params);
	check(other_id > 0, "Second VM custom native rejected");
	id = submit();
	tick(id);
	h.component.engine.close_job(h.owner(), id);
	auto other_job = h.component.engine.job(other_owner, other_id);
	while (!other_job->delivered) {
		h.component.onTick(Microseconds{}, TimePoint{});
		check(pg::Clock::now() < other_until, "Second VM callback timeout");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	check(other.calls.size() == 1 && std::get<cell>(other.calls[0].second[0]) == other_id,
	      "Custom callback routed to wrong VM");
	h.component.engine.close_job(other_owner, other_id);
	other.calls.clear();
	id = submit();
	j = h.component.engine.job(h.owner(), id);
	auto old_owner = h.owner();
	count = h.vm.calls.size();
	h.component.onAmxUnload(h.vm);
	check(!j->callback && !j->notify, "Unload retained callback payload");
	h.vm.natives.clear();
	h.component.onAmxLoad(h.vm);
	check(h.owner() != old_owner, "VM generation reused");
	auto until = pg::Clock::now() + std::chrono::seconds(10);
	while (!pg::terminal(j->status.load())) {
		h.component.onTick(Microseconds{}, TimePoint{});
		check(pg::Clock::now() < until, "Unload cancellation timeout");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	h.component.onTick(Microseconds{}, TimePoint{});
	check(h.vm.calls.size() == count && other.calls.empty(), "Unloaded work reached another VM");
	h.component.onAmxUnload(h.vm);
	h.component.onAmxUnload(other);
}

void callback_benchmark(const char *profile) {
	Harness h;
	h.vm.callbacks = true;
	h.vm.publics = {{"Done", 1}, {"OnPgComplete", 2}};
	auto c = h.component.engine.connect(h.owner(), profile, 2);
	auto deadline = pg::Clock::now() + std::chrono::seconds(20);
	while (h.component.engine.connection(h.owner(), c).state() != pg::State::connected) {
		check(pg::Clock::now() < deadline, "Benchmark connection timeout");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	constexpr std::array names{"global_old_arity", "custom0", "custom_ii", "custom_s96"};
	std::cout << "callback_benchmark,mode,repetition,jobs,submission_ns,dispatch_ns\n";
	for (int repetition = 0; repetition < 8; ++repetition) {
		for (int pass = 0; pass < 4; ++pass) {
			int mode = (pass + repetition) % 4; // Rotate ordering; first repetition warms up.
			h.text("Done");
			h.text(mode == 2 ? "ii" : mode == 3 ? "s" : "", 128);
			h.text("SELECT 1", 192);
			h.text(std::string(96, 'x'), stack);
			h.vm.cells[60] = 42;
			h.vm.cells[61] = -7;
			std::vector<cell> ids;
			ids.reserve(64);
			auto start = pg::Clock::now();
			for (int i = 0; i < 64; ++i) {
				cell id;
				if (mode == 0) {
					id = h.call("pg_query", {c, 192, i % 2, 0, 77, 1});
				} else if (mode == 1) {
					id = h.call("pg_query", {c, 192, i % 2, 0, 0, 1, 0, 128});
				} else if (mode == 2) {
					id = h.call("pg_query", {c, 192, i % 2, 0, 0, 1, 0, 128, 240, 244});
				} else {
					id = h.call("pg_query", {c, 192, i % 2, 0, 0, 1, 0, 128, stack});
				}
				check(id > 0, "Callback benchmark submission rejected");
				ids.push_back(id);
			}
			auto submitted = pg::Clock::now();
			auto until = submitted + std::chrono::seconds(10);
			for (auto id : ids) {
				while (!pg::terminal(h.component.engine.job(h.owner(), id)->status.load())) {
					check(pg::Clock::now() < until, "Callback benchmark completion timeout");
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}
			auto dispatch = pg::Clock::now();
			h.component.onTick(Microseconds{}, TimePoint{});
			auto end = pg::Clock::now();
			check(h.vm.calls.size() == ids.size(), "Callback benchmark lost completions");
			for (auto id : ids) {
				h.component.engine.close_job(h.owner(), id);
			}
			h.vm.calls.clear();
			if (repetition) {
				std::cout
				    << "callback_benchmark," << names[mode] << ',' << repetition << ",64,"
				    << std::chrono::duration_cast<std::chrono::nanoseconds>(submitted - start)
				           .count()
				    << ','
				    << std::chrono::duration_cast<std::chrono::nanoseconds>(end - dispatch).count()
				    << '\n';
			}
		}
	}
	h.component.onAmxUnload(h.vm);
}
