#include "pg/component.hpp"

namespace pg {

Component *Component::instance = nullptr;

void Component::onLoad(ICore *core) {
	instance = this;
	core_ = core;
	core_->getEventDispatcher().addEventHandler(this);
	core_->printLn("omp-postgresql 0.1.0 (experimental)");
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
	IPawnScript *script = nullptr;
	for (auto &[amx, entry] : scripts) {
		if (entry.owner == owner) {
			script = entry.api;
			break;
		}
	}
	if (!script) {
		return;
	}
	int index;
	if (script->FindPublic(name, &index)) {
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
	if (script->Push(b) || script->Push(a)) {
		restore();
		return;
	}
	cell result;
	int err = script->Exec(&result, index);
	if (std::none_of(scripts.begin(), scripts.end(), [=](auto &item) {
		    return item.second.owner == owner;
	    })) {
		return;
	}
	// An asynchronous callback cannot retain a Pawn continuation. Restore the full
	// call frame even if Exec returned AMX_ERR_SLEEP or failed during a partial push.
	restore();
	if (err) {
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
			engine.job(j->owner, j->id);
		} catch (const Failure &) {
			continue;
		}
		callback(j->owner, "OnPgRows", j->id, j->context);
	}
	for (auto &j : dispatch.jobs) {
		// An earlier callback can explicitly release another completed job.
		// Never dispatch that later callback with an already-invalid handle.
		try {
			engine.job(j->owner, j->id);
		} catch (const Failure &) {
			continue;
		}
		callback(j->owner, "OnPgComplete", j->id, j->context);
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
