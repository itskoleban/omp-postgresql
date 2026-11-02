#pragma once

// Diagnostic-only Windows native snapshots. Never close or modify observed handles.
// Layout reference: winsiderss/phnt ntpsapi.h, ProcessHandleInformation (51).
#include <windows.h>
#include <winternl.h>
#include <cstddef>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace pg::resource_probe {
inline std::wstring native_string(const std::vector<unsigned char> &buffer) {
    const auto text = reinterpret_cast<const UNICODE_STRING *>(buffer.data());
    if (!text->Length) return {};
    const auto start = reinterpret_cast<ULONG_PTR>(buffer.data());
    const auto value = reinterpret_cast<ULONG_PTR>(text->Buffer);
    if (!text->Buffer || text->Length % sizeof(wchar_t) || text->Length > text->MaximumLength ||
        value < start || value - start > buffer.size() || text->Length > buffer.size() - (value - start))
        throw std::runtime_error("Invalid native object string");
    return {text->Buffer, text->Length / sizeof(wchar_t)};
}
inline std::string escaped(const std::wstring &value) {
    std::string output;
    for (wchar_t character : value) {
        if (character >= 32 && character < 127) output += static_cast<char>(character);
        else {
            char code[7];
            std::snprintf(code, sizeof(code), "\\u%04x", static_cast<unsigned>(character));
            output += code;
        }
    }
    return output;
}
inline void handle_snapshot(const char *point) {
    struct Entry {
        HANDLE handle;
        ULONG_PTR handles, pointers;
        ULONG access, type, attributes, reserved;
    };
    struct Snapshot { ULONG_PTR count, reserved; Entry entries[1]; };
    static_assert(sizeof(Entry) == (sizeof(void *) == 4 ? 28 : 40));
    static_assert(offsetof(Snapshot, entries) == 2 * sizeof(ULONG_PTR));
    using Query = LONG (NTAPI *)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    const auto module = GetModuleHandleW(L"ntdll.dll");
    const auto process_query = module ? reinterpret_cast<Query>(GetProcAddress(module, "NtQueryInformationProcess")) : nullptr;
    const auto object_query = module ? reinterpret_cast<Query>(GetProcAddress(module, "NtQueryObject")) : nullptr;
    if (!process_query || !object_query) throw std::runtime_error("Native handle query unavailable");
    std::vector<unsigned char> bytes(65536);
    ULONG required = 0;
    LONG status = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        status = process_query(GetCurrentProcess(), 51, bytes.data(), static_cast<ULONG>(bytes.size()), &required);
        if (status >= 0) break;
        if (required <= bytes.size() || required > 16 * 1024 * 1024) break;
        bytes.resize(required);
    }
    if (status < 0) {
        std::printf("handle_snapshot_failed point=%s ntstatus=%08lx required=%lu\n",
                    point, static_cast<unsigned long>(status), required);
        throw std::runtime_error("Native process snapshot failed");
    }
    const auto snapshot = reinterpret_cast<const Snapshot *>(bytes.data());
    if (snapshot->count > (bytes.size() - offsetof(Snapshot, entries)) / sizeof(Entry))
        throw std::runtime_error("Invalid native process snapshot size");
    std::map<std::wstring, unsigned> totals;
    std::printf("handle_snapshot point=%s pid=%lu count=%zu entry_bytes=%zu\n",
                point, GetCurrentProcessId(), size_t(snapshot->count), sizeof(Entry));
    for (size_t index = 0; index < snapshot->count; ++index) {
        const auto &entry = snapshot->entries[index];
        std::vector<unsigned char> info(8192);
        const LONG type_status = object_query(entry.handle, 2, info.data(), static_cast<ULONG>(info.size()), &required);
        std::wstring type = L"QUERY_FAILED", name;
        LONG name_status = 0;
        if (type_status >= 0) {
            type = native_string(info);
            // Native File-name queries may block on pipes/sockets. Keep File ownership
            // unclassified rather than issuing synchronous object-name I/O.
            if (type != L"File") {
                name_status = object_query(entry.handle, 1, info.data(), static_cast<ULONG>(info.size()), &required);
                if (name_status >= 0) name = native_string(info);
            }
        }
        ++totals[type];
        std::printf("handle point=%s value=%zu type=%s name=%s access=%lx type_index=%lu type_status=%08lx name_status=%08lx file_name_skipped=%u\n",
                    point, size_t(entry.handle), escaped(type).c_str(), escaped(name).c_str(),
                    entry.access, entry.type, static_cast<unsigned long>(type_status),
                    static_cast<unsigned long>(name_status), type == L"File" ? 1u : 0u);
    }
    for (const auto &[type, count] : totals)
        std::printf("handle_type point=%s type=%s count=%u\n", point, escaped(type).c_str(), count);
    std::fflush(stdout);
}
} // namespace pg::resource_probe
