// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "chromeos/test_helpers.h"
#include "crash-reporter/udev_collector.h"
#include "gtest/gtest.h"

namespace {

// Dummy log config file name.
const char kLogConfigFileName[] = "log_config_file";

// A bunch of random rules to put into the dummy log config file.
const char kLogConfigFileContents[] =
    "crash_reporter-udev-collection-change-card0-drm:echo change card0 drm\n"
    "crash_reporter-udev-collection-add-state0-cpu:echo change state0 cpu\n"
    "cros_installer:echo not for udev";

void CountCrash() {}

bool s_consent_given = true;

bool IsMetrics() {
  return s_consent_given;
}

// Returns the number of compressed crash log files found in the given path.
int GetNumLogFiles(const FilePath& path) {
  file_util::FileEnumerator enumerator(path, false,
                                       file_util::FileEnumerator::FILES,
                                       "*.log.gz");
  int num_files = 0;
  for (FilePath file_path = enumerator.Next();
       !file_path.value().empty();
       file_path = enumerator.Next()) {
    num_files++;
  }
  return num_files;
}

}  // namespace

class UdevCollectorTest : public ::testing::Test {
  void SetUp() {
    s_consent_given = true;

    collector_.reset(new UdevCollector);
    collector_->Initialize(CountCrash, IsMetrics);

    temp_dir_generator_.reset(new base::ScopedTempDir());
    ASSERT_TRUE(temp_dir_generator_->CreateUniqueTempDir());
    EXPECT_TRUE(temp_dir_generator_->IsValid());

    FilePath log_config_path =
        temp_dir_generator_->path().Append(kLogConfigFileName);
    collector_->log_config_path_ = log_config_path;
    collector_->ForceCrashDirectory(
        temp_dir_generator_->path().value().c_str());

    // Write to a dummy log config file.
    ASSERT_EQ(strlen(kLogConfigFileContents),
              file_util::WriteFile(log_config_path,
                                   kLogConfigFileContents,
                                   strlen(kLogConfigFileContents)));

    chromeos::ClearLog();
  }

 protected:
  scoped_ptr<UdevCollector> collector_;
  scoped_ptr<base::ScopedTempDir> temp_dir_generator_;
};

TEST_F(UdevCollectorTest, TestNoConsent) {
  s_consent_given = false;
  collector_->HandleCrash("ACTION=change:KERNEL=card0:SUBSYSTEM=drm");
  EXPECT_EQ(0, GetNumLogFiles(temp_dir_generator_->path()));
}

TEST_F(UdevCollectorTest, TestNoMatch) {
  // No rule should match this.
  collector_->HandleCrash("ACTION=change:KERNEL=foo:SUBSYSTEM=bar");
  EXPECT_EQ(0, GetNumLogFiles(temp_dir_generator_->path()));
}

TEST_F(UdevCollectorTest, TestMatches) {
  // Try multiple udev events in sequence.  The number of log files generated
  // should increase.
  collector_->HandleCrash("ACTION=change:KERNEL=card0:SUBSYSTEM=drm");
  EXPECT_EQ(1, GetNumLogFiles(temp_dir_generator_->path()));
  collector_->HandleCrash("ACTION=add:KERNEL=state0:SUBSYSTEM=cpu");
  EXPECT_EQ(2, GetNumLogFiles(temp_dir_generator_->path()));
}

// TODO(sque, crosbug.com/32238) - test wildcard cases, multiple identical udev
// events.

int main(int argc, char **argv) {
  SetUpTests(&argc, argv, false);
  return RUN_ALL_TESTS();
}
