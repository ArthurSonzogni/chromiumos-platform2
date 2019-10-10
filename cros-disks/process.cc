// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/process.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include <poll.h>

#include <base/files/file_util.h>
#include <base/posix/eintr_wrapper.h>
#include <base/process/kill.h>
#include <base/strings/string_util.h>
#include <base/strings/string_split.h>
#include <base/time/time.h>

#include "cros-disks/quote.h"

namespace cros_disks {
namespace {

enum class ReadResult {
  kSuccess,
  kWouldBlock,
  kFailure,
};

ReadResult ReadFD(int fd, std::string* data) {
  const size_t kMaxSize = 4096;
  char buffer[kMaxSize];
  ssize_t bytes_read = HANDLE_EINTR(read(fd, buffer, kMaxSize));

  if (bytes_read < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      data->clear();
      return ReadResult::kWouldBlock;
    }
    PLOG(ERROR) << "Read failed.";
    return ReadResult::kFailure;
  }

  data->assign(buffer, bytes_read);
  return ReadResult::kSuccess;
}

// Interleaves streams.
class StreamMerger {
 public:
  explicit StreamMerger(std::vector<std::string>* output) : output_(output) {}

  ~StreamMerger() {
    for (size_t i = 0; i < kStreamCount; ++i) {
      const std::string& remaining = remaining_[i];
      if (!remaining.empty())
        output_->push_back(base::JoinString({kTags[i], ": ", remaining}, ""));
    }
  }

  void Append(const size_t stream, const base::StringPiece data) {
    DCHECK_LT(stream, kStreamCount);

    if (data.empty()) {
      return;
    }

    std::string& remaining = remaining_[stream];
    const base::StringPiece tag = kTags[stream];

    std::vector<base::StringPiece> lines = base::SplitStringPiece(
        data, "\n", base::WhitespaceHandling::KEEP_WHITESPACE,
        base::SplitResult::SPLIT_WANT_ALL);
    const base::StringPiece last_line = lines.back();
    lines.pop_back();

    for (const base::StringPiece line : lines) {
      output_->push_back(base::JoinString({tag, ": ", remaining, line}, ""));
      remaining.clear();
    }

    remaining = last_line.as_string();
  }

 private:
  // Number of streams to interleave.
  static const size_t kStreamCount = 2;
  static const base::StringPiece kTags[kStreamCount];
  std::vector<std::string>* const output_;
  std::string remaining_[kStreamCount];

  DISALLOW_COPY_AND_ASSIGN(StreamMerger);
};

const size_t StreamMerger::kStreamCount;
const base::StringPiece StreamMerger::kTags[kStreamCount] = {"OUT", "ERR"};

}  // namespace

// static
const pid_t Process::kInvalidProcessId = -1;
const int Process::kInvalidFD = base::ScopedFD::traits_type::InvalidValue();

Process::Process() = default;

Process::~Process() = default;

void Process::AddArgument(const std::string& argument) {
  arguments_.push_back(argument);
}

char* const* Process::GetArguments() {
  if (arguments_array_.empty())
    BuildArgumentsArray();

  return arguments_array_.data();
}

bool Process::BuildArgumentsArray() {
  // The following code creates a writable copy of argument strings.
  size_t num_arguments = arguments_.size();
  if (num_arguments == 0)
    return false;

  size_t arguments_buffer_size = 0;
  for (const auto& argument : arguments_) {
    arguments_buffer_size += argument.size() + 1;
  }

  arguments_buffer_.resize(arguments_buffer_size);
  arguments_array_.resize(num_arguments + 1);

  char** array_pointer = arguments_array_.data();
  char* buffer_pointer = arguments_buffer_.data();
  for (const auto& argument : arguments_) {
    *array_pointer = buffer_pointer;
    size_t argument_size = argument.size();
    argument.copy(buffer_pointer, argument_size);
    buffer_pointer[argument_size] = '\0';
    buffer_pointer += argument_size + 1;
    ++array_pointer;
  }
  *array_pointer = nullptr;
  return true;
}

bool Process::Start() {
  CHECK_EQ(kInvalidProcessId, pid_);
  CHECK(!finished());
  CHECK(!arguments_.empty()) << "No arguments provided";
  LOG(INFO) << "Starting process " << quote(arguments_);
  pid_ = StartImpl(&in_fd_, &out_fd_, &err_fd_);
  return pid_ != kInvalidProcessId;
}

int Process::Wait() {
  if (finished()) {
    return status_;
  }

  CHECK_NE(kInvalidProcessId, pid_);
  status_ = WaitImpl();
  CHECK(finished());
  pid_ = kInvalidProcessId;
  return status_;
}

bool Process::IsFinished() {
  if (finished()) {
    return true;
  }

  CHECK_NE(kInvalidProcessId, pid_);
  status_ = WaitNonBlockingImpl();
  return finished();
}

int Process::Run(std::vector<std::string>* output) {
  if (!Start()) {
    return -1;
  }

  if (output) {
    Communicate(output);
  } else {
    in_fd_.reset();
    out_fd_.reset();
    err_fd_.reset();
  }

  const int result = Wait();

  LOG(INFO) << "Process finished with return code " << result;
  if (LOG_IS_ON(INFO) && output && !output->empty()) {
    LOG(INFO) << "Process outputted " << output->size() << " lines:";
    for (const std::string& line : *output) {
      LOG(INFO) << line;
    }
  }

  return result;
}

void Process::Communicate(std::vector<std::string>* output) {
  // We are not going to write there.
  in_fd_.reset();
  // No FD leaves this function alive!
  base::ScopedFD out_fd = std::move(out_fd_);
  base::ScopedFD err_fd = std::move(err_fd_);

  if (out_fd.is_valid()) {
    CHECK(base::SetNonBlocking(out_fd.get()));
  }
  if (err_fd.is_valid()) {
    CHECK(base::SetNonBlocking(err_fd.get()));
  }

  std::string data;
  StreamMerger merger(output);
  std::array<struct pollfd, 2> fds;
  fds[0] = {out_fd.get(), POLLIN, 0};
  fds[1] = {err_fd.get(), POLLIN, 0};

  while (!IsFinished()) {
    size_t still_open = 0;
    for (const auto& f : fds) {
      still_open += f.fd != kInvalidFD;
    }
    if (still_open == 0) {
      // No comms expected anymore.
      break;
    }

    const int ret =
        HANDLE_EINTR(poll(fds.data(), fds.size(), 10 /* milliseconds */));
    if (ret == -1) {
      PLOG(ERROR) << "poll() failed";
      break;
    }

    if (ret) {
      for (size_t i = 0; i < fds.size(); ++i) {
        auto& f = fds[i];
        if (f.revents) {
          if (ReadFD(f.fd, &data) == ReadResult::kFailure) {
            // Failure.
            f.fd = kInvalidFD;
          } else {
            merger.Append(i, data);
          }
        }
      }
    }
  }

  Wait();

  // Final read after process exited.
  for (size_t i = 0; i < fds.size(); ++i) {
    auto& f = fds[i];
    if (f.fd != kInvalidFD) {
      if (ReadFD(f.fd, &data) == ReadResult::kFailure) {
        // Failure.
        f.fd = kInvalidFD;
      } else {
        merger.Append(i, data);
      }
    }
  }
}

}  // namespace cros_disks
