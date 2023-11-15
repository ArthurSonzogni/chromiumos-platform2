// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_TRAINING_CONDITION_H_
#define FEDERATED_TRAINING_CONDITION_H_

namespace federated {

// A virtual class defining the interface for a training condition that are
// checked in DeviceStatusMonitor to control training
class TrainingCondition {
 public:
  virtual ~TrainingCondition() = default;
  // Called before new jobs to see if the device is in a good condition
  [[nodiscard]] virtual bool IsTrainingConditionSatisfiedToStart() const = 0;

  // Called during the training to see if the existing jobs should be aborted.
  [[nodiscard]] virtual bool IsTrainingConditionSatisfiedToContinue() const = 0;
};

}  // namespace federated

#endif  // FEDERATED_TRAINING_CONDITION_H_
