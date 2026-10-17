#include "pg/sql_sections.hpp"
#include "pg/parameters.hpp"

namespace pg {
namespace {
bool letter(unsigned char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
bool digit(unsigned char c) {
	return c >= '0' && c <= '9';
}
bool horizontal(char c) {
	return c == ' ' || c == '\t';
}
bool space(char c) {
	return horizontal(c) || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
bool identifier(unsigned char c) {
	return letter(c) || digit(c) || c == '$' || c >= 128;
}
std::string_view trim(std::string_view s) {
	while (!s.empty() && horizontal(s.front())) {
		s.remove_prefix(1);
	}
	while (!s.empty() && horizontal(s.back())) {
		s.remove_suffix(1);
	}
	return s;
}
bool declaration(std::string_view s) {
	return s.starts_with("name") && (s.size() == 4 || s[4] == ':' || space(s[4]));
}
[[noreturn]] void malformed() {
	throw Failure(Error::file, "Malformed SQL section declaration");
}

// Linear search even for adversarial, very long dollar tags with repeated prefixes.
std::size_t dollar_close(std::string_view sql, std::string_view delimiter, std::size_t start) {
	std::vector<std::size_t> prefix(delimiter.size());
	for (std::size_t i = 1, matched = 0; i < delimiter.size(); ++i) {
		while (matched && delimiter[i] != delimiter[matched]) {
			matched = prefix[matched - 1];
		}
		if (delimiter[i] == delimiter[matched]) {
			++matched;
		}
		prefix[i] = matched;
	}
	for (std::size_t i = start, matched = 0; i < sql.size(); ++i) {
		while (matched && sql[i] != delimiter[matched]) {
			matched = prefix[matched - 1];
		}
		if (sql[i] == delimiter[matched] && ++matched == delimiter.size()) {
			return i + 1 - delimiter.size();
		}
	}
	throw Failure(Error::file, "Unterminated SQL dollar quote");
}
} // namespace

bool valid_section_name(std::string_view name) {
	if (name.empty() || name.size() > sql_section_name_bytes || !letter(name[0])) {
		return false;
	}
	for (const unsigned char c : name) {
		if (!letter(c) && !digit(c)) {
			return false;
		}
	}
	return true;
}

std::string SqlSections::select(std::string_view name) const {
	auto found = sections.find(name);
	if (found == sections.end()) {
		throw Failure(Error::file, "SQL section not found");
	}
	return text.substr(found->second.first, found->second.second);
}

SqlSections parse_sql_sections(std::string text) {
	if (text.starts_with("\xef\xbb\xbf")) {
		text.erase(0, 3);
	}
	if (!utf8(text)) {
		throw Failure(Error::file, "SQL sections require UTF-8 without NUL");
	}
	SqlSections document{std::move(text), {}};
	const std::string_view sql = document.text;
	std::string current;
	std::size_t start = 0;
	bool code = false, ended = false, line_start = true;
	bool continuation = false, continuation_escape = false, newline = false;
	auto finish = [&](std::size_t end) {
		if (!current.empty()) {
			if (!code) {
				throw Failure(Error::file, "Empty SQL section");
			}
			document.sections.at(current) = {start, end - start};
		}
	};
	for (std::size_t i = 0; i < sql.size();) {
		const auto c = sql[i];
		if (c == '\n' || c == '\r') {
			line_start = true;
			newline = true;
			++i;
			continue;
		}
		if (space(c)) {
			// Only spaces/tabs may precede a declaration on its physical line.
			if (!horizontal(c)) {
				line_start = false;
			}
			++i;
			continue;
		}
		if (line_start) {
			// These lookalikes are never accepted as comment delimiters.
			for (const std::string_view dash :
			     {"\xe2\x80\x94", "\xe2\x80\x93", "\xe2\x88\x92", "\xef\xbc\x8d\xef\xbc\x8d"}) {
				if (sql.substr(i).starts_with(dash) &&
				    declaration(trim(sql.substr(i + dash.size())))) {
					malformed();
				}
			}
		}
		if (sql.substr(i).starts_with("--")) {
			auto end = sql.find_first_of("\r\n", i);
			if (end == std::string_view::npos) {
				end = sql.size();
			}
			auto comment = trim(sql.substr(i + 2, end - i - 2));
			if (line_start && declaration(comment)) {
				if (!comment.starts_with("name:")) {
					malformed();
				}
				auto name = trim(comment.substr(5));
				if (!valid_section_name(name)) {
					malformed();
				}
				finish(i);
				if (document.sections.contains(name)) {
					throw Failure(Error::file, "Duplicate SQL section");
				}
				if (document.sections.size() == sql_section_count) {
					throw Failure(Error::limit, "SQL section count limit exceeded");
				}
				current = name;
				document.sections.emplace(current, std::pair<std::size_t, std::size_t>{});
				start = end;
				code = ended = continuation = newline = false;
			}
			i = end;
			line_start = false;
			continue;
		}
		line_start = false;
		if (sql.substr(i).starts_with("/*")) {
			std::size_t depth = 1;
			i += 2;
			while (i < sql.size() && depth) {
				if (sql.substr(i).starts_with("/*")) {
					++depth;
					i += 2;
				} else if (sql.substr(i).starts_with("*/")) {
					--depth;
					i += 2;
				} else {
					newline = newline || sql[i] == '\n' || sql[i] == '\r';
					++i;
				}
			}
			if (depth) {
				throw Failure(Error::file, "Unterminated SQL block comment");
			}
			continue;
		}
		if (current.empty()) {
			throw Failure(Error::file, "SQL content before first section");
		}
		if (ended || (c == ';' && !code)) {
			throw Failure(Error::file, "SQL section must contain exactly one statement");
		}
		if (c == ';') {
			ended = true;
			++i;
			continue;
		}
		code = true;
		const bool explicit_escape =
		    (c == 'E' || c == 'e') && i + 1 < sql.size() && sql[i + 1] == '\'';
		if (c == '\'' || c == '"' || explicit_escape) {
			const bool escape =
			    explicit_escape || (c == '\'' && continuation && newline && continuation_escape);
			const char quote = explicit_escape ? '\'' : c;
			i += explicit_escape ? 2 : 1;
			bool closed = false;
			while (i < sql.size()) {
				if (escape && sql[i] == '\\') {
					i += i + 1 < sql.size() ? 2 : 1;
				} else if (sql[i++] == quote) {
					if (i < sql.size() && sql[i] == quote) {
						++i;
					} else {
						closed = true;
						break;
					}
				}
			}
			if (!closed) {
				throw Failure(Error::file, "Unterminated SQL quoted value");
			}
			continuation = quote == '\'';
			continuation_escape = escape;
			newline = false;
			continue;
		}
		continuation = newline = false;
		if (c == '$') {
			auto end = i + 1;
			if (end < sql.size() &&
			    (letter(sql[end]) || static_cast<unsigned char>(sql[end]) >= 128)) {
				++end;
				while (end < sql.size() && (letter(sql[end]) || digit(sql[end]) ||
				                            static_cast<unsigned char>(sql[end]) >= 128)) {
					++end;
				}
			}
			if (end < sql.size() && sql[end] == '$') {
				auto delimiter = sql.substr(i, end - i + 1);
				auto close = dollar_close(sql, delimiter, end + 1);
				i = close + delimiter.size();
				continue;
			}
		}
		if (letter(c) || static_cast<unsigned char>(c) >= 128) {
			// '$' within an unquoted identifier is not the start of dollar quoting.
			do {
				++i;
			} while (i < sql.size() && identifier(sql[i]));
		} else {
			++i;
		}
	}
	finish(sql.size());
	if (document.sections.empty()) {
		throw Failure(Error::file, "SQL file contains no sections");
	}
	return document;
}
} // namespace pg
