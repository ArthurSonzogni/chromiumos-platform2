// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SPACED_CALCULATOR_STATEFUL_FREE_SPACE_CALCULATOR_H_
#define SPACED_CALCULATOR_STATEFUL_FREE_SPACE_CALCULATOR_H_

#include <spaced/calculator/calculator.h>

#include <sys/stat.h>
#include <sys/statvfs.h>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <base/task/sequenced_task_runner.h>
#include <brillo/blkdev_utils/lvm.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <spaced/proto_bindings/spaced.pb.h>

namespace spaced {

class BRILLO_EXPORT StatefulFreeSpaceCalculator : public Calculator {
 public:
  StatefulFreeSpaceCalculator(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      std::optional<brillo::Thinpool> thinpool,
      base::RepeatingCallback<void(const StatefulDiskSpaceUpdate&)> signal);
  ~StatefulFreeSpaceCalculator() override = default;

  void Start();

 protected:
  // Runs statvfs() on a given path.
  virtual int StatVFS(const base::FilePath& path, struct statvfs* st);

 private:
  friend class StatefulFreeSpaceCalculatorTest;
  FRIEND_TEST(StatefulFreeSpaceCalculatorTest, StatVfsError);
  FRIEND_TEST(StatefulFreeSpaceCalculatorTest, NoThinpoolCalculator);
  FRIEND_TEST(StatefulFreeSpaceCalculatorTest, ThinpoolCalculator);
  FRIEND_TEST(StatefulFreeSpaceCalculatorTest, SignalStatefulDiskSpaceUpdate);

  // Updates the amount of free space available on the stateful partition.
  void UpdateSize();

  // Signal an update on the disk space state.
  void SignalDiskSpaceUpdate();

  // Update size and emit a signal.
  void UpdateSizeAndSignal();

  // Schedules the next update depending on the amount of free space.
  void ScheduleUpdate(base::TimeDelta delay);

  std::optional<brillo::Thinpool> thinpool_;
  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  base::RepeatingCallback<void(const StatefulDiskSpaceUpdate&)> signal_;

  base::WeakPtrFactory<StatefulFreeSpaceCalculator> weak_ptr_factory_{this};
};

}  // namespace spaced

#endif  // SPACED_CALCULATOR_STATEFUL_FREE_SPACE_CALCULATOR_H_
