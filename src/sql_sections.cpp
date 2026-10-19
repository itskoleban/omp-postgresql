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
	return std::string_view::npos;
}

} // namespace

std::string sql_file_label(std::string_view filename) {
	if (filename.empty() || filename.size() > 240 || filename.front() == '/' ||
	    filename.front() == '\\' || filename.find(':') != filename.npos ||
	    filename.find("..") != filename.npos) {
		return "<sql-file>";
	}
	std::string out;
	for (const unsigned char c : filename) {
		if (out.size() == 160) {
			out += "...";
			break;
		}
		out += letter(c) || digit(c) || c == '/' || c == '.' || c == '-' ? char(c) : '?';
	}
	return out;
}

namespace {
// Only called on failure (at most twice): no line index or repeated prefix scans on success.
std::pair<std::size_t, std::size_t> position(std::string_view text, std::size_t offset) {
	std::size_t line = 1, column = 1;
	for (std::size_t i = text.starts_with("\xef\xbb\xbf") ? 3 : 0;
	     i < std::min(offset, text.size()); ++i) {
		if (text[i] == '\r') {
			++line;
			column = 1;
			if (i + 1 < offset && text[i + 1] == '\n') {
				++i;
			}
		} else if (text[i] == '\n') {
			++line;
			column = 1;
		} else {
			++column;
		}
	}
	return {line, column};
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
		throw Failure(Error::file,
		              filename + ": SQL section " +
		                  (valid_section_name(name) ? "'" + std::string(name) + "' " : "") +
		                  "not found; check the case-sensitive declaration name");
	}
	return text.substr(found->second.first, found->second.second);
}

SqlSections parse_sql_sections(std::string text, std::string_view filename) {
	const auto label = sql_file_label(filename);
	if (!utf8(text)) {
		throw Failure(Error::file,
		              label + ": SQL sections require UTF-8 without NUL; save as UTF-8");
	}
	SqlSections document{std::move(text), {}, label};
	const std::string_view sql = document.text;
	std::string current;
	std::size_t start = 0, current_declaration = 0;
	std::map<std::string, std::size_t, std::less<>> declarations;
	auto fail = [&](std::size_t offset, std::string message, Error code = Error::file) -> void {
		auto [line, column] = position(sql, offset);
		throw Failure(code, label + ":" + std::to_string(line) + ":" + std::to_string(column) +
		                        ": " + message);
	};
	auto section_context = [&] {
		return current.empty() ? std::string{} : " in section '" + current + "'";
	};
	auto malformed = [&](std::size_t offset, bool invalid_name = false) {
		fail(offset, invalid_name ? "Malformed SQL section declaration: empty or invalid name; "
		                            "expected ASCII [A-Za-z_][A-Za-z0-9_]* (1..64 bytes)"
		                          : "Malformed SQL section declaration; expected '-- name: "
		                            "identifier' (also '--name:identifier')");
	};
	bool code = false, ended = false, line_start = true;
	bool continuation = false, continuation_escape = false, newline = false;
	auto finish = [&](std::size_t end) {
		if (!current.empty()) {
			if (!code) {
				fail(current_declaration, "Empty SQL section '" + current +
				                              "'; add one SQL statement after the declaration");
			}
			document.sections.at(current) = {start, end - start};
		}
	};
	for (std::size_t i = sql.starts_with("\xef\xbb\xbf") ? 3 : 0; i < sql.size();) {
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
					malformed(i);
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
					malformed(i);
				}
				auto name = trim(comment.substr(5));
				if (!valid_section_name(name)) {
					malformed(i, true);
				}
				finish(i);
				if (document.sections.contains(name)) {
					fail(i, "Duplicate SQL section '" + std::string(name) +
					            "'; first declared at line " +
					            std::to_string(
					                position(sql, declarations.at(std::string(name))).first));
				}
				if (document.sections.size() == sql_section_count) {
					fail(i, "SQL section count limit exceeded (1024); split the file",
					     Error::limit);
				}
				current = name;
				current_declaration = i;
				declarations.emplace(current, i);
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
			const auto opening = i;
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
				fail(opening,
				     "Unterminated SQL block comment" + section_context() + "; close with '*/'");
			}
			continue;
		}
		if (current.empty()) {
			fail(i, "SQL content before first section; add '-- name: identifier' before SQL");
		}
		if (ended || (c == ';' && !code)) {
			fail(i, "SQL section must contain exactly one statement" + section_context() +
			            "; remove the extra statement or semicolon");
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
			const auto opening = i + (explicit_escape ? 1 : 0);
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
				fail(opening, std::string("Unterminated SQL quoted ") +
				                  (quote == '\'' ? "string" : "identifier") + section_context() +
				                  "; close the opening quote");
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
				if (close == std::string_view::npos) {
					fail(i, "Unterminated SQL dollar quote" + section_context() +
					            "; close with the matching dollar delimiter");
				}
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
		throw Failure(Error::file, label + ": SQL file contains no sections; add '-- name: "
		                                   "identifier' or use whole-file mode");
	}
	return document;
}

} // namespace pg
