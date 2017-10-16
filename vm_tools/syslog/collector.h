// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SYSLOG_COLLECTOR_H_
#define VM_TOOLS_SYSLOG_COLLECTOR_H_

#include <memory>

#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/message_loop/message_loop.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <google/protobuf/arena.h>

#include "host.grpc.pb.h"  // NOLINT(build/include)

namespace vm_tools {
namespace syslog {

// Responsible for listening on /dev/log for any userspace applications that
// wish to log messages with the system syslog.  TODO(chirantan):  This
// currently doesn't handle kernel oops or flushing during shutdown.
class Collector : public base::MessageLoopForIO::Watcher {
 public:
  // Create a new, initialized Collector.
  static std::unique_ptr<Collector> Create();
  ~Collector() = default;

  // base::MessageLoopForIO::Watcher overrides.
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override;

  static std::unique_ptr<Collector> CreateForTesting(
      base::ScopedFD syslog_fd,
      base::ScopedFD kmsg_fd,
      base::Time boot_time,
      std::unique_ptr<vm_tools::LogCollector::Stub> stub);

 private:
  // Private default constructor.  Use the static factory function to create new
  // instances of this class.
  Collector();

  // Initializes this Collector.  Starts listening on the syslog socket
  // and sets up timers to periodically flush logs out.
  bool Init();

  // Called periodically to flush any logs that have been buffered.
  void FlushLogs();

  // Reads one log record from the socket and adds it to |syslog_request_|.
  // Returns true if there may still be more data to read from the socket.
  bool ReadOneSyslogRecord();

  // Reads one kernel log record from |kmsg_fd_| and adds it to |kmsg_request_|.
  // Returns true if there may still be more data to be read from the fd.
  bool ReadOneKernelRecord();

  // Initializes this Collector for tests.  Starts listening on the
  // provided file descriptor instead of creating a socket and binding to a
  // path on the file system.
  bool InitForTesting(base::ScopedFD syslog_fd,
                      base::ScopedFD kmsg_fd,
                      base::Time boot_time,
                      std::unique_ptr<vm_tools::LogCollector::Stub> stub);

  // File descriptor bound to /dev/log.
  base::ScopedFD syslog_fd_;
  base::MessageLoopForIO::FileDescriptorWatcher syslog_controller_;

  // File descriptor for listening to /dev/kmsg.
  base::ScopedFD kmsg_fd_;
  base::MessageLoopForIO::FileDescriptorWatcher kmsg_controller_;

  // Time that the VM booted.  Used to convert kernel timestamps to localtime.
  base::Time boot_time_;

  // Shared arena used for allocating log records.
  google::protobuf::Arena arena_;

  // Non-owning pointer to the current syslog LogRequest.  Owned by arena_.
  vm_tools::LogRequest* syslog_request_;

  // Non-owning pointer to the current kernel log LogRequest.  Owend by arena_.
  vm_tools::LogRequest* kmsg_request_;

  // Size of all the currently buffered log records.
  size_t buffered_size_;

  // File descriptor for the file used to keep track of the last flushed kernel
  // log message.
  base::ScopedFD kernel_sequence_fd_;

  // Sequence number of the last kernel log message that was sent to the host.
  uint64_t kernel_sequence_ = 0;

  // Connection to the LogCollector service on the host.
  std::unique_ptr<vm_tools::LogCollector::Stub> stub_;

  // Timer used for periodically flushing buffered log records.
  base::RepeatingTimer timer_;

  base::WeakPtrFactory<Collector> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(Collector);
};

}  // namespace syslog
}  // namespace vm_tools

#endif  // VM_TOOLS_SYSLOG_COLLECTOR_H_
