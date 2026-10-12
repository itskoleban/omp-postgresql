#include "pg/parameters.hpp"
#include <charconv>
#include <cmath>
#include <limits>

namespace pg {
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
	// Text transport also supports UUID, JSONB and BYTEA hex input; PostgreSQL validates them.
	if (oid != 25 && oid != 2950 && oid != 3802 && oid != 17) {
		throw Failure(Error::invalid, "Unsupported text OID");
	}
	return {oid, std::move(v)};
}

Param integer(std::int32_t v) {
	return {23, std::to_string(v)};
}

Param integer64(std::string v) {
	std::int64_t parsed;
	auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), parsed);
	if (ec != std::errc{} || end != v.data() + v.size()) {
		throw Failure(Error::invalid, "BIGINT requires a signed decimal int64 string");
	}
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
	switch (oid) {
	case 16:
	case 17:
	case 20:
	case 23:
	case 25:
	case 700:
	case 1007:
	case 1009:
	case 2950:
	case 3802:
		return {oid, std::nullopt};
	}
	throw Failure(Error::invalid, "NULL needs a supported explicit OID");
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
		if (!utf8(v)) {
			throw Failure(Error::invalid, "Invalid array UTF-8");
		}
		if (out.size() > 1) {
			out += ',';
		}
		out += '"';
		for (char c : v) {
			if (c == '"' || c == '\\') {
				out += '\\';
			}
			out += c;
		}
		out += '"';
	}
	out += '}';
	return {1009, std::move(out)};
}
} // namespace pg
