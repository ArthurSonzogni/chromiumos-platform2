// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootsplash/bootsplash_daemon.h"
#include "bootsplash/paths.h"

#include <string>
#include <sysexits.h>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <brillo/timers/alarm_timer.h>

#include <gmock/gmock.h>
#include <gmock/gmock-actions.h>
#include <gtest/gtest.h>

using base::FilePath;

namespace bootsplash {

namespace {

class AlarmTimerTester {
 public:
  AlarmTimerTester(bool* did_run,
                   base::TimeDelta delay,
                   base::OnceClosure quit_closure)
      : did_run_(did_run),
        quit_closure_(std::move(quit_closure)),
        delay_(delay),
        timer_(brillo::timers::SimpleAlarmTimer::CreateForTesting()) {}
  AlarmTimerTester(const AlarmTimerTester&) = delete;
  AlarmTimerTester& operator=(const AlarmTimerTester&) = delete;

  void Start() {
    timer_->Start(
        FROM_HERE, delay_,
        base::BindRepeating(&AlarmTimerTester::Run, base::Unretained(this)));
  }

 private:
  void Run() {
    *did_run_ = true;
    if (quit_closure_)
      std::move(quit_closure_).Run();
  }

  bool* did_run_;
  base::OnceClosure quit_closure_;
  const base::TimeDelta delay_;
  std::unique_ptr<brillo::timers::SimpleAlarmTimer> timer_;
};
}  // namespace

class BootSplashDaemonMock : public BootSplashDaemon {
 public:
  BootSplashDaemonMock() : BootSplashDaemon(false) {
    OverrideBootLogoAnimationAlarmForTesting();
  }
  MOCK_METHOD(void, DBusDaemonInit, (), (override));
};

class BootSplashDaemonTest : public ::testing::Test {
 protected:
  const FilePath& test_dir() const { return scoped_temp_dir_.GetPath(); }
  const FilePath& frecon_vt_path() const { return frecon_vt_path_; }

 protected:
  void SetUp() override {
    EXPECT_CALL(boot_splash_daemon_, DBusDaemonInit())
        .WillRepeatedly(testing::Return());

    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    paths::SetPrefixForTesting(test_dir());

    // Create an empty vt0 file to Write() to, so the Frecon object can be
    // created.
    frecon_vt_path_ = paths::Get(paths::kFreconVt);
    ASSERT_TRUE(base::CreateDirectory(frecon_vt_path_.DirName()));
    ASSERT_TRUE(base::WriteFile(frecon_vt_path_, ""));

    // Create hi_res file to that's initially "0" (low res).
    frecon_hi_res_path_ = paths::Get(paths::kFreconHiRes);
    ASSERT_TRUE(base::CreateDirectory(frecon_hi_res_path_.DirName()));
    ASSERT_TRUE(base::WriteFile(frecon_hi_res_path_, "0"));

    // Create an empty boot splash assets directory, so the Frecon object can be
    // created.
    boot_splash_assets_dir_ = paths::GetBootSplashAssetsDir(false);
    ASSERT_TRUE(base::CreateDirectory(boot_splash_assets_dir_));
  }

  BootSplashDaemonMock boot_splash_daemon_;
  base::ScopedTempDir scoped_temp_dir_;
  base::FilePath frecon_vt_path_;
  base::FilePath frecon_hi_res_path_;
  base::FilePath boot_splash_assets_dir_;
};

TEST_F(BootSplashDaemonTest, DBusDaemonInit) {
  // Verify DBus can be initialized without errors.
  boot_splash_daemon_.DBusDaemonInit();
}

TEST_F(BootSplashDaemonTest, InitBootSplash) {
  ASSERT_EQ(boot_splash_daemon_.OnInit(), EX_OK);
  ASSERT_EQ(boot_splash_daemon_.InitBootSplash(), EX_OK);
}

TEST_F(BootSplashDaemonTest, OnShutdown) {
  // Verify BootSplashDaemon can be shutdown without errors.
  int return_code = -1;
  boot_splash_daemon_.OnShutdown(&return_code);
  ASSERT_EQ(return_code, EX_OK);
}

TEST_F(BootSplashDaemonTest, SessionManagerLoginPromptVisibleEventReceived) {
  // Verify BootSplashDaemon can quit without errors.
  boot_splash_daemon_.SessionManagerLoginPromptVisibleEventReceived();
}

TEST_F(BootSplashDaemonTest, OnBootLogoAnimationAlarmFired) {
  ASSERT_EQ(boot_splash_daemon_.OnInit(), EX_OK);

  // Record the previous file contents, which will be appended to.
  std::string prev_file_contents;
  ASSERT_TRUE(ReadFileToString(frecon_vt_path(), &prev_file_contents));

  // Handle the boot logo alarm firing. This should write the next image file to
  // the frecon VT file.
  boot_splash_daemon_.OnBootLogoAnimationAlarmFired();

  // Build the expected image name.
  std::string imageFileName =
      base::StringPrintf("%s%02d%s", paths::kBootSplashFilenamePrefix,
                         0,  // BootSplashDaemon starts with frame 0
                         paths::kImageExtension);
  base::FilePath imagePath =
      paths::Get(boot_splash_assets_dir_.value()).Append(imageFileName);
  // Build the expected file contents.
  std::string expected_contents =
      prev_file_contents + "\033]image:file=" + imagePath.value() + "\a";

  // Validate the new data was written.
  std::string curr_file_contents;
  ASSERT_TRUE(ReadFileToString(frecon_vt_path(), &curr_file_contents));
  EXPECT_EQ(curr_file_contents, expected_contents);
}

}  // namespace bootsplash
