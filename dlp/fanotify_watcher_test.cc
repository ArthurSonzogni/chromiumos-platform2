// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/fanotify_watcher.h"

#include <memory>
#include <sys/fanotify.h>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback.h"
#include "base/test/task_environment.h"
#include "gtest/gtest.h"

namespace dlp {

namespace {
constexpr int kInode = 1;
constexpr time_t kCrtime = 2;
constexpr int kPid = 3;
}  // namespace

class FanotifyWatcherTest : public ::testing::Test,
                            public FanotifyWatcher::Delegate {
 public:
  FanotifyWatcherTest() = default;
  FanotifyWatcherTest(const FanotifyWatcherTest&) = delete;
  FanotifyWatcherTest& operator=(const FanotifyWatcherTest&) = delete;
  ~FanotifyWatcherTest() override = default;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(base::CreatePipe(&perm_fd_out_, &perm_fd_in_));
    watcher_ = std::make_unique<FanotifyWatcher>(this, perm_fd_in_.release(),
                                                 /*fanotify_notif_fd=*/-1);
  }

  void ProcessFileOpenRequest(
      FileId id, int pid, base::OnceCallback<void(bool)> callback) override {
    EXPECT_EQ(id.first, kInode);
    EXPECT_EQ(id.second, kCrtime);
    EXPECT_EQ(pid, kPid);
    counter_++;
    std::move(callback).Run(file_open_allowed_);
  }

  void OnFileDeleted(ino64_t inode) override {
    // Not expected to be called.
    FAIL();
  };

  void OnFanotifyError(FanotifyError error) override {
    // Not expected to be called.
    FAIL();
  };

  bool SimulateFileOpen(int expected_counter) {
    base::FilePath temp_file;
    base::ScopedFD file_fd = base::CreateAndOpenFdForTemporaryFileInDir(
        temp_dir_.GetPath(), &temp_file);
    const int fd = file_fd.get();
    watcher_->OnFileOpenRequested(
        kInode, kCrtime, kPid, std::move(file_fd),
        std::make_unique<FanotifyReaderThread::FanotifyReplyWatchdog>());
    EXPECT_EQ(counter_, expected_counter);

    struct fanotify_response response = {};
    HANDLE_EINTR(read(perm_fd_out_.get(), &response, sizeof(response)));
    EXPECT_EQ(response.fd, fd);
    return response.response == FAN_ALLOW;
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  bool file_open_allowed_ = true;
  std::unique_ptr<FanotifyWatcher> watcher_;

 private:
  uint32_t counter_ = 0;
  base::ScopedTempDir temp_dir_;
  base::ScopedFD perm_fd_in_, perm_fd_out_;
};

TEST_F(FanotifyWatcherTest, FileOpen) {
  int requests_counter = 0;
  file_open_allowed_ = true;

  // Allowed when not active.
  EXPECT_TRUE(SimulateFileOpen(requests_counter));

  // Allowed when processed.
  watcher_->SetActive(true);
  requests_counter++;
  EXPECT_TRUE(SimulateFileOpen(requests_counter));

  // Allowed when disactivated.
  watcher_->SetActive(false);
  EXPECT_TRUE(SimulateFileOpen(requests_counter));

  // Not allowed when processed.
  file_open_allowed_ = false;
  watcher_->SetActive(true);
  requests_counter++;
  EXPECT_FALSE(SimulateFileOpen(requests_counter));

  // Allowed when disactivated again.
  watcher_->SetActive(false);
  EXPECT_TRUE(SimulateFileOpen(requests_counter));
}

}  // namespace dlp
