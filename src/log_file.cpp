#include "pg/log_file.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#ifdef _WIN32
#include <windows.h>
#include <winternl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace pg {

std::string log_path(LogDestination destination, std::string_view path) {
	if (destination < LogDestination::console || destination > LogDestination::both ||
	    (destination == LogDestination::console && !path.empty())) {
		throw Failure(Error::invalid, "Invalid log destination or nonempty console path");
	}
	if (path.empty()) {
		return destination == LogDestination::console ? "" : "omp-postgresql.log";
	}
	if (path.starts_with("./")) {
		path.remove_prefix(2);
	}
	if (!path.starts_with("logs/")) {
		throw Failure(Error::invalid, "Log path must be logs/<basename>.log (optional './')");
	}
	path.remove_prefix(5);
	if (path.size() < 5 || path.size() > 96 || !path.ends_with(".log") || path.front() == '.' ||
	    path.find("..") != path.npos ||
	    path.find_first_not_of(
	        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != path.npos) {
		throw Failure(Error::invalid,
		              "Invalid log basename; use ASCII letters, digits, '_', '-' or '.' and .log");
	}
	auto stem = std::string(path.substr(0, path.find('.')));
	for (char &c : stem) {
		if (c >= 'a' && c <= 'z') {
			c -= 'a' - 'A';
		}
	}
	if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" ||
	    stem == "CONOUT$" ||
	    (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
	     stem[3] >= '0' && stem[3] <= '9')) {
		throw Failure(Error::invalid, "Reserved log basename");
	}
	return std::string(path);
}

namespace {

// The server directory/ancestors are trusted. The logs directory and file are not.
// Pin logs while open; never follow a final symlink/reparse point, device or hard link.
class File {
#ifdef _WIN32
	HANDLE directory_ = INVALID_HANDLE_VALUE, file_ = INVALID_HANDLE_VALUE;
	std::filesystem::path path_;
#else
	int directory_ = -1, file_ = -1;
	std::string name_;
	dev_t device_{};
	ino_t inode_{};
#endif
	std::size_t bytes_ = 0;

public:
	~File() {
		close();
	}
	void close() noexcept {
#ifdef _WIN32
		if (file_ != INVALID_HANDLE_VALUE) {
			CloseHandle(file_);
		}
		if (directory_ != INVALID_HANDLE_VALUE) {
			CloseHandle(directory_);
		}
		file_ = directory_ = INVALID_HANDLE_VALUE;
#else
		if (file_ >= 0) {
			::close(file_);
		}
		if (directory_ >= 0) {
			::close(directory_);
		}
		file_ = directory_ = -1;
#endif
	}
	bool open(const std::filesystem::path &base, const std::string &name) {
		close();
#ifdef _WIN32
		auto directory = base / "logs";
		if (!CreateDirectoryW(directory.c_str(), nullptr) &&
		    GetLastError() != ERROR_ALREADY_EXISTS) {
			return false;
		}
		directory_ = CreateFileW(
		    directory.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		BY_HANDLE_FILE_INFORMATION info{};
		if (directory_ == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(directory_, &info) ||
		    !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
		    (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
			return false;
		}
		path_ = directory / name;
		return open_file();
#else
		int parent = ::open(base.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		if (parent < 0) {
			return false;
		}
		int result = mkdirat(parent, "logs", 0700);
		if (result != 0 && errno != EEXIST) {
			::close(parent);
			return false;
		}
		directory_ = openat(parent, "logs", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		::close(parent);
		if (directory_ < 0) {
			return false;
		}
		name_ = name;
		return open_file();
#endif
	}
	bool open_file() {
#ifdef _WIN32
		// Open relative to the pinned directory instead of re-resolving a mutable path.
		const auto procedure = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile");
		decltype(&NtCreateFile) create = nullptr;
		static_assert(sizeof create == sizeof procedure);
		std::memcpy(&create, &procedure, sizeof create);
		if (!create) {
			return false;
		}
		auto name = path_.filename().wstring();
		UNICODE_STRING unicode{};
		unicode.Length = unicode.MaximumLength = static_cast<USHORT>(name.size() * sizeof(wchar_t));
		unicode.Buffer = name.data();
		// MinGW exposes extra ABI padding fields: initialize members by name on both SDKs.
		OBJECT_ATTRIBUTES attributes{};
		attributes.Length = sizeof attributes;
		attributes.RootDirectory = directory_;
		attributes.ObjectName = &unicode;
		attributes.Attributes = 0x40; // OBJ_CASE_INSENSITIVE
		IO_STATUS_BLOCK status{};
		HANDLE handle = nullptr;
		// FILE_OPEN_IF; NON_DIRECTORY, OPEN_REPARSE_POINT, SYNCHRONOUS_IO_NONALERT.
		if (create(&handle, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &attributes,
		           &status, nullptr, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, 3,
		           0x40 | 0x00200000 | 0x20, nullptr, 0) < 0) {
			return false;
		}
		file_ = handle;
		BY_HANDLE_FILE_INFORMATION info{};
		if (file_ == INVALID_HANDLE_VALUE || GetFileType(file_) != FILE_TYPE_DISK ||
		    !GetFileInformationByHandle(file_, &info) || info.nNumberOfLinks != 1 ||
		    (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
			return false;
		}
		const auto size = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
#else
		file_ = openat(directory_, name_.c_str(),
		               O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
		struct stat info{};
		if (file_ < 0 || fstat(file_, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
		    info.st_uid != geteuid()) {
			return false;
		}
		device_ = info.st_dev;
		inode_ = info.st_ino;
		const auto size = static_cast<std::uint64_t>(info.st_size);
#endif
		// Never truncate an externally oversized file or turn it into an oversized archive.
		if (size > LogFileWriter::file_limit) {
			return false;
		}
		bytes_ = static_cast<std::size_t>(size);
		return true;
	}
	bool write(std::string_view text) {
#ifndef _WIN32
		struct stat info{};
		if (fstatat(directory_, name_.c_str(), &info, AT_SYMLINK_NOFOLLOW) ||
		    !S_ISREG(info.st_mode) || info.st_dev != device_ || info.st_ino != inode_ ||
		    info.st_nlink != 1 || info.st_size < 0 ||
		    static_cast<std::uint64_t>(info.st_size) > LogFileWriter::file_limit) {
			return false;
		}
		bytes_ = static_cast<std::size_t>(info.st_size);
#endif
		if (bytes_ > LogFileWriter::file_limit - text.size()) {
#ifdef _WIN32
			CloseHandle(file_);
			file_ = INVALID_HANDLE_VALUE;
			auto archive = path_;
			archive += ".1";
			// Replacing the directory entry never follows an archive reparse point.
			if (!MoveFileExW(path_.c_str(), archive.c_str(), MOVEFILE_REPLACE_EXISTING)) {
				return false;
			}
#else
			::close(file_);
			file_ = -1;
			if (renameat(directory_, name_.c_str(), directory_, (name_ + ".1").c_str())) {
				return false;
			}
#endif
			if (!open_file()) {
				return false;
			}
		}
#ifdef _WIN32
		DWORD written = 0;
		if (!WriteFile(file_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
		    written != text.size()) {
			return false;
		}
#else
		// Do not spin on EINTR, partial writes, ENOSPC or EAGAIN. Disable and report.
		if (::write(file_, text.data(), text.size()) != static_cast<ssize_t>(text.size())) {
			return false;
		}
#endif
		bytes_ += text.size();
		return true;
	}
};

} // namespace

LogFileWriter::LogFileWriter(std::filesystem::path base, WriteGate gate, void *context)
    : base_(std::move(base)), gate_(gate), gate_context_(context), thread_([this] {
	      run();
      }) {}

LogFileWriter::~LogFileWriter() {
	{
		std::lock_guard lock(mutex_);
		stop_ = true;
		discard_queued();
	}
	wake_.notify_one();
	thread_.join(); // Never detach component code across DLL unload.
}

void LogFileWriter::discard_queued() noexcept {
	std::uint64_t lost = 0;
	for (std::size_t i = 0; i < size_; ++i) {
		if (records_[(head_ + i) % records_.size()].event != LogEvent::log_summary) {
			++lost;
		}
	}
	dropped_.fetch_add(lost);
	head_ = size_ = 0;
}

void LogFileWriter::configure(std::string_view path) {
	if (!path.empty()) {
		log_path(LogDestination::file, "logs/" + std::string(path));
	}
	{
		std::lock_guard lock(mutex_);
		if (dead_ && !path.empty()) {
			throw Failure(Error::internal, "Log writer stopped; restart component");
		}
		discard_queued();
		path_.fill(0);
		std::copy(path.begin(), path.end(), path_.begin());
		++generation_;
		unavailable_.store(false);
	}
	wake_.notify_one();
}

bool LogFileWriter::enqueue(LogRecord record) noexcept {
	try {
		std::unique_lock lock(mutex_, std::try_to_lock);
		if (!lock.owns_lock() || size_ == records_.size()) {
			if (record.event != LogEvent::log_summary) {
				dropped_.fetch_add(1);
			}
			return false;
		}
		records_[(head_ + size_) % records_.size()] = record;
		++size_;
	} catch (...) {
		if (record.event != LogEvent::log_summary) {
			dropped_.fetch_add(1);
		}
		return false;
	}
	wake_.notify_one();
	return true;
}

void LogFileWriter::run() noexcept {
	bool in_flight = false;
	try {
		File file;
		std::uint64_t generation = 0;
		bool ready = false;
		auto failed = [&] {
			std::lock_guard lock(mutex_);
			if (generation == generation_) {
				unavailable_.store(true);
			}
			failures_.fetch_add(1);
		};
		for (;;) {
			LogRecord record;
			std::array<char, 128> path{};
			bool changed = false, have_record = false;
			{
				std::unique_lock lock(mutex_);
				wake_.wait(lock, [&] {
					return stop_ || size_ || generation != generation_;
				});
				if (stop_) {
					return;
				}
				changed = generation != generation_;
				generation = generation_;
				path = path_;
				if (size_) {
					record = records_[head_];
					head_ = (head_ + 1) % records_.size();
					--size_;
					have_record = true;
					in_flight = record.event != LogEvent::log_summary;
				}
			}
			if (changed) {
				file.close();
				ready = false;
				if (path[0]) {
					ready = file.open(base_, path.data());
					if (!ready) {
						file.close();
						failed();
					}
				}
			}
			if (!have_record) {
				continue;
			}
			if (!ready) {
				if (in_flight) {
					dropped_.fetch_add(1);
				}
				in_flight = false;
				continue;
			}
			char event[512], line[576];
			format_log_record(event, record);
			auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			std::tm utc{};
#ifdef _WIN32
			gmtime_s(&utc, &now);
#else
			gmtime_r(&now, &utc);
#endif
			char stamp[32]{};
			std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &utc);
			auto length = std::snprintf(line, sizeof line, "%s %s\n", stamp, event);
			if (length <= 0 || (gate_ && !gate_(gate_context_)) ||
			    !file.write(
			        std::string_view(line, std::min<std::size_t>(length, sizeof line - 1)))) {
				if (in_flight) {
					dropped_.fetch_add(1);
				}
				failed();
				ready = false;
				file.close();
			}
			in_flight = false;
		}
	} catch (...) {
		if (in_flight) {
			dropped_.fetch_add(1);
		}
		failures_.fetch_add(1);
		unavailable_.store(true);
		// Wait for destruction; a terminated writer must not silently accumulate records.
		std::unique_lock lock(mutex_);
		dead_ = true;
		for (;;) {
			discard_queued();
			if (stop_) {
				return;
			}
			wake_.wait(lock);
		}
	}
}

} // namespace pg
