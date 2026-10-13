#include "pg/parameters.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>

namespace pg {
namespace {
// PostgreSQL's built-in scalar/array OID pairs, including the legacy binders.
constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 16> array_types{{
    {16, 1000},
    {17, 1001},
    {20, 1016},
    {21, 1005},
    {23, 1007},
    {25, 1009},
    {700, 1021},
    {701, 1022},
    {1082, 1182},
    {1083, 1183},
    {1114, 1115},
    {1184, 1185},
    {1186, 1187},
    {1700, 1231},
    {2950, 2951},
    {3802, 3807},
}};

void validate_integer64(std::string_view value) {
	std::int64_t parsed;
	auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
	if (ec != std::errc{} || end != value.data() + value.size()) {
		throw Failure(Error::invalid, "BIGINT requires a signed decimal int64 string");
	}
}

void append_array_string(std::string &output, std::string_view value) {
	if (!utf8(value)) {
		throw Failure(Error::invalid, "Invalid array UTF-8 or embedded NUL");
	}
	output += '"';
	for (char character : value) {
		if (character == '"' || character == '\\') {
			output += '\\';
		}
		output += character;
	}
	output += '"';
}

int hex_digit(char character) {
	if (character >= '0' && character <= '9') {
		return character - '0';
	}
	if (character >= 'a' && character <= 'f') {
		return character - 'a' + 10;
	}
	if (character >= 'A' && character <= 'F') {
		return character - 'A' + 10;
	}
	return -1;
}
} // namespace

bool utf8(std::string_view s) {
	for (std::size_t i = 0; i < s.size();) {
		auto c = static_cast<unsigned char>(s[i++]);
		if (c == 0) {
			return false;
		}
		if (c < 128) {
			continue;
		}
		unsigned cp, n, min;
		if ((c & 0xe0) == 0xc0) {
			cp = c & 31;
			n = 1;
			min = 128;
		} else if ((c & 0xf0) == 0xe0) {
			cp = c & 15;
			n = 2;
			min = 2048;
		} else if ((c & 0xf8) == 0xf0) {
			cp = c & 7;
			n = 3;
			min = 65536;
		} else {
			return false;
		}
		if (i + n > s.size()) {
			return false;
		}
		while (n--) {
			c = static_cast<unsigned char>(s[i++]);
			if ((c & 0xc0) != 0x80) {
				return false;
			}
			cp = (cp << 6) | (c & 63);
		}
		if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
			return false;
		}
	}
	return true;
}

Param text(std::string v, std::uint32_t oid) {
	if (!utf8(v)) {
		throw Failure(Error::invalid, "Invalid UTF-8 or embedded NUL");
	}
	// PostgreSQL validates these textual formats. Never narrow NUMERIC or float8 through Pawn.
	switch (oid) {
	case 17:
	case 21:
	case 25:
	case 701:
	case 1082:
	case 1083:
	case 1114:
	case 1184:
	case 1186:
	case 1700:
	case 2950:
	case 3802:
		return {oid, std::move(v)};
	default:
		throw Failure(Error::invalid, "Unsupported text OID");
	}
}

Param integer(std::int32_t v) {
	return {23, std::to_string(v)};
}

Param integer64(std::string v) {
	validate_integer64(v);
	return {20, std::move(v)};
}

Param floating(float v) {
	if (!std::isfinite(v)) {
		throw Failure(Error::invalid, "Float must be finite");
	}
	char buf[64];
	auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::general,
	                               std::numeric_limits<float>::max_digits10);
	if (ec != std::errc{}) {
		throw Failure(Error::invalid, "Float encoding failed");
	}
	return {700, std::string(buf, end)};
}

Param boolean(bool v) {
	return {16, v ? "true" : "false"};
}

Param null(std::uint32_t oid) {
	if (std::any_of(array_types.begin(), array_types.end(), [oid](const auto &type) {
		    return type.first == oid || type.second == oid;
	    })) {
		return {oid, std::nullopt};
	}
	throw Failure(Error::invalid, "NULL needs a supported explicit OID");
}

Param byte_array(std::span<const std::int32_t> values) {
	constexpr char digits[] = "0123456789abcdef";
	std::string output = "\\x";
	if (values.size() > (output.max_size() - 2) / 2) {
		throw Failure(Error::limit, "BYTEA parameter size exceeded");
	}
	output.reserve(2 + values.size() * 2);
	for (auto value : values) {
		if (value < 0 || value > 255) {
			throw Failure(Error::invalid, "Each byte must be an integer in 0..255");
		}
		output += digits[value >> 4];
		output += digits[value & 15];
	}
	return {17, std::move(output)};
}

std::vector<std::int32_t> decode_bytea(std::string_view value) {
	if (!value.starts_with("\\x") || (value.size() - 2) % 2 != 0) {
		throw Failure(Error::invalid,
		              "BYTEA result requires PostgreSQL hex output (bytea_output=hex)");
	}
	std::vector<std::int32_t> output;
	output.reserve((value.size() - 2) / 2);
	for (std::size_t index = 2; index < value.size(); index += 2) {
		auto high = hex_digit(value[index]);
		auto low = hex_digit(value[index + 1]);
		if (high < 0 || low < 0) {
			throw Failure(Error::invalid, "Invalid BYTEA hex output");
		}
		output.push_back((high << 4) | low);
	}
	return output;
}

Param int_array(std::span<const std::int32_t> values) {
	std::string out = "{";
	for (auto v : values) {
		if (out.size() > 1) {
			out += ',';
		}
		out += std::to_string(v);
	}
	out += '}';
	return {1007, std::move(out)};
}

Param string_array(std::span<const std::string> values) {
	std::string out = "{";
	for (auto &v : values) {
		if (out.size() > 1) {
			out += ',';
		}
		append_array_string(out, v);
	}
	out += '}';
	return {1009, std::move(out)};
}

Param typed_array(std::span<const std::optional<std::string>> values, std::uint32_t element_oid) {
	auto type =
	    std::find_if(array_types.begin(), array_types.end(), [element_oid](const auto &entry) {
		    return entry.first == element_oid;
	    });
	if (type == array_types.end()) {
		throw Failure(Error::invalid, "Unsupported array element OID");
	}
	std::string output = "{";
	for (const auto &value : values) {
		if (output.size() > 1) {
			output += ',';
		}
		if (!value) {
			output += "NULL";
			continue;
		}
		if (element_oid == 20) {
			validate_integer64(*value);
		}
		append_array_string(output, *value);
	}
	output += '}';
	return {type->second, std::move(output)};
}
} // namespace pg
