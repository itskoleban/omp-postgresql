#pragma once

#include "types.hpp"
#include <filesystem>
#include <map>
#include <string_view>

namespace pg {

inline constexpr std::size_t sql_section_name_bytes = 64;
inline constexpr std::size_t sql_section_count = 1024;

bool valid_section_name(std::string_view name);

// Offsets, rather than string_views, keep the index valid when the document moves.
struct SqlSections {
	std::string text;
	std::map<std::string, std::pair<std::size_t, std::size_t>, std::less<>> sections;
	std::string filename;
	std::string select(std::string_view name) const;
};

// A bounded lexical index, not a PostgreSQL syntax parser. Requires standard strings.
SqlSections parse_sql_sections(std::string text, std::string_view filename = {});
// Owner-readable context only; never forwarded to automatic logging.
std::string sql_file_label(std::string_view filename);
std::string read_sql_contents(const std::filesystem::path &root, const std::string &name,
                              std::size_t max);

} // namespace pg
