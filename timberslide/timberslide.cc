// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <brillo/daemons/daemon.h>
#include <base/files/file_util.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/strings/string_util.h>
#include <brillo/flag_helper.h>

namespace {

const char kDeviceLogFile[] = "/sys/kernel/debug/cros_ec/console_log";

const char kDefaultLogDirectory[] = "/var/log/";

const char kCurrentLogFile[] = "cros_ec.log";
const char kPreviousLogFile[] = "cros_ec.previous";

const int kMaxCurrentLogSize = 10 * 1024 * 1024;

class TimberSlide : public brillo::Daemon,
                    public base::MessageLoopForIO::Watcher {
 public:
  TimberSlide(const base::FilePath& device_log,
              const base::FilePath& log_dir):
      device_log_(device_log),
      total_size_(0) {
    current_log_ = log_dir.Append(kCurrentLogFile);
    previous_log_ = log_dir.Append(kPreviousLogFile);
  }

 private:
  int OnInit() override {
    LOG(INFO) << "Starting timberslide daemon";
    if (!base::PathExists(device_log_)) {
      LOG(FATAL) << "EC log does not exist!";
      return 1;
    }

    RotateLogs(previous_log_, current_log_);

    device_file_ = base::File(device_log_,
                              base::File::FLAG_OPEN | base::File::FLAG_READ);

    if (!device_file_.IsValid()) {
      LOG(FATAL) << "Open error : " << device_file_.error_details();
      return 1;
    }

    CHECK(base::MessageLoopForIO::current()->WatchFileDescriptor(
              device_file_.GetPlatformFile(), true,
              base::MessageLoopForIO::WATCH_READ, &fd_watcher_, this));
    return 0;
  }

  void OnFileCanWriteWithoutBlocking(int fd) override {
    LOG(FATAL) << "Unexpected call to write event handler";
  }

  void OnFileCanReadWithoutBlocking(int fd) override {
    char buffer[4096];
    int ret;

    CHECK_EQ(fd, device_file_.GetPlatformFile());

    ret = TEMP_FAILURE_RETRY(
        device_file_.ReadAtCurrentPosNoBestEffort(buffer, sizeof(buffer)));
    if (ret == 0)
      return;

    if (ret < 0) {
      LOG(FATAL) << "Read error: " << base::File::ErrorToString(
                                        base::File::OSErrorToFileError(errno));
      return;
    }

    if (!base::AppendToFile(current_log_, buffer, ret)) {
      LOG(FATAL) << "Could not append log file!";
      return;
    }

    total_size_ += ret;
    if (total_size_ >= kMaxCurrentLogSize) {
      RotateLogs(previous_log_, current_log_);
      total_size_ = 0;
    }
  }

  void RotateLogs(const base::FilePath& previous_log,
                  const base::FilePath& current_log) {
    CHECK(base::DeleteFile(previous_log, /* recursive = */ false));

    if (base::PathExists(current_log))
      CHECK(base::Move(current_log, previous_log));

    base::WriteFile(current_log, "", 0);
  }

  base::FilePath device_log_;
  base::File device_file_;
  base::FilePath current_log_;
  base::FilePath previous_log_;
  base::MessageLoopForIO::FileDescriptorWatcher fd_watcher_;
  int total_size_;
};

}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_string(device_log, kDeviceLogFile,
                "File where the recent EC logs are posted to.");
  DEFINE_string(log_directory, kDefaultLogDirectory,
                "Directory where the output logs should be.");
  brillo::FlagHelper::Init(argc, argv,
      "timberslide concatenates EC logs for use in debugging.");

  TimberSlide ts((base::FilePath(FLAGS_device_log)),
                 (base::FilePath(FLAGS_log_directory)));

  return ts.Run();
}
