// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/process.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <poll.h>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/process/kill.h>
#include <base/strings/strcat.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>

#include "cros-disks/quote.h"
#include "cros-disks/sandboxed_init.h"

namespace cros_disks {
namespace {

// Reads chunks of data from a pipe and splits the read data into lines.
class PipeReader {
 public:
  PipeReader(base::ScopedFD fd, std::vector<std::string>* output)
      : fd_(std::move(fd)), output_(output) {
    PCHECK(base::SetNonBlocking(fd_.get()));
  }

  ~PipeReader() {
    if (!remaining_.empty()) {
      LOG(INFO) << "Got line: " << quote(remaining_);
      output_->push_back(remaining_);
    }
  }

  void Append(std::string_view data) {
    LOG(INFO) << ">> Processing: " << quote(data);
    for (std::string_view::size_type i;
         (i = data.find_first_of('\n')) != std::string_view::npos;) {
      remaining_ += data.substr(0, i);
      LOG(INFO) << "Got line: " << quote(remaining_);
      output_->push_back(remaining_);
      remaining_.clear();

      data.remove_prefix(i + 1);
    }

    remaining_ += data;
  }

  // Reads and processes as much data as possible from the given file
  // descriptor. Returns true if there might be more data to read in the future,
  // or false if the end of stream has definitely been reached.
  bool Read() {
    if (!fd_.is_valid())
      return false;

    const int fd = fd_.get();

    while (true) {
      char buffer[PIPE_BUF];
      const ssize_t n = read(fd, buffer, PIPE_BUF);

      if (n < 0) {
        // Error reading.
        switch (errno) {
          case EAGAIN:
          case EINTR:
            // It's Ok to try again later.
            PLOG(INFO) << "Nothing to read from file descriptor " << fd
                       << " for the time being";
            return true;
        }

        PLOG(ERROR) << "Cannot read from file descriptor " << fd;
        fd_.reset();
        return false;
      }

      if (n == 0) {
        // End of stream.
        LOG(INFO) << "End of stream from file descriptor " << fd;
        fd_.reset();
        return false;
      }

      DCHECK_GT(n, 0);
      DCHECK_LE(n, PIPE_BUF);
      Append(std::string_view(buffer, n));
    }
  }

  // Waits for something to read from the given file descriptor (with a timeout
  // of 100ms), and reads and processes as much data as possible. Returns true
  // if there might be more data to read in the future, or false if the end of
  // stream has definitely been reached.
  bool WaitAndRead() {
    if (!fd_.is_valid())
      return false;

    const int fd = fd_.get();

    struct pollfd pfd {
      fd, POLLIN, 0
    };

    const int ret = poll(&pfd, 1, 100 /* milliseconds */);

    if (ret < 0) {
      // Error.
      PLOG(ERROR) << "Cannot poll file descriptor " << fd;
      CHECK_EQ(errno, EINTR);
      return true;
    }

    if (ret == 0) {
      // Nothing to do / timeout.
      LOG(INFO) << "Nothing to do with file descriptor " << fd
                << " for the time being";
      return true;
    }

    return Read();
  }

 private:
  base::ScopedFD fd_;
  std::vector<std::string>* const output_;
  std::string remaining_;
};

// Opens /dev/null. Dies in case of error.
base::ScopedFD OpenNull() {
  const int ret = open("/dev/null", O_WRONLY);
  PLOG_IF(FATAL, ret < 0) << "Cannot open /dev/null";
  return base::ScopedFD(ret);
}

// Creates a pipe holding the given string and returns a file descriptor to the
// read end of this pipe. If the given string is too big to fit into the pipe's
// buffer, it is truncated.
base::ScopedFD WrapStdIn(const base::StringPiece in) {
  SubprocessPipe p(SubprocessPipe::kParentToChild);

  CHECK(base::SetNonBlocking(p.parent_fd.get()));
  const ssize_t n =
      HANDLE_EINTR(write(p.parent_fd.get(), in.data(), in.size()));
  if (n < 0) {
    PLOG(ERROR) << "Cannot write to pipe";
  } else if (n < in.size()) {
    LOG(ERROR) << "Short write to pipe: Wrote " << n << " bytes instead of "
               << in.size() << " bytes";
  }

  return std::move(p.child_fd);
}

}  // namespace

// static
const pid_t Process::kInvalidProcessId = -1;
const int Process::kInvalidFD = base::ScopedFD::traits_type::InvalidValue();

Process::Process() = default;

Process::~Process() = default;

void Process::AddArgument(std::string argument) {
  DCHECK(arguments_array_.empty());
  arguments_.push_back(std::move(argument));
}

char* const* Process::GetArguments() {
  if (arguments_array_.empty())
    BuildArgumentsArray();

  return arguments_array_.data();
}

void Process::BuildArgumentsArray() {
  for (std::string& argument : arguments_) {
    // TODO(fdegros) Remove const_cast when using C++17
    arguments_array_.push_back(const_cast<char*>(argument.data()));
  }

  arguments_array_.push_back(nullptr);
}

void Process::AddEnvironmentVariable(const base::StringPiece name,
                                     const base::StringPiece value) {
  DCHECK(environment_array_.empty());
  DCHECK(!name.empty());
  std::string s;
  s.reserve(name.size() + value.size() + 1);
  s.append(name.data(), name.size());
  s += '=';
  s.append(value.data(), value.size());
  environment_.push_back(std::move(s));
}

char* const* Process::GetEnvironment() {
  // If there are no extra environment variables, just use the current
  // environment.
  if (environment_.empty()) {
    return environ;
  }

  if (environment_array_.empty()) {
    // Prepare the new environment.
    for (std::string& s : environment_) {
      // TODO(fdegros) Remove const_cast when using C++17
      environment_array_.push_back(const_cast<char*>(s.data()));
    }

    // Append the current environment.
    for (char* const* p = environ; *p; ++p) {
      environment_array_.push_back(*p);
    }

    environment_array_.push_back(nullptr);
  }

  return environment_array_.data();
}

bool Process::Start(base::ScopedFD in_fd, base::ScopedFD out_fd) {
  CHECK_EQ(kInvalidProcessId, pid_);
  CHECK(!finished());
  CHECK(!arguments_.empty()) << "No arguments provided";
  LOG(INFO) << "Starting program " << quote(arguments_.front())
            << " with arguments " << quote(arguments_);
  LOG_IF(INFO, !environment_.empty())
      << "and extra environment " << quote(environment_);
  pid_ = StartImpl(std::move(in_fd), std::move(out_fd));
  return pid_ != kInvalidProcessId;
}

bool Process::Start() {
  return Start(WrapStdIn(input_), OpenNull());
}

int Process::Wait() {
  if (finished()) {
    return exit_code_;
  }

  CHECK_NE(kInvalidProcessId, pid_);
  exit_code_ = WaitImpl();
  CHECK(finished());
  pid_ = kInvalidProcessId;
  return exit_code_;
}

bool Process::IsFinished() {
  if (finished()) {
    return true;
  }

  CHECK_NE(kInvalidProcessId, pid_);
  exit_code_ = WaitNonBlockingImpl();
  return finished();
}

int Process::Run(std::vector<std::string>* output) {
  DCHECK(output);

  base::ScopedFD out_fd;
  if (!Start(WrapStdIn(input_),
             SubprocessPipe::Open(SubprocessPipe::kChildToParent, &out_fd))) {
    return -1;
  }

  Communicate(output, std::move(out_fd));

  const int result = Wait();

  LOG(INFO) << "Process finished with return code " << result;
  if (LOG_IS_ON(INFO) && !output->empty()) {
    LOG(INFO) << "Process outputted " << output->size() << " lines:";
    for (const std::string& line : *output) {
      LOG(INFO) << line;
    }
  }

  return result;
}

void Process::Communicate(std::vector<std::string>* output,
                          base::ScopedFD out_fd) {
  DCHECK(output);

  PipeReader reader(std::move(out_fd), output);

  // Poll process and pipe. Read from pipe when possible.
  while (!IsFinished() && reader.WaitAndRead())
    continue;

  // Really wait for process to finish.
  Wait();

  // Final read from pipe after process finished.
  reader.Read();
}

}  // namespace cros_disks
