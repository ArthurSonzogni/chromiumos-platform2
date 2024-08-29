// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_PRIME_NUMBER_SEARCH_DELEGATE_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_PRIME_NUMBER_SEARCH_DELEGATE_H_

namespace diagnostics {

class PrimeNumberSearchDelegate {
 public:
  virtual ~PrimeNumberSearchDelegate() = default;

  // Executes prime number search task. Returns true if searching is completed
  // without any error, false otherwise.
  virtual bool Run() = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_PRIME_NUMBER_SEARCH_DELEGATE_H_
