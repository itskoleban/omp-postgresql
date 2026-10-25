#include "pg/component.hpp"
#include <array>
#include <new>

// Focused tests throw only at internal error-storage/validation boundaries.
#ifndef PG_CALLBACK_ERROR_CHECKPOINT
#define PG_CALLBACK_ERROR_CHECKPOINT() ((void)0)
#endif
#ifndef PG_CALLBACK_LOOKUP_CHECKPOINT
#define PG_CALLBACK_LOOKUP_CHECKPOINT() ((void)0)
#endif

namespace pg {

Component *Component::instance = nullptr;

void Component::onLoad(ICore *core) {
	instance = this;
	core_ = core;
	core_->getEventDispatcher().addEventHandler(this);
#if PG_VERSION_PRERELEASE > 0
    core_->printLn("omp-postgresql " PG_VERSION_STRING " (candidate build)");
#else
    core_->printLn("omp-postgresql " PG_VERSION_STRING);
#endif
}

void Component::onInit(IComponentList *list) {
	pawn_ = list->queryComponent<IPawnComponent>();
	if (pawn_) {
		pawn_->getEventDispatcher().addEventHandler(this);
	}
}

void Component::onFree(IComponent *component) {
	if (component == pawn_) {
		reset();
		pawn_->getEventDispatcher().removeEventHandler(this);
		pawn_ = nullptr;
	}
}

void Component::onAmxLoad(IPawnScript &s) {
	scripts.emplace(s.GetAMX(), Script{&s, ++generation_, {}});
	register_natives(s);
}

void Component::onAmxUnload(IPawnScript &s) {
	auto it = scripts.find(s.GetAMX());
	if (it != scripts.end()) {
		engine.unload(it->second.owner);
		scripts.erase(it);
	}
}

void Component::callback(Owner owner, const char *name, cell a, cell b) {
	const std::array<CallbackArgument, 1> arguments{b};
	callback(owner, name, a, arguments, false);
}

void Component::callback(Owner owner, const char *name, cell a,
                         std::span<const CallbackArgument> arguments, bool custom) {
	auto log = [&](LogEvent event, std::uint64_t value = 0) {
		LogRecord record{event, owner};
		if (!custom && std::string_view(name) == "OnPgConnection") {
			record.connection = a;
		} else {
			record.job = a;
		}
		record.value = value;
		engine.diagnostics().emit(record);
	};
	IPawnScript *script = nullptr;
	for (auto &[amx, entry] : scripts) {
		if (entry.owner == owner) {
			script = entry.api;
			break;
		}
	}
	if (!script) {
		engine.diagnostics().emit({LogEvent::owner_missing, owner});
		return;
	}
	auto record_error = [&](AMX *amx, std::string_view message) noexcept {
		auto entry = scripts.find(amx);
		if (entry == scripts.end() || entry->second.owner != owner) return;
		try {
			PG_CALLBACK_ERROR_CHECKPOINT();
			entry->second.error.assign(message);
		} catch (...) {
			// Match the native boundary: a diagnostic allocation cannot abort
			// other accepted deliveries. The callback event is submitted separately.
			entry->second.error.clear();
		}
	};
	int index;
	if (script->FindPublic(name, &index)) {
		log(LogEvent::callback_missing, custom ? 1 : 0);
		if (custom) {
			record_error(script->GetAMX(), "Completion callback public no longer exists");
		}
		return;
	}
	AMX *amx = script->GetAMX();
	auto stack = script->GetSTK();
	auto heap = script->GetHEA();
	auto cip = script->GetCIP();
	auto frame = script->GetFRM();
	auto parameters = amx->paramcount;
	auto reset_stack = amx->reset_stk;
	auto reset_heap = amx->reset_hea;
	auto pri = amx->pri;
	auto alt = amx->alt;
	auto restore = [&] {
		script->SetSTK(stack);
		script->SetHEA(heap);
		script->SetCIP(cip);
		script->SetFRM(frame);
		amx->paramcount = parameters;
		amx->reset_stk = reset_stack;
		amx->reset_hea = reset_heap;
		amx->pri = pri;
		amx->alt = alt;
	};
	int err = 0;
	for (auto it = arguments.rbegin(); it != arguments.rend(); ++it) {
		if (const auto *text = std::get_if<std::string>(&*it)) {
			cell location;
			cell *physical;
			err = script->PushString(&location, &physical, StringView(text->data(), text->size()),
			                         false, false);
		} else {
			err = script->Push(std::get<std::int32_t>(*it));
		}
		if (err) {
			break;
		}
	}
	if (err || (err = script->Push(a))) {
		log(LogEvent::callback_failed, static_cast<unsigned>(err));
		restore();
		if (custom) {
			record_error(amx, "Completion callback argument push failed");
		}
		script->PrintError(err);
		return;
	}
	cell result;
	err = script->Exec(&result, index);
	if (std::none_of(scripts.begin(), scripts.end(), [=](auto &item) {
		    return item.second.owner == owner;
	    })) {
		return;
	}
	// An asynchronous callback cannot retain a Pawn continuation. Restore the full
	// call frame even if Exec returned AMX_ERR_SLEEP or failed during a partial push.
	restore();
	if (err) {
		log(LogEvent::callback_failed, static_cast<unsigned>(err));
		if (custom) {
			record_error(amx, "Completion callback execution failed; continuation discarded");
		}
		script->PrintError(err);
	}
}

void Component::onTick(Microseconds, TimePoint) {
	auto dispatch = engine.poll(); // No queue locks held while invoking Pawn.
	for (auto &e : dispatch.connections) {
		callback(e.owner, "OnPgConnection", e.connection, static_cast<cell>(e.state));
	}
	for (auto &j : dispatch.streams) {
		try {
			PG_CALLBACK_LOOKUP_CHECKPOINT();
			engine.job(j->owner, j->id);
		} catch (const Failure &) {
			continue;
		} catch (const std::bad_alloc &) {
			// Valid lookups allocate nothing; only the stale-handle Failure's
			// diagnostic can allocate after an earlier callback released a job.
			continue;
		}
		callback(j->owner, "OnPgRows", j->id, j->context);
		engine.diagnostics().emit(job_record(LogEvent::stream_rows, *j));
	}
	for (auto &j : dispatch.jobs) {
		// An earlier callback can explicitly release another completed job.
		// Never dispatch that later callback with an already-invalid handle.
		try {
			PG_CALLBACK_LOOKUP_CHECKPOINT();
			engine.job(j->owner, j->id);
		} catch (const Failure &) {
			continue;
		} catch (const std::bad_alloc &) {
			continue;
		}
		// Move before invoking Pawn: close/reentry cannot destroy the active arguments.
		// Delivery consumes metadata even when the public or VM is no longer available.
		auto custom = std::move(j->callback);
		auto record = job_record(LogEvent::callback_begin, *j);
		record.status = j->status.load();
		if (engine.diagnostics().enabled(LogLevel::debug)) {
			record.completion_wait_us = std::chrono::duration_cast<std::chrono::microseconds>(
			                                Clock::now() - j->completed_at)
			                                .count();
			engine.diagnostics().emit(record);
		}
		if (custom) {
			callback(j->owner, custom->function.c_str(), j->id, custom->arguments, true);
		} else {
			callback(j->owner, "OnPgComplete", j->id, j->context);
		}
		record.event = LogEvent::callback_end;
		engine.diagnostics().emit(record);
	}
	if (core_) {
		engine.diagnostics().drain(
		    [](void *context, std::string_view line) {
			    static_cast<ICore *>(context)->printLn("%.*s", static_cast<int>(line.size()),
				                                       line.data());
		    },
		    core_);
	}
}

void Component::reset() {
	for (auto &[amx, s] : scripts) {
		engine.unload(s.owner);
		s.owner = ++generation_;
		s.error.clear();
	}
}

void Component::free() {
	delete this;
}

Component::~Component() {
	if (pawn_) {
		pawn_->getEventDispatcher().removeEventHandler(this);
	}
	if (core_) {
		core_->getEventDispatcher().removeEventHandler(this);
	}
	instance = nullptr;
}

} // namespace pg

COMPONENT_ENTRY_POINT() {
	try {
		return new pg::Component;
	} catch (...) {
		return nullptr;
	}
}
