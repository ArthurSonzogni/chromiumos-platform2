// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/kernel_collector.h"

#include <linux/watchdog.h>

#include <algorithm>
#include <cinttypes>
#include <memory>
#include <utility>

#include <base/files/file.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/types/expected.h>
#include <base/types/expected_macros.h>
#include <metrics/metrics_library.h>
#include <re2/re2.h>

#include "crash-reporter/constants.h"
#include "crash-reporter/crash_collection_status.h"
#include "crash-reporter/crash_collector_names.h"
#include "crash-reporter/paths.h"

using base::FilePath;
using base::StringPrintf;

namespace {

// Name for extra BIOS dump attached to report. Also used as metadata key.
constexpr char kBiosDumpName[] = "bios_log";
const FilePath kBiosLogPath("/sys/firmware/log");
// Names of the four BIOS stages in which the BIOS log can start.
const char* const kBiosStageNames[] = {
    "bootblock",
    "verstage",
    "romstage",
    "ramstage",
};
constexpr char kDumpParentPath[] = "/sys/fs";
constexpr char kDumpPath[] = "/sys/fs/pstore";
constexpr char kDumpRecordDmesgName[] = "dmesg";
constexpr char kDumpRecordConsoleName[] = "console";
// The files take the form <record type>-<driver name>-<record id>.
// e.g. console-ramoops-0 or dmesg-ramoops-0.
constexpr char kDumpNameFormat[] = "%s-%s-%zu";
// If the kernel had trouble decoding the record then it will be raw
// (compressed) and end in ".enc.z"
constexpr char kCorruptDumpNameFormat[] = "%s-%s-%zu.enc.z";
constexpr char kCorruptDumpExtension[] = ".enc.z";

const FilePath kEventLogPath("/var/log/eventlog.txt");
constexpr char kEventNameBoot[] = "System boot";
constexpr char kEventNameWatchdog[] = "Hardware watchdog reset";
constexpr pid_t kKernelPid = 0;
constexpr char kKernelSignatureKey[] = "sig";

// Used to build up the path to a watchdog's boot status:
// For example: /sys/class/watchdog/watchdog0/bootstatus
constexpr char kWatchdogSysBootstatusFile[] = "bootstatus";

// Buffer size for reading a bootstatus file into memory.
constexpr size_t kMaxBootstatusSize = 1024 * 1024;

static LazyRE2 kBasicCheckRe = {"\n(<\\d+>)?\\[\\s*(\\d+\\.\\d+)\\]"};

}  // namespace

std::string KernelCollector::PstoreRecordTypeToString(
    PstoreRecordType record_type) {
  switch (record_type) {
    case PstoreRecordType::kPanic:
      return "Panic";
    case PstoreRecordType::kOops:
      return "Oops";
    case PstoreRecordType::kEmergency:
      return "Emergency";
    case PstoreRecordType::kShutdown:
      return "Shutdown";
    case PstoreRecordType::kUnknown:
      return "Unknown";
    case PstoreRecordType::kCorrupt:
      return "Corrupt";
    case PstoreRecordType::kParseFailed:
      return "ParseFailed";
  }
  LOG(ERROR) << "Unknown enum value for pstore record type: "
             << static_cast<int>(record_type);
  return "Unknown enum";
}

// This matches the strings returned from kmsg_dump_reason_str() in the
// kernel.
PstoreRecordType KernelCollector::StringToPstoreRecordType(
    std::string_view record) {
  if (record == "Panic") {
    return PstoreRecordType::kPanic;
  }
  if (record == "Oops") {
    return PstoreRecordType::kOops;
  }
  if (record == "Emergency") {
    return PstoreRecordType::kEmergency;
  }
  if (record == "Shutdown") {
    return PstoreRecordType::kShutdown;
  }
  if (record == "Unknown") {
    return PstoreRecordType::kUnknown;
  }
  return PstoreRecordType::kParseFailed;
}

std::ostream& operator<<(std::ostream& out, PstoreRecordType record_type) {
  return out << KernelCollector::PstoreRecordTypeToString(record_type);
}

KernelCollector::KernelCollector(
    const scoped_refptr<
        base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
        metrics_lib)
    : CrashCollector(CrashReporterCollector::kKernel, metrics_lib),
      is_enabled_(false),
      eventlog_path_(kEventLogPath),
      dump_path_(kDumpPath),
      bios_log_path_(kBiosLogPath),
      watchdogsys_path_(paths::Get(paths::kWatchdogSysPath)),
      // We expect crash dumps in the format of architecture we are built for.
      arch_(kernel_util::GetCompilerArch()) {}

KernelCollector::~KernelCollector() {}

void KernelCollector::OverrideEventLogPath(const FilePath& file_path) {
  eventlog_path_ = file_path;
}

void KernelCollector::OverrideBiosLogPath(const FilePath& file_path) {
  bios_log_path_ = file_path;
}

void KernelCollector::OverridePreservedDumpPath(const FilePath& file_path) {
  dump_path_ = file_path;
}

void KernelCollector::OverrideWatchdogSysPath(const FilePath& file_path) {
  watchdogsys_path_ = file_path;
}

FilePath KernelCollector::GetDumpRecordPath(const char* type,
                                            const char* driver,
                                            size_t record) {
  FilePath record_path =
      dump_path_.Append(StringPrintf(kDumpNameFormat, type, driver, record));

  if (!base::PathExists(record_path)) {
    // If the path wasn't found, check to see if the kernel exported it raw
    // to us because it couldn't decode it. We'll still read/upload it, but
    // code that runs later in the collection process will warn about it.
    FilePath corrupt_record_path = dump_path_.Append(
        StringPrintf(kCorruptDumpNameFormat, type, driver, record));
    if (base::PathExists(corrupt_record_path)) {
      return corrupt_record_path;
    }
  }

  return record_path;
}

bool KernelCollector::LoadLastBootBiosLog(std::string* contents) {
  contents->clear();

  if (!base::PathExists(bios_log_path_)) {
    LOG(INFO) << bios_log_path_.value() << " does not exist, skipping "
              << "BIOS crash check. (This is normal for older boards.)";
    return false;
  }

  std::string full_log;
  if (!base::ReadFileToString(bios_log_path_, &full_log)) {
    PLOG(ERROR) << "Unable to read " << bios_log_path_.value();
    return false;
  }

  RE2::Options opt;
  opt.set_dot_nl(true);  // match \n with '.'
  // Different platforms start their BIOS log at different stages. Look for
  // banner strings of all stages in order until we find one that works.
  for (auto stage : kBiosStageNames) {
    // use the "^" to anchor to the start of the string
    RE2 banner_re(StringPrintf("(^.*?)(?:"
                               "\n\\*\\*\\* Pre-CBMEM %s console overflow"
                               "|\n\n[^\n]*"
                               "coreboot-[^\n]* %s starting.*\\.\\.\\.\n"
                               ")",
                               stage, stage),
                  opt);
    re2::StringPiece remaining_log(full_log);
    re2::StringPiece previous_boot;
    bool found = false;

    // Keep iterating until last previous_boot before current one.
    while (RE2::PartialMatch(remaining_log, banner_re, &previous_boot)) {
      remaining_log.remove_prefix(previous_boot.size() + 1);
      found = true;
    }

    if (!previous_boot.empty()) {
      contents->append(std::string(previous_boot));
      return true;
    }

    // If banner found but no log before it, don't look for other stage banners.
    // This just means we booted up from S5 and there was nothing left in DRAM.
    if (found) {
      return false;
    }
  }

  // This shouldn't happen since we should always see at least the current boot.
  LOG(ERROR) << "BIOS log contains no known banner strings!";
  return false;
}

bool KernelCollector::LastRebootWasBiosCrash(const std::string& dump) {
  // BIOS crash detection only supported on ARM64 for now. We're in userspace,
  // so we can't easily check for 64-bit (but that's not a big deal).
  if (arch_ != kernel_util::kArchArm) {
    return false;
  }

  if (dump.empty()) {
    return false;
  }

  return RE2::PartialMatch(
      dump, RE2("(PANIC|Unhandled( Interrupt)? Exception) in EL3"));
}

bool KernelCollector::LastRebootWasNoCError(const std::string& dump) {
  // NoC errors are only on Qualcomm platforms for now.
  if (dump.empty()) {
    return false;
  }

  return RE2::PartialMatch(dump, RE2("QTISECLIB.*NOC ERROR: ERRLOG"));
}

// Return true if the HW watchdog caused a reboot, so a crash report
// can be collected. Fills out `watchdog_reboot_reason` with the decoded
// reboot reason. Returns base::unexpected on error, returns false if the
// HW watchdog did not cause a reboot.
static base::expected<bool, CrashCollectionStatus>
GetWatchdogRebootReasonFromPath(const base::FilePath& watchdog_path,
                                std::string& watchdog_reboot_reason) {
  std::string bootstatus_string;
  {
    base::File watchdog_file(watchdog_path,
                             base::File::FLAG_OPEN | base::File::FLAG_READ);
    if (!watchdog_file.IsValid()) {
      if (watchdog_file.error_details() == base::File::FILE_ERROR_NOT_FOUND) {
        // Normal, watchdogs may not have a bootstatus file.
        LOG(INFO) << watchdog_path << " does not exist";
        return false;
      } else {
        LOG(ERROR) << "Unable to read " << watchdog_path << ": "
                   << base::File::ErrorToString(watchdog_file.error_details());
        return base::unexpected(
            CrashCollectionStatus::kFailureOpeningWatchdogFile);
      }
    }
    int64_t length = watchdog_file.GetLength();
    if (length < 0) {
      LOG(ERROR) << "Error getting length of " << watchdog_path;
      return base::unexpected(
          CrashCollectionStatus::kFailureReadingWatchdogFile);
    }

    if (length > kMaxBootstatusSize) {
      LOG(ERROR) << watchdog_path << " too big (size" << length << ")";
      return base::unexpected(
          CrashCollectionStatus::kFailureReadingWatchdogFile);
    }

    bootstatus_string.resize(length);
    int read_result = watchdog_file.ReadAtCurrentPos(
        bootstatus_string.data(), bootstatus_string.length());
    if (read_result < 0) {
      LOG(ERROR) << "Error reading " << watchdog_path;
      return base::unexpected(
          CrashCollectionStatus::kFailureReadingWatchdogFile);
    }
    // The crash report may come from pseudo file system which may report
    // the max size of data buffer instead of size of actual data so adjust
    // size of the string according to size of data actually read for the file
    bootstatus_string.resize(read_result);
  }

  unsigned bootstatus = 0;
  if (!base::StringToUint(
          base::CollapseWhitespaceASCII(bootstatus_string, true),
          &bootstatus)) {
    LOG(ERROR) << "Invalid bootstatus string '" << bootstatus_string << "'";
    return base::unexpected(CrashCollectionStatus::kCorruptWatchdogFile);
  }

  // Ignore normal bootstatus.
  if (bootstatus == 0) {
    return false;
  }

  watchdog_reboot_reason = std::string();
  uint32_t known_bootstatus_values =
      WDIOF_OVERHEAT | WDIOF_FANFAULT | WDIOF_EXTERN1 | WDIOF_EXTERN2 |
      WDIOF_POWERUNDER | WDIOF_CARDRESET | WDIOF_POWEROVER;
  if (bootstatus & ~known_bootstatus_values) {
    watchdog_reboot_reason += "-(UNKNOWN)";
    LOG(ERROR) << watchdog_path
               << ": unknown boot status value: " << std::showbase << std::hex
               << (bootstatus & ~known_bootstatus_values);
  }

  // bootstatus is a bitmap, so build up the reboot reason string.
  if (bootstatus & WDIOF_OVERHEAT) {
    watchdog_reboot_reason += "-(OVERHEAT)";
  }
  if (bootstatus & WDIOF_FANFAULT) {
    watchdog_reboot_reason += "-(FANFAULT)";
  }
  if (bootstatus & WDIOF_EXTERN1) {
    watchdog_reboot_reason += "-(EXTERN1)";
  }
  if (bootstatus & WDIOF_EXTERN2) {
    watchdog_reboot_reason += "-(EXTERN2)";
  }
  if (bootstatus & WDIOF_POWERUNDER) {
    watchdog_reboot_reason += "-(POWERUNDER)";
  }
  if (bootstatus & WDIOF_CARDRESET) {
    watchdog_reboot_reason += "-(WATCHDOG)";
  }
  if (bootstatus & WDIOF_POWEROVER) {
    watchdog_reboot_reason += "-(POWEROVER)";
  }

  // Watchdog recorded some kind of reset, so collect a crash dump.
  return true;
}

// We can't always trust kernel watchdog drivers to correctly report the boot
// reason, since on some platforms our BIOS has to reinitialize the hardware
// registers in a way that clears this information. If /sys/class/watchdog is
// unavailable, read the BIOS eventlog to figure out if a watchdog reset was
// detected during the last boot.
base::expected<bool, CrashCollectionStatus>
KernelCollector::LastRebootWasWatchdog(std::string& watchdog_reboot_reason) {
  if (base::PathExists(watchdogsys_path_)) {
    base::FilePath watchdog_sys_dir(watchdogsys_path_);
    base::FileEnumerator watchdog_sys_dir_enumerator(
        watchdog_sys_dir, false, base::FileEnumerator::DIRECTORIES);

    // Iterate through the watchdogN devices and look for a reboot.
    for (base::FilePath watchdog_path =
             watchdog_sys_dir_enumerator.Next().StripTrailingSeparators();
         !watchdog_path.empty();
         watchdog_path =
             watchdog_sys_dir_enumerator.Next().StripTrailingSeparators()) {
      // Build up the path to the watchdog's boot status:
      // For example: /sys/class/watchdog/watchdog0/bootstatus
      base::FilePath watchdog_sys_path =
          watchdog_path.Append(kWatchdogSysBootstatusFile);

      auto from_path = GetWatchdogRebootReasonFromPath(watchdog_sys_path,
                                                       watchdog_reboot_reason);
      // Return an error if there was an error.
      if (!from_path.has_value()) {
        return base::unexpected(from_path.error());
      }
      // Return true if watchdog_sys_path shows the system was rebooted by a
      // watchdog.
      if (from_path.value()) {
        return true;
      }
      // Otherwise look in the eventlog.
    }
  }

  if (!base::PathExists(eventlog_path_)) {
    LOG(INFO) << "Cannot find " << eventlog_path_.value()
              << ", skipping hardware watchdog check.";
    return false;
  }

  std::string eventlog;
  if (!base::ReadFileToString(eventlog_path_, &eventlog)) {
    PLOG(ERROR) << "Unable to open " << eventlog_path_.value();
    return base::unexpected(CrashCollectionStatus::kFailureReadingEventLog);
  }

  std::string_view piece = std::string_view(eventlog);
  size_t last_boot = piece.rfind(kEventNameBoot);
  if (last_boot == std::string_view::npos) {
    return false;
  }

  watchdog_reboot_reason = "-(WATCHDOG)";
  return piece.find(kEventNameWatchdog, last_boot) != std::string_view::npos;
}

bool KernelCollector::LoadConsoleRamoops(std::string* contents) {
  FilePath record_path;

  // We assume there is only one record.  Bad idea?
  record_path =
      GetDumpRecordPath(kDumpRecordConsoleName, kDumpDriverRamoopsName, 0);

  if (!base::PathExists(record_path)) {
    LOG(WARNING) << "No console-ramoops file found after watchdog reset";
    return false;
  }

  if (!base::ReadFileToString(record_path, contents)) {
    PLOG(ERROR) << "Unable to open " << record_path.value();
    return false;
  }

  if (!RE2::PartialMatch(contents->substr(0, 1024), *kBasicCheckRe)) {
    LOG(WARNING) << "Found invalid console-ramoops file";
    return false;
  }

  return true;
}

bool KernelCollector::DumpDirMounted() {
  struct stat st_parent;
  if (stat(kDumpParentPath, &st_parent)) {
    PLOG(WARNING) << "Could not stat " << kDumpParentPath;
    return false;
  }

  struct stat st_dump;
  if (stat(kDumpPath, &st_dump)) {
    PLOG(WARNING) << "Could not stat " << kDumpPath;
    return false;
  }

  if (st_parent.st_dev == st_dump.st_dev) {
    LOG(WARNING) << "Dump dir " << kDumpPath << " not mounted";
    return false;
  }

  return true;
}

bool KernelCollector::Enable() {
  if (arch_ == kernel_util::kArchUnknown || arch_ >= kernel_util::kArchCount) {
    LOG(WARNING) << "KernelCollector does not understand this architecture";
    return false;
  }

  if (!DumpDirMounted()) {
    LOG(WARNING) << "Kernel does not support crash dumping";
    return false;
  }

  // To enable crashes, we will eventually need to set
  // the chnv bit in BIOS, but it does not yet work.
  LOG(INFO) << "Enabling kernel crash handling";
  is_enabled_ = true;
  return true;
}

CrashCollector::ComputedCrashSeverity KernelCollector::ComputeSeverity(
    const std::string& exec_name) {
  return ComputedCrashSeverity{
      .crash_severity = CrashSeverity::kFatal,
      .product_group = Product::kPlatform,
  };
}

KernelCollector::PstoreCrash::~PstoreCrash() = default;

PstoreRecordType KernelCollector::PstoreCrash::GetType() const {
  // Stack traces could be generated and written to pstore during kernel oops,
  // kernel panic, etc. First line contains header of format:
  // <crash_type>#<crash_count> Part#<part_number>
  // <crash_type> indicates when stack trace was generated. e.g. Panic#1 Part#1.
  std::string dump;
  if (IsPartCorrupted(1)) {
    return PstoreRecordType::kCorrupt;
  }
  if (base::ReadFileToString(GetFilePath(1), &dump)) {
    size_t pos = dump.find('#');
    if (pos != std::string::npos) {
      return KernelCollector::StringToPstoreRecordType(dump.substr(0, pos));
    }
  }
  return PstoreRecordType::kParseFailed;
}

bool KernelCollector::PstoreCrash::Load(std::string& contents) const {
  // Part0 is never generated by pstore.
  // Part number is descending, so Part1 contains last 1KiB (e.g. EFI
  // variable size) of kmsg buffer, Part2 contains the second to last 1KiB,
  // etc.
  for (uint32_t part = GetMaxPart(); part > 0; part--) {
    std::string dump;
    if (!base::ReadFileToString(GetFilePath(part), &dump)) {
      PLOG(ERROR) << "Unable to open->read file for crash: " << GetId()
                  << " part: " << part;
      return false;
    }
    if (IsPartCorrupted(part)) {
      // The kernel identified this as a compressed dump but was unable to
      // decompress. We'll still upload this in case someone can figure
      // something useful out of it. We'll prepend a warning, though.
      LOG(WARNING) << "Kernel couldn't decode ramoops " << GetId();
      base::StrAppend(&contents, {constants::kCorruptPstore, dump});
    } else {
      // Strip first line since it contains header e.g. Panic#1 Part#1.
      contents += dump.substr(dump.find('\n') + 1, std::string::npos);
    }
  }
  return true;
}

void KernelCollector::PstoreCrash::Remove() const {
  // Delete pstore crash record(s).
  // Part can be deleted in any order. Start from Part1 since Part0 is
  // never generated.
  for (uint32_t part = 1; part <= GetMaxPart(); part++) {
    base::DeleteFile(GetFilePath(part));
  }
}

CrashCollectionStatus KernelCollector::PstoreCrash::CollectCrash(
    std::string& crash) const {
  LOG(INFO) << "Generating kernel crash id: " << GetId();
  CrashCollectionStatus result = CrashCollectionStatus::kUnknownStatus;

  PstoreRecordType crash_type;
  crash_type = GetType();
  if (crash_type != PstoreRecordType::kParseFailed) {
    if (crash_type == PstoreRecordType::kPanic ||
        crash_type == PstoreRecordType::kCorrupt) {
      LOG(INFO) << "Reporting kernel crash id: " << GetId()
                << " type: " << crash_type;
      if (!Load(crash)) {
        result = CrashCollectionStatus::kFailureLoadingPstoreCrash;
      } else {
        result = CrashCollectionStatus::kSuccess;
      }
    } else {
      LOG(WARNING) << "Ignoring kernel crash id: " << GetId()
                   << " type: " << crash_type;
      result = CrashCollectionStatus::kUncollectedPstoreCrashType;
    }
  } else {
    result = CrashCollectionStatus::kFailureGettingPstoreType;
  }
  // Remove pstore files corresponding to crash.
  Remove();
  return result;
}

base::FilePath KernelCollector::PstoreCrash::GetFilePath(uint32_t part) const {
  return GetCollector()->dump_path_.Append(base::StrCat(
      {kDumpRecordDmesgName, "-", backend_,
       StringPrintf("-%" PRIu64 "%s", GetIdForPart(part),
                    IsPartCorrupted(part) ? kCorruptDumpExtension : "")}));
}

KernelCollector::RamoopsCrash::~RamoopsCrash() = default;

bool KernelCollector::RamoopsCrash::IsPartCorrupted(uint32_t part) const {
  return corrupted_;
}

KernelCollector::EfiCrash::~EfiCrash() = default;

bool KernelCollector::EfiCrash::IsPartCorrupted(uint32_t part) const {
  // Implementing this would require some way to indicate which parts of the
  // crash record are corrupted while searching in FindEfiCrashes(). Ignore this
  // for now until we need to handle this.
  return false;
}

std::vector<KernelCollector::RamoopsCrash> KernelCollector::FindRamoopsCrashes()
    const {
  std::vector<RamoopsCrash> ramoops_crashes;
  const base::FilePath pstore_dir(dump_path_);
  if (!base::PathExists(pstore_dir)) {
    return ramoops_crashes;
  }

  // Scan /sys/fs/pstore/.
  std::string ramoops_crash_pattern =
      StringPrintf("%s-%s-*", kDumpRecordDmesgName, kDumpDriverRamoopsName);
  base::FileEnumerator ramoops_file_iter(
      pstore_dir, false, base::FileEnumerator::FILES, ramoops_crash_pattern);

  for (auto ramoops_file = ramoops_file_iter.Next(); !ramoops_file.empty();
       ramoops_file = ramoops_file_iter.Next()) {
    bool corrupted =
        ramoops_file.BaseName().value().ends_with(kCorruptDumpExtension);
    uint64_t crash_id;
    if (!base::StringToUint64(
            ramoops_file.RemoveExtension().BaseName().value().substr(
                ramoops_crash_pattern.length() - 1),
            &crash_id)) {
      // This should not ever happen.
      LOG(ERROR) << "Failed to parse ramoops file name: "
                 << ramoops_file.BaseName().value();
      continue;
    }

    RamoopsCrash ramoops_crash(crash_id, this, corrupted);
    ramoops_crashes.push_back(ramoops_crash);
  }
  return ramoops_crashes;
}

std::vector<KernelCollector::EfiCrash> KernelCollector::_FindDriverEfiCrashes(
    const char* efi_driver_name) const {
  std::vector<EfiCrash> efi_crashes;
  const base::FilePath pstore_dir(dump_path_);
  if (!base::PathExists(pstore_dir)) {
    return efi_crashes;
  }

  // Scan /sys/fs/pstore/.
  std::string efi_crash_pattern =
      StringPrintf("%s-%s-*", kDumpRecordDmesgName, efi_driver_name);
  base::FileEnumerator efi_file_iter(
      pstore_dir, false, base::FileEnumerator::FILES, efi_crash_pattern);

  for (auto efi_file = efi_file_iter.Next(); !efi_file.empty();
       efi_file = efi_file_iter.Next()) {
    uint64_t crash_id;
    if (!base::StringToUint64(
            efi_file.BaseName().value().substr(efi_crash_pattern.length() - 1),
            &crash_id)) {
      // This should not ever happen.
      LOG(ERROR) << "Failed to parse efi file name:"
                 << efi_file.BaseName().value();
      continue;
    }

    const uint64_t keyed_crash_id = EfiCrash::GetIdForPart(crash_id, 1);
    std::vector<EfiCrash>::iterator it =
        std::find_if(efi_crashes.begin(), efi_crashes.end(),
                     [keyed_crash_id](const EfiCrash& efi_crash) -> bool {
                       return efi_crash.GetId() == keyed_crash_id;
                     });
    if (it != efi_crashes.end()) {
      // Update part number if its greater.
      it->UpdateMaxPart(crash_id);

    } else {
      // New crash detected.
      EfiCrash efi_crash(keyed_crash_id, std::string(efi_driver_name), this);
      efi_crash.UpdateMaxPart(crash_id);
      efi_crashes.push_back(efi_crash);
    }
  }
  return efi_crashes;
}

// Find number of efi crashes at /sys/fs/pstore and returns vector of EfiCrash.
std::vector<KernelCollector::EfiCrash> KernelCollector::FindEfiCrashes() const {
  std::vector<EfiCrash> efi_crashes, driver_efi_crashes;

  for (size_t i = 0; i < numKDumpDriverEfiNames; i++) {
    driver_efi_crashes = _FindDriverEfiCrashes(kDumpDriverEfiNames[i]);
    efi_crashes.insert(efi_crashes.end(), driver_efi_crashes.begin(),
                       driver_efi_crashes.end());
  }

  return efi_crashes;
}

// Safely writes the string to the named log file.
void KernelCollector::AddLogFile(const char* log_name,
                                 const std::string& log_data,
                                 const FilePath& log_path) {
  if (!log_data.empty()) {
    if (WriteNewFile(log_path, log_data) !=
        static_cast<int>(log_data.length())) {
      PLOG(WARNING) << "Failed to write " << log_name << " to "
                    << log_path.value() << " (ignoring)";
    } else {
      AddCrashMetaUploadFile(log_name, log_path.BaseName().value());
      LOG(INFO) << "Stored " << log_name << " to " << log_path.value();
    }
  }
}

// Stores crash pointed by kernel_dump to crash directory. This will be later
// sent to backend from crash directory by crash_sender.
CrashCollectionStatus KernelCollector::HandleCrash(
    const std::string& kernel_dump,
    const std::string& bios_dump,
    const std::string& signature) {
  FilePath root_crash_directory;

  LOG(INFO) << "Received prior crash notification from kernel (signature "
            << signature << ") (handling)";

  CrashCollectionStatus status = GetCreatedCrashDirectoryByEuid(
      constants::kRootUid, &root_crash_directory, nullptr);
  if (!IsSuccessCode(status)) {
    return status;
  }

  std::string dump_basename = FormatDumpBasename(kernel_util::kKernelExecName,
                                                 time(nullptr), kKernelPid);
  FilePath kernel_crash_path = root_crash_directory.Append(
      StringPrintf("%s.kcrash", dump_basename.c_str()));
  FilePath bios_dump_path = root_crash_directory.Append(
      StringPrintf("%s.%s", dump_basename.c_str(), kBiosDumpName));
  FilePath log_path = root_crash_directory.Append(
      StringPrintf("%s.log", dump_basename.c_str()));

  // We must use WriteNewFile instead of base::WriteFile as we
  // do not want to write with root access to a symlink that an attacker
  // might have created.
  if (WriteNewFile(kernel_crash_path, kernel_dump) !=
      static_cast<int>(kernel_dump.length())) {
    LOG(INFO) << "Failed to write kernel dump to "
              << kernel_crash_path.value().c_str();
    return CrashCollectionStatus::kFailedKernelDumpWrite;
  }
  AddLogFile(kBiosDumpName, bios_dump, bios_dump_path);

  AddCrashMetaData(kKernelSignatureKey, signature);

  // Collect additional logs if one is specified in the config file.
  if (IsSuccessCode(GetLogContents(log_config_path_,
                                   kernel_util::kKernelExecName, log_path))) {
    AddCrashMetaUploadFile("log", log_path.BaseName().value());
  }

  const char* exec_name = kernel_util::IsHypervisorCrash(kernel_dump)
                              ? kernel_util::kHypervisorExecName
                              : kernel_util::kKernelExecName;

  status = FinishCrash(root_crash_directory.Append(
                           StringPrintf("%s.meta", dump_basename.c_str())),
                       exec_name, kernel_crash_path.BaseName().value());

  LOG(INFO) << "Stored kcrash to " << kernel_crash_path.value();

  return status;
}

// CollectEfiCrashes looks at /sys/fs/pstore and extracts crashes written via
// efi-pstore.
std::vector<CrashCollectionStatus> KernelCollector::CollectEfiCrashes(
    bool use_saved_lsb) {
  SetUseSavedLsb(use_saved_lsb);
  // List of efi crashes.
  const std::vector<KernelCollector::EfiCrash> efi_crashes = FindEfiCrashes();

  LOG(INFO) << "Found " << efi_crashes.size()
            << " kernel crashes in efi-pstore.";
  if (efi_crashes.empty()) {
    return {CrashCollectionStatus::kNoCrashFound};
  }
  std::vector<CrashCollectionStatus> result;
  // Now read each crash in buffer and cleanup pstore.
  std::vector<EfiCrash>::const_iterator efi_crash;

  for (efi_crash = efi_crashes.begin(); efi_crash != efi_crashes.end();
       ++efi_crash) {
    std::string crash;
    CrashCollectionStatus single_result = efi_crash->CollectCrash(crash);
    if (single_result == CrashCollectionStatus::kSuccess) {
      StripSensitiveData(&crash);
      if (crash.empty()) {
        single_result = CrashCollectionStatus::kPstoreCrashEmpty;
      } else {
        single_result =
            HandleCrash(crash, std::string(),
                        kernel_util::ComputeKernelStackSignature(crash, arch_));
        if (!IsSuccessCode(single_result)) {
          LOG(ERROR) << "Failed to handle kernel crash id: "
                     << efi_crash->GetId();
        }
      }
    }
    result.push_back(single_result);
  }
  return result;
}

std::vector<CrashCollectionStatus> KernelCollector::CollectRamoopsCrashes(
    bool use_saved_lsb) {
  SetUseSavedLsb(use_saved_lsb);
  const std::vector<KernelCollector::RamoopsCrash> ramoops_crashes =
      FindRamoopsCrashes();

  std::string bios_dump;
  std::string console_dump;
  std::string signature;

  LoadLastBootBiosLog(&bios_dump);
  LoadConsoleRamoops(&console_dump);

  LOG(INFO) << "Found " << ramoops_crashes.size()
            << " kernel crashes in ramoops-pstore.";
  if (ramoops_crashes.empty()) {
    return {
        // There wasn't a pstore crash record for a panic or oops. The system
        // likely rebooted unexpectedly, probably due to a watchdog or some
        // BIOS error.  Collect the system logs that are preserved across
        // reboots and generate a crash report.
        CollectConsoleRamoopsCrash(bios_dump, console_dump)};
  }

  StripSensitiveData(&bios_dump);

  std::vector<CrashCollectionStatus> result;
  // Now read each crash in buffer and cleanup pstore.
  // Since the system is set to restart on oops we won't actually ever have
  // more than 2 records, but check in case we don't restart on oops in the
  // future.
  std::vector<RamoopsCrash>::const_iterator ramoops_crash;
  for (ramoops_crash = ramoops_crashes.begin();
       ramoops_crash != ramoops_crashes.end(); ++ramoops_crash) {
    LOG(INFO) << "Generating kernel ramoops crash id:"
              << ramoops_crash->GetId();
    CrashCollectionStatus single_result = CrashCollectionStatus::kUnknownStatus;
    std::string crash;
    single_result = ramoops_crash->CollectCrash(crash);
    if (single_result == CrashCollectionStatus::kSuccess) {
      signature = kernel_util::ComputeKernelStackSignature(crash, arch_);
      StripSensitiveData(&crash);
      if (crash.empty()) {
        single_result = CrashCollectionStatus::kPstoreCrashEmpty;
      } else {
        single_result = HandleCrash(crash, bios_dump, signature);
        if (!IsSuccessCode(single_result)) {
          LOG(ERROR) << "Failed to handle kernel crash id: "
                     << ramoops_crash->GetId();
        }
      }
    }
    result.push_back(single_result);
  }
  return result;
}

CrashCollectionStatus KernelCollector::CollectConsoleRamoopsCrash(
    std::string& bios_dump, std::string& console_dump) {
  std::string signature;
  std::string watchdog_reboot_reason;

  if (LastRebootWasBiosCrash(bios_dump)) {
    signature = kernel_util::BiosCrashSignature(bios_dump);
  } else if (LastRebootWasNoCError(bios_dump)) {
    signature = kernel_util::ComputeNoCErrorSignature(bios_dump);
  } else {
    ASSIGN_OR_RETURN(bool watchdog_result,
                     LastRebootWasWatchdog(watchdog_reboot_reason));
    if (watchdog_result) {
      signature =
          kernel_util::WatchdogSignature(console_dump, watchdog_reboot_reason);
    } else {
      return CrashCollectionStatus::kNoCrashFound;
    }
  }
  StripSensitiveData(&bios_dump);
  StripSensitiveData(&console_dump);
  // As long as there's some log contents then maybe we'll be able to figure
  // out why the system rebooted unexpectedly. Otherwise ignore the crash as
  // we're unlikely to be able to diagnose the issue.
  if (console_dump.empty() && bios_dump.empty()) {
    return CrashCollectionStatus::kRamoopsDumpEmpty;
  }
  return HandleCrash(console_dump, bios_dump, signature);
}

bool KernelCollector::WasKernelCrash(
    const std::vector<CrashCollectionStatus>& efi_crash_statuses,
    const std::vector<CrashCollectionStatus>& ramoops_crash_statuses) {
  // kSuccess means there was a crash and the Collect function collected it.
  // Failures mean there was a crash and Collect function didn't collect it.
  // Only kNoCrashFound means there wasn't a crash to collect.
  if (efi_crash_statuses.size() != 1 ||
      efi_crash_statuses[0] != CrashCollectionStatus::kNoCrashFound) {
    return true;
  }

  if (ramoops_crash_statuses.size() != 1 ||
      ramoops_crash_statuses[0] != CrashCollectionStatus::kNoCrashFound) {
    return true;
  }
  return false;
}
