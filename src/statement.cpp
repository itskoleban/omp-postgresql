#include "pg/statement.hpp"
#include "pg/parameters.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>

namespace pg {
void Statement::bind(int index, Param p, const Limits &limits) {
	if (index < 1 || static_cast<std::size_t>(index) > bindings.size()) {
		throw Failure(Error::invalid, "Parameter index out of range");
	}
	if (p.value && p.value->size() > limits.parameter_bytes) {
		throw Failure(Error::limit, "Parameter too large");
	}
	bindings[index - 1] = std::move(p);
}

void Statement::add(const Limits &limits) {
	Params snapshot;
	std::vector<std::uint32_t> new_types;
	std::size_t size = bindings.size() * sizeof(Param);
	for (auto &p : bindings) {
		if (!p) {
			throw Failure(Error::invalid, "Missing parameter; NULL must be explicit");
		}
		new_types.push_back(p->oid);
		snapshot.push_back(*p);
		size += p->value ? p->value->size() : 0;
	}
	if (!types.empty() && types != new_types) {
		throw Failure(Error::invalid, "Parameter types changed; create another statement");
	}
	if (batch.size() >= limits.batch_count || size > limits.batch_bytes - bytes) {
		throw Failure(Error::limit, "Batch limit exceeded");
	}
	batch.push_back(std::move(snapshot));
	types = std::move(new_types);
	bytes += size;
	for (auto &p : bindings) {
		p.reset();
	}
}

void validate_sql(const std::string &sql) {
	if (sql.empty() || !utf8(sql)) {
		throw Failure(Error::invalid, "SQL must be nonempty UTF-8");
	}
	// Skip leading whitespace and SQL comments, then reject transaction-control commands.
	std::size_t i = 0;
	for (;;) {
		while (i < sql.size() && std::isspace(static_cast<unsigned char>(sql[i]))) {
			++i;
		}
		if (sql.compare(i, 2, "--") == 0) {
			auto e = sql.find_first_of("\r\n", i);
			i = e == std::string::npos ? sql.size() : e + 1;
		} else if (sql.compare(i, 2, "/*") == 0) {
			int depth = 1;
			i += 2;
			while (i < sql.size() && depth) {
				if (sql.compare(i, 2, "/*") == 0) {
					++depth;
					i += 2;
				} else if (sql.compare(i, 2, "*/") == 0) {
					--depth;
					i += 2;
				} else {
					++i;
				}
			}
			if (depth) {
				throw Failure(Error::invalid, "Unterminated SQL comment");
			}
		} else {
			break;
		}
	}
	std::string word;
	if (i < sql.size() && sql[i] == ';') {
		throw Failure(Error::invalid, "Leading empty SQL commands are not supported");
	}
	while (i < sql.size() && std::isalpha(static_cast<unsigned char>(sql[i]))) {
		word += static_cast<char>(std::toupper(static_cast<unsigned char>(sql[i++])));
	}
	for (auto forbidden :
	     {"BEGIN", "START", "COMMIT", "END", "ROLLBACK", "ABORT", "SAVEPOINT", "RELEASE", "PREPARE",
	      "EXECUTE", "DEALLOCATE", "DISCARD", "COPY", "LISTEN", "UNLISTEN", "CALL", "DO"}) {
		if (word == forbidden) {
			throw Failure(Error::invalid,
			              "Command requires a dedicated protocol or transaction API");
		}
	}
}

std::string read_sql(const std::filesystem::path &root, const std::string &name, std::size_t max) {
	try {
		auto relative = std::filesystem::path(std::u8string(name.begin(), name.end()));
		if (relative.is_absolute() || relative.has_root_name() || relative.extension() != ".sql") {
			throw Failure(Error::file, "Expected relative .sql path");
		}
		auto base = std::filesystem::canonical(root);
		auto path = std::filesystem::canonical(base / relative);
		auto b = base.begin(), p = path.begin();
		for (; b != base.end(); ++b, ++p) {
			if (p == path.end() || *p != *b) {
				throw Failure(Error::file, "SQL path escapes configured directory");
			}
		}
		if (!std::filesystem::is_regular_file(path)) {
			throw Failure(Error::file, "SQL is not a regular file");
		}
		std::ifstream f(path, std::ios::binary);
		if (!f) {
			throw Failure(Error::file, "SQL file cannot be opened");
		}
		std::string sql(max + 1, '\0');
		f.read(sql.data(), static_cast<std::streamsize>(sql.size()));
		if (f.bad()) {
			throw Failure(Error::file, "SQL file read failed");
		}
		sql.resize(static_cast<std::size_t>(f.gcount()));
		if (sql.size() > max) {
			throw Failure(Error::file, "SQL file exceeds size limit");
		}
		if (sql.starts_with("\xef\xbb\xbf")) {
			sql.erase(0, 3);
		}
		validate_sql(sql);
		return sql;
	} catch (const std::filesystem::filesystem_error &) {
		throw Failure(Error::file, "SQL file missing or inaccessible");
	}
}
} // namespace pg
