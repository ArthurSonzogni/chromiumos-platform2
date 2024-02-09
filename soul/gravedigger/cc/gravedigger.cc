// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "soul/gravedigger/cc/gravedigger.h"
#include <sys/types.h>
#include <cstdint>
#include <utility>

#include "src/ffi.rs.h"

namespace {
rust::Str StringViewToRustStr(std::string_view str) {
  return rust::Str(str.data(), str.size());
}
}  // namespace

namespace gravedigger {

bool try_init(std::string_view application_name) {
  return rust::try_init(StringViewToRustStr(application_name));
}

class LogFile::Impl {
 public:
  explicit Impl(::rust::Box<rust::LogFile>&& log_file)
      : log_file_(std::move(log_file)) {}

  uint64_t GetInode() const { return log_file_->get_inode(); }

  int64_t GetLength() const { return log_file_->get_file_length(); }

  int64_t Read(char* data, int64_t size) {
    return rust::read_to_char_ptr(log_file_, data, size);
  }

  int64_t Read(std::vector<unsigned char>& data) {
    return rust::read_to_slice(
        log_file_, ::rust::Slice<unsigned char>(data.data(), data.size()));
  }

  bool SeekToBegin() { return log_file_->seek(rust::SeekLocation::Begin); }

  bool SeekBeforeEnd() {
    return log_file_->seek(rust::SeekLocation::BeforeEnd);
  }

  bool SeekToEnd() { return log_file_->seek(rust::SeekLocation::End); }

 private:
  ::rust::Box<rust::LogFile> log_file_;
};

bool LogFile::PathExists(const base::FilePath& path) {
  return rust::file_path_exists(StringViewToRustStr(path.value()));
}

std::unique_ptr<LogFile> LogFile::Open(const base::FilePath& path) {
  ::rust::Box<rust::CreateResult> log_file =
      rust::new_log_file_from_path(StringViewToRustStr(path.value()));
  if (!log_file->has_value()) {
    return nullptr;
  }
  return std::unique_ptr<LogFile>(new LogFile(
      std::make_unique<Impl>(rust::result_to_logfile(std::move(log_file)))));
}

LogFile::LogFile(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

LogFile::~LogFile() = default;

base::expected<int64_t, int64_t> LogFile::ReadAtCurrentPosition(char* data,
                                                                int64_t size) {
  const int64_t read_bytes = impl_->Read(data, size);
  if (read_bytes >= 0) {
    return base::ok(read_bytes);
  } else {
    return base::unexpected(read_bytes);
  }
}

base::expected<int64_t, int64_t> LogFile::ReadAtCurrentPosition(
    std::vector<unsigned char>& data) {
  const int64_t read_bytes = impl_->Read(data);
  if (read_bytes >= 0) {
    return base::ok(read_bytes);
  } else {
    return base::unexpected(read_bytes);
  }
}

bool LogFile::SeekToBegin() {
  return impl_->SeekToBegin();
}

bool LogFile::SeekBeforeEnd() {
  return impl_->SeekBeforeEnd();
}

bool LogFile::SeekToEnd() {
  return impl_->SeekToEnd();
}

uint64_t LogFile::GetInode() const {
  return impl_->GetInode();
}

int64_t LogFile::GetLength() const {
  return impl_->GetLength();
}

}  // namespace gravedigger
