// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_FLOATING_POINT_ACCURACY_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_FLOATING_POINT_ACCURACY_H_

namespace diagnostics {

class FloatingPointAccuracyDelegate {
 public:
  FloatingPointAccuracyDelegate() = default;
  FloatingPointAccuracyDelegate(const FloatingPointAccuracyDelegate&) = delete;
  FloatingPointAccuracyDelegate& operator=(
      const FloatingPointAccuracyDelegate&) = delete;
  virtual ~FloatingPointAccuracyDelegate() = default;

  // Executes floating point accuracy task. Returns true if test is completed
  // without any error, false otherwise.
  bool Run();
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_FLOATING_POINT_ACCURACY_H_
