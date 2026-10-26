#pragma once

#include <cstddef>
#include <istream>
#include <string>

namespace pg::detail {

// Shared by the file reader and deterministic stream-error contract tests.
// No content is published unless the complete bounded read succeeds.
std::string read_sql_stream(std::istream &input, std::size_t max);

} // namespace pg::detail
