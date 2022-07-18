// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SPACED_CALCULATOR_STATEFUL_FREE_SPACE_CALCULATOR_H_
#define SPACED_CALCULATOR_STATEFUL_FREE_SPACE_CALCULATOR_H_

#include <spaced/calculator/calculator.h>

#include <sys/stat.h>
#include <sys/statvfs.h>

#include <base/files/file_path.h>
#include <base/timer/timer.h>
#include <brillo/blkdev_utils/lvm.h>

namespace spaced {

class BRILLO_EXPORT StatefulFreeSpaceCalculator : public Calculator {
 public:
  StatefulFreeSpaceCalculator(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      int64_t time_delta_seconds,
      std::optional<brillo::Thinpool> thinpool);
  ~StatefulFreeSpaceCalculator() override { Stop(); }

  void Start();
  void Stop();

 protected:
  // Runs statvfs() on a given path.
  virtual int StatVFS(const base::FilePath& path, struct statvfs* st);

 private:
  friend class StatefulFreeSpaceCalculatorTest;
  FRIEND_TEST(StatefulFreeSpaceCalculatorTest, StatVfsError);
  FRIEND_TEST(StatefulFreeSpaceCalculatorTest, NoThinpoolCalculator);
  FRIEND_TEST(StatefulFreeSpaceCalculatorTest, ThinpoolCalculator);

  void UpdateSize();

  base::RepeatingTimer timer_;
  int64_t time_delta_seconds_;
  std::optional<brillo::Thinpool> thinpool_;
};

}  // namespace spaced

#endif  // SPACED_CALCULATOR_STATEFUL_FREE_SPACE_CALCULATOR_H_
