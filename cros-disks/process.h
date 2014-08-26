// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_PROCESS_H_
#define CROS_DISKS_PROCESS_H_

#include <string>
#include <vector>

#include <base/basictypes.h>
#include <gtest/gtest_prod.h>

namespace cros_disks {

// A base class for executing a process.
//
// TODO(benchan): This base class is not feature complete yet.
class Process {
 public:
  // Invalid process ID assigned to a process that has not started.
  static const pid_t kInvalidProcessId;

  virtual ~Process();

  // Adds an argument to the end of the argument list. Any argument added by
  // this method does not affect the process that has been started by Start().
  void AddArgument(const std::string& argument);

  // Implemented by a derived class to start the process without waiting for
  // it to terminate. Returns true on success.
  virtual bool Start() = 0;

  pid_t pid() const { return pid_; }

 protected:
  Process();

  // Returns the array of arguments used to start the process, or NULL if
  // no arguments is added using AddArgument(). This method calls
  // BuildArgumentsArray() to build |arguments_array_| only once (i.e.
  // when |arguments_array_| is null). Once |arguments_array_| is built,
  // subsequent calls to AddArgument() do not change the return value of
  // this method. The returned array of arguments is managed by the base
  // class.
  char** GetArguments();

  void set_pid(pid_t pid) { pid_ = pid; }

 private:
  // Builds |arguments_array_| and |arguments_buffer_| from |arguments_|.
  // Existing values of |arguments_array_| and |arguments_buffer_| are
  // overridden. Return false if |arguments_| is empty.
  bool BuildArgumentsArray();

  // Process arguments.
  std::vector<std::string> arguments_;
  std::vector<char*> arguments_array_;
  std::vector<char> arguments_buffer_;

  // Process ID (default to kInvalidProcessId when the process has not started).
  pid_t pid_;

  FRIEND_TEST(ProcessTest, GetArguments);
  FRIEND_TEST(ProcessTest, GetArgumentsWithNoArgumentsAdded);

  DISALLOW_COPY_AND_ASSIGN(Process);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_PROCESS_H_
