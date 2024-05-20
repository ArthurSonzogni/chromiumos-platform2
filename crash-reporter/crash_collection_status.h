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
  kFinishedEphermeralCollection = 1,
  kNoCrashFound = 2,
  kChromeCrashInUserCollector = 3,
  kFilteredOut = 4,
  kVmProcessNotInRootNamespace = 5,
  kNotArc = 6,
  kNotArcSystemProcess = 7,
  kDevCoredumpIgnored = 8,
  kSuccessForConnectivityFwdump = 9,
  kLastSuccessCode = kSuccessForConnectivityFwdump,

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
  kFailureReadingChromeDumpFile = 437,
  kFailureReadingChromeDumpFd = 438,
  kIllegalBaseName = 439,
  kNoPayload = 440,
  kFailureCreatingNoStackPayload = 441,
  kInvalidChromeDumpNoDelimitedNameString = 442,
  kInvalidChromeDumpNoDelimitedSizeString = 443,
  kInvalidSizeNaN = 444,
  kInvalidSizeOverflow = 445,
  kTruncatedChromeDump = 446,
  kUnexpectedMinidumpInJavaScriptError = 447,
  kMultipleMinidumps = 448,
  kFailedMinidumpWrite = 449,
  kUnexpectedJavaScriptStackInExecutableCrash = 450,
  kMultipleJavaScriptStacks = 451,
  kFailedJavaScriptStackWrite = 452,
  kFailureReadingGenericReport = 453,
  kBadGenericReportFormat = 454,
  kFailedReadingLogConfigFile = 455,
  kNoExecSpecifiedForGetMultipleLogContents = 456,
  kExecNotConfiguredForLog = 457,
  kFailureCreatingLogCollectionTmpFile = 458,
  kFailureReadingLogCollectionTmpFile = 459,
  kFailureWritingCompressedLogContents = 460,
  kFailureWritingLogContents = 461,
  kFailureLoadingPstoreCrash = 462,
  kPstoreCrashEmpty = 463,
  kFailureGettingPstoreType = 464,
  kFailedKernelDumpWrite = 465,
  kUncollectedPstoreCrashType = 466,
  kCorruptWatchdogFile = 467,
  kFailureReadingEventLog = 468,
  kFailureReadingWatchdogFile = 469,
  kFailureOpeningWatchdogFile = 470,
  kRamoopsDumpEmpty = 471,
  kNeedPidForVm = 472,
  kFailureRetrievingProcessPIDNamespace = 473,
  kFailureRetrievingOwnPIDNamespace = 474,
  kFailureParsingVmToolsCiceroneCrashReport = 475,
  kFailureWritingProcessTree = 476,
  kInvalidKernelNumber = 477,
  kFailedGetDaemonStoreFbPreprocessordDirectory = 478,
  kFailedGetFbpreprocessorUserNameInfo = 479,
  kFailedGetFbpreprocessorGroupNameInfo = 480,
  kOutOfFbpreprocessorCapacity = 481,
  kDevCoredumpDoesntExist = 482,
  kFailedProcessBluetoothCoredump = 483,
  kFailureGettingDeviceDriverName = 484,
  kFailureReadingJavaCrash = 485,
  kJavaCrashEmpty = 486,
  kFailureParsingCrashLog = 487,
  kBadMinidumpFd = 488,
  kMaxValue = kBadMinidumpFd,
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
