// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_CONSTANTS_H_
#define CRASH_REPORTER_CONSTANTS_H_

#include <sys/stat.h>
#include <unistd.h>

namespace constants {

// UserID for root account.
inline constexpr uid_t kRootUid = 0;

inline constexpr char kCrashName[] = "crash";

// The name of the crash-access group, which owns /var/spool/crash.
inline constexpr char kCrashGroupName[] = "crash-access";

#if !USE_KVM_GUEST
inline constexpr char kCrashUserGroupName[] = "crash-user-access";
#endif

// Directory mode of the daemon store spool directory. This has the sticky bit
// set to prevent different crash collectors from accidentally changing or
// modifying each other's files.
inline constexpr mode_t kDaemonStoreCrashPathMode = 03770;

// The user name that has access to connectivity fw crash dumps.
inline constexpr char kFbpreprocessorUserName[] = "fbpreprocessor";

// The name of fbpreprocessord user access group that owns
// connectivity fw dumps.
inline constexpr char kFbpreprocessorGroupName[] = "fbpreprocessor-user-access";

inline constexpr char kUploadVarPrefix[] = "upload_var_";
inline constexpr char kUploadTextPrefix[] = "upload_text_";
inline constexpr char kUploadFilePrefix[] = "upload_file_";

// An upload var for the metafile, indicating that a crash happened
// in crash loop mode.
inline constexpr char kCrashLoopModeKey[] = "crash_loop_mode";

// An upload var for the metafile, giving the name of a Chrome crash key that
// gives the shutdown type (for example, "close", "exit", "end", "silent_exit",
// or "other_exit"). This const needs to match the shutdown type key set in
// `OnShutdownStarting()` from
// https://crsrc.org/c/chrome/browser/lifetime/browser_shutdown.cc.
constexpr char kShutdownTypeKey[] = "shutdown-type";

// An upload var for the metafile, giving the product name (for example,
// "Chrome_ChromeOS" or "ChromeOS").
inline constexpr char kUploadDataKeyProductKey[] = "prod";

// The product name for Chrome ash crashes. Must match the string in
// ChromeCrashReporterClient::GetProductNameAndVersion() in the chromium repo.
inline constexpr char kProductNameChromeAsh[] = "Chrome_ChromeOS";

// Upload var in the metadata file which holds the severity of a crash as
// determined by the crash collector that handles the crash.
inline constexpr char kClientComputedSeverityKey[] = "client_computed_severity";

// All values that `kClientComputedSeverityKey` can have.
inline constexpr char kClientComputedCrashSeverityFatal[] = "FATAL";
inline constexpr char kClientComputedCrashSeverityError[] = "ERROR";
inline constexpr char kClientComputedCrashSeverityInfo[] = "INFO";
inline constexpr char kClientComputedCrashSeverityWarning[] = "WARNING";
inline constexpr char kClientComputedCrashSeverityUnspecified[] = "UNSPECIFIED";

// Upload var in the metadata file which holds the name of the collector that
// handled the crash.
inline constexpr char kCollectorKey[] = "collector";

inline constexpr char kJavaScriptStackExtension[] = "js_stack";
inline constexpr char kJavaScriptStackExtensionWithDot[] = ".js_stack";
// This *must match* the crash::FileStorage::kJsStacktraceFileName constant
// in the google3 internal crash processing code.
inline constexpr char kKindForJavaScriptError[] = "JavascriptError";

inline constexpr char kMinidumpExtension[] = "dmp";
inline constexpr char kMinidumpExtensionWithDot[] = ".dmp";
// This *must match* the ending of the crash::FileStorage::kDumpFileName
// in the google3 internal crash processing code.
inline constexpr char kKindForMinidump[] = "minidump";

// The crash key used by Chrome to record its process type (browser, renderer,
// gpu-process, etc). Must match the key of the |ptype_key| variable inside
// InitializeCrashpadImpl() in
// https://source.chromium.org/chromium/chromium/src/+/main:components/crash/core/app/crashpad.cc
inline constexpr char kChromeProcessTypeKey[] = "ptype";

// Some of the values that `kChromeProcessTypeKey` can have.
constexpr char kChromeProcessTypeRenderer[] = "renderer";
constexpr char kChromeProcessTypeUtility[] = "utility";
constexpr char kChromeProcessTypeGpu[] = "gpu-process";
constexpr char kChromeProcessTypeExtension[] = "extension";

inline constexpr mode_t kSystemCrashFilesMode = 0660;

// Keys to report on chromebook plus status of a device, to debug potential
// crashes related to that status. See libsegmentation for more.
inline constexpr char kFeatureLevelKey[] = "feature_level";
inline constexpr char kScopeLevelKey[] = "scope_level";

// If pstore record is corrupted, we'll prepend this string to the raw data
// that the kernel gives us.
inline constexpr char kCorruptPstore[] =
    "UNPROCESSED (CORRUPT?) PSTORE RECORD:\n";

}  // namespace constants

#endif  // CRASH_REPORTER_CONSTANTS_H_
