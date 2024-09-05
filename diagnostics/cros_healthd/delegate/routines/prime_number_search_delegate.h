// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_PRIME_NUMBER_SEARCH_DELEGATE_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_PRIME_NUMBER_SEARCH_DELEGATE_H_

#include <bitset>
#include <cstdint>

#include "diagnostics/cros_healthd/delegate/routines/cpu_routine_task_delegate.h"

namespace diagnostics {

class PrimeNumberSearchDelegate : public CpuRoutineTaskDelegate {
 public:
  explicit PrimeNumberSearchDelegate(uint64_t max_num);
  PrimeNumberSearchDelegate(const PrimeNumberSearchDelegate&) = delete;
  PrimeNumberSearchDelegate& operator=(const PrimeNumberSearchDelegate&) =
      delete;
  ~PrimeNumberSearchDelegate() override;

  virtual bool IsPrime(uint64_t num) const;

  // CpuRoutineTaskDelegate overrides.
  bool Run() override;

  // Largest number that routine will calculate prime numbers up to.
  static constexpr uint64_t kMaxPrimeNumber = 1000000;

 private:
  const uint64_t max_num_ = 0;
  std::bitset<kMaxPrimeNumber + 1> prime_sieve_ =
      std::bitset<kMaxPrimeNumber + 1>().set();
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_PRIME_NUMBER_SEARCH_DELEGATE_H_
