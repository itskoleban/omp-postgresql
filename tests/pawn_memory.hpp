#pragma once

#include "pg/component.hpp"
#include <array>
#include <map>
#include <stdexcept>

// Minimal test double for the pinned official SDK. Unsupported calls fail the test;
// native memory access uses only GetAddr, registration uses only Register/GetAMX.
class PawnMemory final : public IPawnScript {
public:
	AMX amx{};
	mutable std::array<cell, 256> cells{};
	std::map<std::string, AMX_NATIVE> natives;
	mutable unsigned unexpected_calls = 0;
	enum class Fault { none, null, allocation };
	Fault fault = Fault::none;

	[[noreturn]] void unexpected() const {
		++unexpected_calls;
		throw std::logic_error("Unexpected SDK call");
	}
	int GetAddr(cell offset, cell **output) const override {
		*output = nullptr;
		if (fault == Fault::allocation) {
			throw std::bad_alloc();
		}
		if (fault == Fault::null) {
			return AMX_ERR_NONE;
		}
		// Model allocated heap and active stack, separated by an inaccessible gap.
		if (offset < 0 || offset % sizeof(cell) || offset >= sizeof(cells) ||
		    (offset >= 64 * sizeof(cell) && offset < 128 * sizeof(cell))) {
			return AMX_ERR_MEMACCESS;
		}
		*output = &cells[static_cast<std::size_t>(offset) / sizeof(cell)];
		return AMX_ERR_NONE;
	}
	using IPawnScript::Register;
	int Register(const AMX_NATIVE_INFO *entries, int count) override {
		for (int i = 0; i < count; ++i) {
			if (!entries[i].name || !entries[i].func ||
			    !natives.emplace(entries[i].name, entries[i].func).second) {
				throw std::logic_error("Invalid or duplicate native registration");
			}
		}
		return AMX_ERR_NONE;
	}
	AMX *GetAMX() override {
		return &amx;
	}
	int Allot(int cells, cell *amx_addr, cell **phys_addr) override {
		unexpected();
	}
	int Callback(cell index, cell *result, const cell *params) override {
		unexpected();
	}
	int Cleanup() override {
		unexpected();
	}
	int Clone(AMX *amxClone, void *data) const override {
		unexpected();
	}
	int Exec(cell *retval, int index) override {
		unexpected();
	}
	int FindNative(char const *name, int *index) const override {
		unexpected();
	}
	int FindPublic(char const *funcname, int *index) const override {
		unexpected();
	}
	int FindPubVar(char const *varname, cell *amx_addr) const override {
		unexpected();
	}
	int FindTagId(cell tag_id, char *tagname) const override {
		unexpected();
	}
	int Flags(uint16_t *flags) const override {
		unexpected();
	}
	int GetNative(int index, char *funcname) const override {
		unexpected();
	}
	int GetNativeByIndex(int index, AMX_NATIVE_INFO *ret) const override {
		unexpected();
	}
	int GetPublic(int index, char *funcname) const override {
		unexpected();
	}
	int GetPubVar(int index, char *varname, cell *amx_addr) const override {
		unexpected();
	}
	int GetString(char const *dest, const cell *source, bool use_wchar,
	              size_t size) const override {
		unexpected();
	}
	int GetString(char *dest, const cell *source, bool use_wchar, size_t size) override {
		unexpected();
	}
	int GetTag(int index, char *tagname, cell *tag_id) const override {
		unexpected();
	}
	int GetUserData(long tag, void **ptr) const override {
		unexpected();
	}
	int Init(void *program) override {
		unexpected();
	}
	int InitJIT(void *reloc_table, void *native_code) override {
		unexpected();
	}
	int MakeAddr(cell *phys_addr, cell *amx_addr) const override {
		unexpected();
	}
	int MemInfo(long *codesize, long *datasize, long *stackheap) const override {
		unexpected();
	}
	int NameLength(int *length) const override {
		unexpected();
	}
	AMX_NATIVE_INFO *NativeInfo(char const *name, AMX_NATIVE func) const override {
		unexpected();
	}
	int NumNatives(int *number) const override {
		unexpected();
	}
	int NumPublics(int *number) const override {
		unexpected();
	}
	int NumPubVars(int *number) const override {
		unexpected();
	}
	int NumTags(int *number) const override {
		unexpected();
	}
	int Push(cell value) override {
		unexpected();
	}
	int PushArray(cell *amx_addr, cell **phys_addr, const cell array[], int numcells) override {
		unexpected();
	}
	int PushString(cell *amx_addr, cell **phys_addr, StringView string, bool pack,
	               bool use_wchar) override {
		unexpected();
	}
	int RaiseError(int error) override {
		unexpected();
	}
	int Release(cell amx_addr) override {
		unexpected();
	}
	int SetCallback(AMX_CALLBACK callback) override {
		unexpected();
	}
	int SetDebugHook(AMX_DEBUG debug) override {
		unexpected();
	}
	int SetString(cell *dest, StringView source, bool pack, bool use_wchar,
	              size_t size) const override {
		unexpected();
	}
	int SetUserData(long tag, void *ptr) override {
		unexpected();
	}
	int StrLen(const cell *cstring, int *length) const override {
		unexpected();
	}
	int StrSize(const cell *cstr, int *length) const override {
		unexpected();
	}
	int UTF8Check(char const *string, int *length) const override {
		unexpected();
	}
	int UTF8Get(char const *string, char const **endptr, cell *value) const override {
		unexpected();
	}
	int UTF8Len(const cell *cstr, int *length) const override {
		unexpected();
	}
	int UTF8Put(char *string, char **endptr, int maxchars, cell value) const override {
		unexpected();
	}
	cell GetCIP() const override {
		unexpected();
	}
	cell GetHEA() const override {
		unexpected();
	}
	cell GetSTP() const override {
		unexpected();
	}
	cell GetSTK() const override {
		unexpected();
	}
	cell GetHLW() const override {
		unexpected();
	}
	cell GetFRM() const override {
		unexpected();
	}
	void SetCIP(cell v) override {
		unexpected();
	}
	void SetHEA(cell v) override {
		unexpected();
	}
	void SetSTP(cell v) override {
		unexpected();
	}
	void SetSTK(cell v) override {
		unexpected();
	}
	void SetHLW(cell v) override {
		unexpected();
	}
	void SetFRM(cell v) override {
		unexpected();
	}
	void PrintError(int err) override {
		unexpected();
	}
	int GetID() const override {
		unexpected();
	}
	bool IsLoaded() const override {
		unexpected();
	}
	cell CallNativeArray(const char *, Span<Impl::NativeParam>) override {
		unexpected();
	}
};
