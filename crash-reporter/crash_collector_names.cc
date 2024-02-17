// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_collector_names.h"

#include <string_view>
#include <unordered_map>
#include <utility>

#include <base/no_destructor.h>

namespace {
// Note: the strings are not particularly consistent in order to maintain
// backwards compatibility with crashes from before this file was created. Do
// not change the existing names, as that will break several downstream tools.
// (It will break crash_sender's metrics' collector field for older crashes, and
// a number of internal-to-Google queries.)
// LINT.IfChange(collector_list)
constexpr std::pair<CrashReporterCollector, const char*> kNameCollectorPairs[] =
    {
        {CrashReporterCollector::kUnknownCollector, "unknown_collector"},
        {CrashReporterCollector::kUser, "user"},
        {CrashReporterCollector::kChrome, "chrome"},
        {CrashReporterCollector::kBERT, "bert"},
        {CrashReporterCollector::kClobberState, "clobber_state_collector"},
        {CrashReporterCollector::kKernelWarning, "kernel_warning"},
        {CrashReporterCollector::kCrashReporterFailure,
         "crash-reporter-failure-collector"},
        {CrashReporterCollector::kEphemeral, "ephemeral_crash_collector"},
        {CrashReporterCollector::kGenericFailure, "generic_failure"},
        {CrashReporterCollector::kGSC, "gsc"},
        {CrashReporterCollector::kUdev, "udev"},
        {CrashReporterCollector::kEC, "ec"},
        {CrashReporterCollector::kKernel, "kernel"},
        {CrashReporterCollector::kMissedCrash, "missed_crash"},
        {CrashReporterCollector::kMountFailure, "mount_failure_collector"},
        {CrashReporterCollector::kUncleanShutdown, "unclean_shutdown"},
        {CrashReporterCollector::kSecurityAnomaly,
         "security_anomaly_collector"},
        {CrashReporterCollector::kSELinuxViolation, "selinux"},
        {CrashReporterCollector::kVm, "vm_collector"},
        {CrashReporterCollector::kArcJava, "ARC_java"},
        {CrashReporterCollector::kArcvmCxx, "ARCVM_native"},
        {CrashReporterCollector::kArcvmKernel, "ARCVM_kernel"},
        {CrashReporterCollector::kArcppCxx, "ARCPP_cxx"},
        {CrashReporterCollector::kMock, "mock"},
};
// LINT.ThenChange(crash_collector_names.h:collector_list)

static_assert(std::size(kNameCollectorPairs) ==
              static_cast<int>(CrashReporterCollector::kMaxValue) + 1);

}  // namespace

const char* GetNameForCollector(CrashReporterCollector collector) {
  static base::NoDestructor<
      const std::unordered_map<CrashReporterCollector, const char*>>
      kEnumToStringMapping([]() {
        std::unordered_map<CrashReporterCollector, const char*> result;
        for (const auto& pair : kNameCollectorPairs) {
          result.emplace(pair.first, pair.second);
        }
        return result;
      }());

  auto it = kEnumToStringMapping->find(collector);
  if (it == kEnumToStringMapping->end()) {
    return "bad_collector_enum";
  }
  return it->second;
}

CrashReporterCollector GetCollectorForName(std::string_view collector_name) {
  static base::NoDestructor<
      const std::unordered_map<std::string_view, CrashReporterCollector>>
      kStringToEnumMapping([]() {
        std::unordered_map<std::string_view, CrashReporterCollector> result;
        for (const auto& pair : kNameCollectorPairs) {
          result.emplace(pair.second, pair.first);
        }
        return result;
      }());
  auto it = kStringToEnumMapping->find(collector_name);
  if (it == kStringToEnumMapping->end()) {
    return CrashReporterCollector::kUnknownCollector;
  }
  return it->second;
}
