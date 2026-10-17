#pragma once

#include "types.hpp"
#include <filesystem>

namespace pg {

struct Statement {
	Owner owner;
	Id connection;
	std::string sql;
	bool file, prepared;
	std::vector<std::optional<Param>> bindings;
	std::vector<std::uint32_t> types;
	std::vector<Params> batch;
	std::size_t bytes = 0;
	std::string section;
	void bind(int index, Param p, const Limits &limits);
	void add(const Limits &limits);
};

std::string read_sql(const std::filesystem::path &root, const std::string &name, std::size_t max);
void validate_sql(const std::string &sql);

} // namespace pg
