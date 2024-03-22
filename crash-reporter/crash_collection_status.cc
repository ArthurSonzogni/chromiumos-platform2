// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_collection_status.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "base/strings/string_number_conversions.h"

namespace {
// LINT.IfChange(status_list)
constexpr std::pair<CrashCollectionStatus, std::string_view> kStatusNames[] = {
    {CrashCollectionStatus::kSuccess, "Success"},
    {CrashCollectionStatus::kFinishedEphermeralCollection,
     "Finished ephermeral collection"},
    {CrashCollectionStatus::kUnknownStatus, "Unknown Status"},
    {CrashCollectionStatus::kOutOfCapacity, "Out of capacity"},
    {CrashCollectionStatus::kFailedMetaWrite, "Failed to write .meta"},
    {CrashCollectionStatus::kCreateCrashDirectoryFailed,
     "Failed to create crash directory"},
    {CrashCollectionStatus::kOpenCrashDirectoryFailed,
     "Failed to open crash directory"},
    {CrashCollectionStatus::kGetDefaultUserInfoFailed,
     "Get default user info failed"},
    {CrashCollectionStatus::kFailedCrashNameGroupInfo,
     "Failed to get group info for group kCrashName"},
    {CrashCollectionStatus::kFailedCrashUserGroupNameGroupInfo,
     "Failed to get group info for group kCrashUserGroupName"},
    {CrashCollectionStatus::kFailedCrashGroupNameGroupInfo,
     "Failed to get group info for group kCrashGroupName"},
    {CrashCollectionStatus::kInvalidPayloadName, "Payload had invalid name"},
    {CrashCollectionStatus::kNoUserCrashDirectoryWhenRequired,
     "Crash selection method was kAlwaysUseDaemonStore but user crash "
     "directory was not available"},
    {CrashCollectionStatus::kFailedCrashNameGroupInfoForOld,
     "Failed to get group info for group kCrashName (old code path)"},
    {CrashCollectionStatus::kFailedCrashUserGroupNameGroupInfoForOld,
     "Failed to get group info for group kCrashUserGroupName (old code path)"},
    {CrashCollectionStatus::kFailedGetUserCrashDirectoryOld,
     "Failed to retrieve user directories (old code path)"},
    {CrashCollectionStatus::kFailedCrashGroupNameGroupInfoForOld,
     "Failed to get group info for group kCrashGroupName (old code path)"},
    {CrashCollectionStatus::kFailedLogFileWrite, "Failed to write log file"},
    {CrashCollectionStatus::kInvalidCrashType, "Invalid crash type"},
    {CrashCollectionStatus::kFailedInfoFileWrite, "Failed to write info file"},
    {CrashCollectionStatus::kFailedClobberContainerDirectory,
     "Failed to clobber the container directory"},
    {CrashCollectionStatus::kTestingFailure,
     "Failure deliberately added for integration test purposes"},
    {CrashCollectionStatus::kBadProcessState, "Bad process_status"},
    {CrashCollectionStatus::kBadUserIdStatusLine,
     "UserId not found in status lines"},
    {CrashCollectionStatus::kFailureCopyingCoreData,
     "Failure copying core data to file"},
    {CrashCollectionStatus::kUnusableProcFiles, "Unusable /proc files"},
    {CrashCollectionStatus::kFailureCore2MinidumpConversion,
     "core2md-conversion"},
    {CrashCollectionStatus::kFailureOpeningCoreFile,
     "Failure opening core file"},
    {CrashCollectionStatus::kFailureReadingCoreFileHeader,
     "Failure reading code file header"},
    {CrashCollectionStatus::kBadCoreFileMagic,
     "Core file had bad magic number in header"},
    {CrashCollectionStatus::kFailureUnsupported32BitCoreFile,
     "32 bit core files not supported on 64-bit systems"},
    {CrashCollectionStatus::kFailedGetArcRoot, "Failure getting ARC root"},
    {CrashCollectionStatus::kCoreCollectorFailed,
     "Failure running core collector"},
    {CrashCollectionStatus::kCoreCollectorReturnedOSFile,
     "core_collector return EX_OSFILE"},
    {CrashCollectionStatus::kCoreCollectorReturnedSoftware,
     "core_collector return EX_SOFTWARE"},
    {CrashCollectionStatus::kCoreCollectorReturnedUsage,
     "core_collector return EX_USAGE"},
    {CrashCollectionStatus::kCoreCollectorReturnedIOErr,
     "core_collector return EX_IOERR"},
    {CrashCollectionStatus::kCoreCollectorReturnedCantCreat,
     "core_collector return EX_CANTCREAT"},
    {CrashCollectionStatus::kCoreCollectorReturnedOSErr,
     "core_collector return EX_OSERR"},
    {CrashCollectionStatus::kCoreCollectorReturnedUnknownValue,
     "core_collector returned an unknown exit code"},
};
// LINT.ThenChange(crash_collection_status.h:status_list)

constexpr size_t kStatusNamesSize =
    (sizeof(kStatusNames) / sizeof(kStatusNames[0]));

struct Comparator {
  constexpr bool operator()(
      const std::pair<CrashCollectionStatus, std::string_view>& first,
      const std::pair<CrashCollectionStatus, std::string_view>& second) {
    return first.first < second.first;
  }
  constexpr bool operator()(
      const std::pair<CrashCollectionStatus, std::string_view>& first,
      CrashCollectionStatus value) {
    return first.first < value;
  }
};

static_assert(std::is_sorted(kStatusNames,
                             kStatusNames + kStatusNamesSize,
                             Comparator{}),
              "kStatusNames is not sorted");

constexpr bool StatusNamesAreAllUnique() {
  for (int i = 0; i < kStatusNamesSize; ++i) {
    for (int j = i + 1; j < kStatusNamesSize; ++j) {
      if (kStatusNames[i].first == kStatusNames[j].first) {
        return false;
      }
      if (kStatusNames[i].second == kStatusNames[j].second) {
        return false;
      }
    }
  }
  return true;
}

// Avoid copy-paste errors. Duplicate strings will make logs misleading.
static_assert(StatusNamesAreAllUnique(),
              "kStatusNames has duplicate values or strings");

constexpr bool ValuesAreAllInRange() {
  for (const auto& status_name : kStatusNames) {
    if (static_cast<int>(status_name.first) < 0 ||
        status_name.first > CrashCollectionStatus::kMaxValue) {
      return false;
    }
    if (status_name.first > CrashCollectionStatus::kLastSuccessCode &&
        status_name.first < CrashCollectionStatus::kFirstErrorValue &&
        status_name.first != CrashCollectionStatus::kUnknownStatus) {
      return false;
    }
  }
  return true;
}

// Mostly to catch cases when someone forgot to update kMaxValue or
// kLastSuccessCode when adding a new value.
static_assert(ValuesAreAllInRange(), "kStatusNames has invalid values");
}  // namespace

std::string CrashCollectionStatusToString(CrashCollectionStatus status) {
  const std::pair<CrashCollectionStatus, std::string_view>* p =
      std::lower_bound(kStatusNames, kStatusNames + kStatusNamesSize, status,
                       Comparator{});
  if (p == kStatusNames + kStatusNamesSize || p->first != status) {
    return "Invalid status enum " +
           base::NumberToString(static_cast<int>(status));
  }
  return std::string(p->second);
}
