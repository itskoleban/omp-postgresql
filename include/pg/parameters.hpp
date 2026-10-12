#pragma once

#include "types.hpp"
#include <span>
#include <string_view>

namespace pg {
bool utf8(std::string_view value);
Param text(std::string value, std::uint32_t oid = 25);
Param integer(std::int32_t value);
Param integer64(std::string value);
Param floating(float value);
Param boolean(bool value);
Param null(std::uint32_t oid);
Param int_array(std::span<const std::int32_t> values);
Param string_array(std::span<const std::string> values);
} // namespace pg
