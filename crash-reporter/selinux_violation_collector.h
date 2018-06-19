// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_SELINUX_VIOLATION_COLLECTOR_H_
#define CRASH_REPORTER_SELINUX_VIOLATION_COLLECTOR_H_

#include <map>
#include <string>

#include <base/files/file_util.h>
#include <base/macros.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "crash-reporter/crash_collector.h"

// SELinux violation collector.
class SELinuxViolationCollector : public CrashCollector {
 public:
  SELinuxViolationCollector();

  ~SELinuxViolationCollector() override;

  // Collects warning.
  bool Collect();

 protected:
  void set_violation_report_path_for_testing(const base::FilePath& file_path) {
    violation_report_path_ = file_path;
  }

  void set_developer_image_for_testing() {
    developer_image_for_testing_ = true;
  }

  void set_fake_random_for_statistic_sampling(int fake_random) {
    fake_random_for_statistic_sampling_ = fake_random;
  }

 private:
  friend class SELinuxViolationCollectorTest;
  FRIEND_TEST(SELinuxViolationCollectorTest, CollectOK);
  FRIEND_TEST(SELinuxViolationCollectorTest, CollectSample);
  FRIEND_TEST(SELinuxViolationCollectorTest, DroppedSample);

  base::FilePath violation_report_path_;
  bool developer_image_for_testing_ = false;
  int fake_random_for_statistic_sampling_ = -1;

  bool LoadSELinuxViolation(std::string* content,
                            std::string* signature,
                            std::map<std::string, std::string>* extra_metadata);

  bool ShouldDropThisReport();

  DISALLOW_COPY_AND_ASSIGN(SELinuxViolationCollector);
};

#endif  // CRASH_REPORTER_SELINUX_VIOLATION_COLLECTOR_H_
