// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_CRASH_COLLECTION_STATUS_H_
#define CRASH_REPORTER_CRASH_COLLECTION_STATUS_H_

#include <string>

// The result of a Collect() callback (InvocationInfo::cb). All possible
// collection results should have their own enum value.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// LINT.IfChange(status_list)
enum class CrashCollectionStatus {
  // Crash report was written successfully. Does NOT include cases where we
  // chose not to write a crash report; the reason for not writing the crash
  // report should be a separate enum value. In other words, # of crash
  // reports received at Google / # of kSuccess logs should give the quality
  // of the crash sender, and should be 1.0 if crash sender is working
  // perfectly.
  kSuccess = 0,

  // Other success types. Things were "successful" but no report generated or
  // the report isn't going into the normal crash pipeline.
  // (will be filled in by children CLs)
  kLastSuccessCode = kSuccess,

  // We should never see this status. It exists just to initialize variables
  // before they get a real value.
  kUnknownStatus = 200,

  // Error types:
  kOutOfCapacity = 400,
  kFirstErrorValue = kOutOfCapacity,
  kFailedMetaWrite = 401,
  kCreateCrashDirectoryFailed = 402,
  kOpenCrashDirectoryFailed = 403,
  kGetDefaultUserInfoFailed = 404,
  kFailedCrashNameGroupInfo = 405,
  kFailedCrashUserGroupNameGroupInfo = 406,
  kFailedCrashGroupNameGroupInfo = 407,
  kInvalidPayloadName = 408,
  kNoUserCrashDirectoryWhenRequired = 409,
  kFailedCrashNameGroupInfoForOld = 410,
  kFailedCrashUserGroupNameGroupInfoForOld = 411,
  kFailedGetUserCrashDirectoryOld = 412,
  kFailedCrashGroupNameGroupInfoForOld = 413,
  kFailedLogFileWrite = 414,
  kInvalidCrashType = 415,
  kFailedInfoFileWrite = 416,
  kFailedClobberContainerDirectory = 417,
  kTestingFailure = 418,
  kBadProcessState = 419,
  kBadUserIdStatusLine = 420,
  kFailureCopyingCoreData = 421,
  kUnusableProcFiles = 422,
  kFailureCore2MinidumpConversion = 423,
  kFailureOpeningCoreFile = 424,
  kFailureReadingCoreFileHeader = 425,
  kBadCoreFileMagic = 426,
  kFailureUnsupported32BitCoreFile = 427,
  kFailedGetArcRoot = 428,
  kCoreCollectorFailed = 429,
  kCoreCollectorReturnedOSFile = 430,
  kCoreCollectorReturnedSoftware = 431,
  kCoreCollectorReturnedUsage = 432,
  kCoreCollectorReturnedIOErr = 433,
  kCoreCollectorReturnedCantCreat = 434,
  kCoreCollectorReturnedOSErr = 435,
  kCoreCollectorReturnedUnknownValue = 436,

  kMaxValue = kCoreCollectorReturnedUnknownValue,
};
// LINT.ThenChange(crash_collection_status.cc:status_list)

// Prefer calling IsSuccessCode instead of comparing to kSuccess when
// determining if a particular function was successful.
inline bool IsSuccessCode(CrashCollectionStatus status) {
  return status <= CrashCollectionStatus::kLastSuccessCode;
}

std::string CrashCollectionStatusToString(CrashCollectionStatus status);

inline std::ostream& operator<<(std::ostream& out,
                                CrashCollectionStatus status) {
  return out << CrashCollectionStatusToString(status);
}

#endif  // CRASH_REPORTER_CRASH_COLLECTION_STATUS_H_