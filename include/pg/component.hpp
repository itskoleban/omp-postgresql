#pragma once

#include "config.hpp"
#include "engine.hpp"
#include <Server/Components/Pawn/pawn.hpp>
#include <sdk.hpp>
#include <unordered_map>

namespace pg {

struct Script {
	IPawnScript *api;
	Owner owner;
	std::string error;
};

class Component final : public IComponent, public PawnEventHandler, public CoreEventHandler {
public:
	static Component *instance;
	Engine engine{environment_limits()};
	std::unordered_map<AMX *, Script> scripts;
	PROVIDE_UID(UID(0xcfd9534e1ba74280));

	StringView componentName() const override {
		return "omp-postgresql";
	}

	SemanticVersion componentVersion() const override {
		return SemanticVersion(0, 1, 0, 0);
	}

	void onLoad(ICore *core) override;
	void onInit(IComponentList *list) override;
	void onFree(IComponent *component) override;
	void onAmxLoad(IPawnScript &script) override;
	void onAmxUnload(IPawnScript &script) override;
	void onTick(Microseconds, TimePoint) override;
	void reset() override;
	void free() override;
	~Component();

private:
	ICore *core_ = nullptr;
	IPawnComponent *pawn_ = nullptr;
	Owner generation_ = 0;
	void callback(Owner owner, const char *name, cell first, cell second);
};

void register_natives(IPawnScript &script);

} // namespace pg
