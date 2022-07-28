// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_MEMORY_MEMORY_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_MEMORY_MEMORY_CONSTANTS_H_

namespace diagnostics {

// Different bit flags which can be encoded in the return value for memtester.
// See https://linux.die.net/man/8/memtester for details. Note that this is not
// an enum class so that it can be implicitly converted to a bit flag.
enum MemtesterErrorCodes {
  // An error allocating or locking memory, or invoking the memtester binary.
  kAllocatingLockingInvokingError = 0x01,
  // Stuck address test found an error.
  kStuckAddressTestError = 0x02,
  // Any test other than the stuck address test found an error.
  kOtherTestError = 0x04,
};

// Ensure the operating system is left with at least the following size to avoid
// out of memory error.
constexpr int kMemoryRoutineReservedSizeKiB = 500 * 1024;  // 500 MiB

// Status messages the memory routine can report.
inline constexpr char kMemoryRoutineSucceededMessage[] =
    "Memory routine passed.";
inline constexpr char kMemoryRoutineRunningMessage[] = "Memory routine running";
inline constexpr char kMemoryRoutineCancelledMessage[] =
    "Memory routine cancelled.";

// Error messages for memtester precondition.
inline constexpr char kMemoryRoutineMemtesterAlreadyRunningMessage[] =
    "Error Memtester process already running.";
inline constexpr char kMemoryRoutineFetchingAvailableMemoryFailureMessage[] =
    "Error fetching available memory.\n";
inline constexpr char kMemoryRoutineNotHavingEnoughAvailableMemoryMessage[] =
    "Error not having enough available memory.\n";

// Error messages for memtester failure.
inline constexpr char kMemoryRoutineAllocatingLockingInvokingFailureMessage[] =
    "Error allocating or locking memory, or invoking the memtester binary.\n";
inline constexpr char kMemoryRoutineStuckAddressTestFailureMessage[] =
    "Error during the stuck address test.\n";
inline constexpr char kMemoryRoutineOtherTestFailureMessage[] =
    "Error during a test other than the stuck address test.\n";

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_MEMORY_MEMORY_CONSTANTS_H_
