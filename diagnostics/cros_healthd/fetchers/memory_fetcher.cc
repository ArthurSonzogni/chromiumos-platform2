// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/memory_fetcher.h"

#include <string>
#include <utility>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_tokenizer.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

namespace {

namespace executor_ipc = chromeos::cros_healthd_executor::mojom;
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
using OptionalProbeErrorPtr = base::Optional<mojo_ipc::ProbeErrorPtr>;

// Path to procfs, relative to the root directory.
constexpr char kRelativeProcCpuInfoPath[] = "proc/cpuinfo";
constexpr char kRelativeProcPath[] = "proc";
constexpr char kRelativeMktmePath[] = "sys/kernel/mm/mktme";
constexpr char kMktmeActiveFile[] = "active";
constexpr char kMktmeActiveAlgorithmFile[] = "active_algo";
constexpr char kMktmeKeyCountFile[] = "keycnt";
constexpr char kMktmeKeyLengthFile[] = "keylen";
constexpr uint32_t kTmeCapabilityMsr = 0x981;
constexpr uint64_t kTmeBypassAllowBit = (uint64_t)1 << 31;
constexpr uint64_t kTmeAllowAesXts128 = 1;
constexpr uint64_t kTmeAllowAesXts256 = (uint64_t)1 << 2;
constexpr uint32_t kTmeActivateMsr = 0x982;
constexpr uint64_t kTmeEnableBit = (uint64_t)1 << 1;
constexpr uint64_t kTmeBypassBit = (uint64_t)1 << 31;
// tme agorithm mask bits[7:4].
constexpr uint64_t kTmeAlgorithmMask = ((uint64_t)1 << 8) - ((uint64_t)1 << 4);
// AES_XTS_128: bits[7:4] == 0
constexpr uint64_t kTmeAlgorithmAesXts128 = 0;
// AES_XTS_128: bits[7:4] == 2
constexpr uint64_t kTmeAlgorithmAesXts256 = (uint64_t)2 << 4;

}  // namespace

// Sets the total_memory_kib, free_memory_kib and available_memory_kib fields of
// |info| with information read from proc/meminfo. Returns any error
// encountered probing the memory information. |info| is valid iff no error
// occurred.
void MemoryFetcher::ParseProcMeminfo(mojo_ipc::MemoryInfo* info) {
  std::string file_contents;
  if (!ReadAndTrimString(context_->root_dir().Append(kRelativeProcPath),
                         "meminfo", &file_contents)) {
    CreateErrorAndSendBack(mojo_ipc::ErrorType::kFileReadError,
                           "Unable to read /proc/meminfo");
    return;
  }
  // Parse the meminfo contents for MemTotal, MemFree and MemAvailable. Note
  // that these values are actually reported in KiB from /proc/meminfo, despite
  // claiming to be in kB.
  base::StringPairs keyVals;
  if (!base::SplitStringIntoKeyValuePairs(file_contents, ':', '\n', &keyVals)) {
    CreateErrorAndSendBack(mojo_ipc::ErrorType::kParseError,
                           "Incorrectly formatted /proc/meminfo");
    return;
  }

  bool memtotal_found = false;
  bool memfree_found = false;
  bool memavailable_found = false;
  for (int i = 0; i < keyVals.size(); i++) {
    if (keyVals[i].first == "MemTotal") {
      int memtotal;
      base::StringTokenizer t(keyVals[i].second, " ");
      if (t.GetNext() && base::StringToInt(t.token(), &memtotal) &&
          t.GetNext() && t.token() == "kB") {
        info->total_memory_kib = memtotal;
        memtotal_found = true;
      } else {
        CreateErrorAndSendBack(mojo_ipc::ErrorType::kParseError,
                               "Incorrectly formatted MemTotal");
        return;
      }
    } else if (keyVals[i].first == "MemFree") {
      int memfree;
      base::StringTokenizer t(keyVals[i].second, " ");
      if (t.GetNext() && base::StringToInt(t.token(), &memfree) &&
          t.GetNext() && t.token() == "kB") {
        info->free_memory_kib = memfree;
        memfree_found = true;
      } else {
        CreateErrorAndSendBack(mojo_ipc::ErrorType::kParseError,
                               "Incorrectly formatted MemFree");
        return;
      }
    } else if (keyVals[i].first == "MemAvailable") {
      // Convert from kB to MB and cache the result.
      int memavailable;
      base::StringTokenizer t(keyVals[i].second, " ");
      if (t.GetNext() && base::StringToInt(t.token(), &memavailable) &&
          t.GetNext() && t.token() == "kB") {
        info->available_memory_kib = memavailable;
        memavailable_found = true;
      } else {
        CreateErrorAndSendBack(mojo_ipc::ErrorType::kParseError,
                               "Incorrectly formatted MemAvailable");
        return;
      }
    }
  }

  if (!memtotal_found || !memfree_found || !memavailable_found) {
    std::string error_msg = !memtotal_found ? "Memtotal " : "";
    error_msg += !memfree_found ? "MemFree " : "";
    error_msg += !memavailable_found ? "MemAvailable " : "";
    error_msg += "not found in /proc/meminfo";

    CreateErrorAndSendBack(mojo_ipc::ErrorType::kParseError,
                           std::move(error_msg));
    return;
  }
}

// Sets the page_faults_per_second field of |info| with information read from
// /proc/vmstat. Returns any error encountered probing the memory information.
// |info| is valid iff no error occurred.
void MemoryFetcher::ParseProcVmStat(mojo_ipc::MemoryInfo* info) {
  std::string file_contents;
  if (!ReadAndTrimString(context_->root_dir().Append(kRelativeProcPath),
                         "vmstat", &file_contents)) {
    CreateErrorAndSendBack(mojo_ipc::ErrorType::kFileReadError,
                           "Unable to read /proc/vmstat");
    return;
  }

  // Parse the vmstat contents for pgfault.
  base::StringPairs keyVals;
  if (!base::SplitStringIntoKeyValuePairs(file_contents, ' ', '\n', &keyVals)) {
    CreateErrorAndSendBack(mojo_ipc::ErrorType::kParseError,
                           "Incorrectly formatted /proc/vmstat");
    return;
  }

  bool pgfault_found = false;
  for (int i = 0; i < keyVals.size(); i++) {
    if (keyVals[i].first == "pgfault") {
      uint64_t num_page_faults;
      if (base::StringToUint64(keyVals[i].second, &num_page_faults)) {
        info->page_faults_since_last_boot = num_page_faults;
        pgfault_found = true;
        break;
      } else {
        CreateErrorAndSendBack(mojo_ipc::ErrorType::kParseError,
                               "Incorrectly formatted pgfault");
        return;
      }
    }
  }

  if (!pgfault_found) {
    CreateErrorAndSendBack(mojo_ipc::ErrorType::kParseError,
                           "pgfault not found in /proc/vmstat");
    return;
  }
}

void MemoryFetcher::CreateResultAndSendBack() {
  SendBackResult(mojo_ipc::MemoryResult::NewMemoryInfo(mem_info_.Clone()));
}

void MemoryFetcher::CreateErrorAndSendBack(mojo_ipc::ErrorType error_type,
                                           const std::string& message) {
  SendBackResult(mojo_ipc::MemoryResult::NewError(
      CreateAndLogProbeError(error_type, message)));
}

void MemoryFetcher::SendBackResult(mojo_ipc::MemoryResultPtr result) {
  // Invalid all weak ptrs to prevent other callbacks to be run.
  weak_factory_.InvalidateWeakPtrs();
  if (pending_callbacks_.empty())
    return;
  for (size_t i = 1; i < pending_callbacks_.size(); ++i) {
    std::move(pending_callbacks_[i]).Run(result.Clone());
  }
  std::move(pending_callbacks_[0]).Run(std::move(result));
  pending_callbacks_.clear();
}

// Parse mktme information.
void MemoryFetcher::FetchMktmeInfo() {
  auto mktme_path = context_->root_dir().Append(kRelativeMktmePath);
  // Directory /sys/kernel/mktme existence indicates mktme support.
  if (!base::PathExists(mktme_path)) {
    CreateResultAndSendBack();
    return;
  }
  auto memory_encryption_info = mojo_ipc::MemoryEncryptionInfo::New();
  std::string file_contents;

  // Check if mktme enabled or not.
  if (!ReadAndTrimString(mktme_path, kMktmeActiveFile, &file_contents)) {
    CreateErrorAndSendBack(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read " + mktme_path.Append(kMktmeActiveFile).value());
    return;
  }
  uint32_t value;
  if (!base::StringToUint(file_contents, &value)) {
    CreateErrorAndSendBack(
        mojo_ipc::ErrorType::kParseError,
        "Failed to convert mktme enable state to integer: " + file_contents);
    return;
  }
  memory_encryption_info->encryption_state =
      (value != 0) ? mojo_ipc::EncryptionState::kMktmeEnabled
                   : mojo_ipc::EncryptionState::kEncryptionDisabled;

  // Get max number of key support.
  if (!ReadAndTrimString(mktme_path, kMktmeKeyCountFile, &file_contents)) {
    CreateErrorAndSendBack(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read " + mktme_path.Append(kMktmeKeyCountFile).value());
    return;
  }
  if (!base::StringToUint(file_contents,
                          &memory_encryption_info->max_key_number)) {
    CreateErrorAndSendBack(
        mojo_ipc::ErrorType::kParseError,
        "Failed to convert mktme maximum key number to integer: " +
            file_contents);
    return;
  }

  // Get key length.
  if (!ReadAndTrimString(mktme_path, kMktmeKeyLengthFile, &file_contents)) {
    CreateErrorAndSendBack(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read " + mktme_path.Append(kMktmeKeyLengthFile).value());
    return;
  }
  if (!base::StringToUint(file_contents, &memory_encryption_info->key_length)) {
    CreateErrorAndSendBack(
        mojo_ipc::ErrorType::kParseError,
        "Failed to convert mktme key length to integer: " + file_contents);
    return;
  }

  // Get active algorithm.
  if (!ReadAndTrimString(mktme_path, kMktmeActiveAlgorithmFile,
                         &file_contents)) {
    CreateErrorAndSendBack(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read " +
            mktme_path.Append(kMktmeActiveAlgorithmFile).value());
    return;
  }
  if (file_contents == "AES_XTS_256") {
    memory_encryption_info->active_algorithm =
        mojo_ipc::CryptoAlgorithm::kAesXts256;
  } else if (file_contents == "AES_XTS_128") {
    memory_encryption_info->active_algorithm =
        mojo_ipc::CryptoAlgorithm::kAesXts128;
  } else {
    memory_encryption_info->active_algorithm =
        mojo_ipc::CryptoAlgorithm::kUnknown;
  }
  mem_info_.memory_encryption_info = std::move(memory_encryption_info);
  CreateResultAndSendBack();
}

void MemoryFetcher::ExtractTmeInfoFromMsr() {
  mojo_ipc::MemoryEncryptionInfo info;
  // tme enabled when hardware tme enabled and tme encryption not bypassed.
  bool tme_enable = ((tme_activate_value_ & kTmeEnableBit) &&
                     (!(tme_capability_value_ & kTmeBypassAllowBit) ||
                      !(tme_activate_value_ & kTmeBypassBit)));
  info.encryption_state = tme_enable
                              ? mojo_ipc::EncryptionState::kTmeEnabled
                              : mojo_ipc::EncryptionState::kEncryptionDisabled;
  info.max_key_number = 1;

  if (((tme_activate_value_ & kTmeAlgorithmMask) == kTmeAlgorithmAesXts128) &&
      (tme_capability_value_ & kTmeAllowAesXts128)) {
    info.active_algorithm = mojo_ipc::CryptoAlgorithm::kAesXts128;
    info.key_length = 128;
  } else if (((tme_activate_value_ & kTmeAlgorithmMask) ==
              kTmeAlgorithmAesXts256) &&
             (tme_capability_value_ & kTmeAllowAesXts256)) {
    info.active_algorithm = mojo_ipc::CryptoAlgorithm::kAesXts256;
    info.key_length = 256;
  } else {
    info.active_algorithm = mojo_ipc::CryptoAlgorithm::kUnknown;
    info.key_length = 0;
  }
  mem_info_.memory_encryption_info = info.Clone();
  CreateResultAndSendBack();
}

void MemoryFetcher::HandleReadTmeActivateMsr(
    executor_ipc::ProcessResultPtr status, uint64_t val) {
  DCHECK(mem_info_.memory_encryption_info);
  if (!status->err.empty() || status->return_code != EXIT_SUCCESS) {
    CreateErrorAndSendBack(mojo_ipc::ErrorType::kFileReadError,
                           status->err + " with error code: " +
                               std::to_string(status->return_code));
    return;
  }
  tme_activate_value_ = val;
  ExtractTmeInfoFromMsr();
}

void MemoryFetcher::HandleReadTmeCapabilityMsr(
    executor_ipc::ProcessResultPtr status, uint64_t val) {
  DCHECK(mem_info_.memory_encryption_info);
  if (!status->err.empty() || status->return_code != EXIT_SUCCESS) {
    CreateErrorAndSendBack(mojo_ipc::ErrorType::kFileReadError,
                           status->err + " with error code: " +
                               std::to_string(status->return_code));
    return;
  }
  tme_capability_value_ = val;
  // Read tme activate register.
  context_->executor()->ReadMsr(
      kTmeActivateMsr, base::BindOnce(&MemoryFetcher::HandleReadTmeActivateMsr,
                                      weak_factory_.GetWeakPtr()));
}

void MemoryFetcher::FetchTmeInfo() {
  std::string file_content;
  // First check tme flag in /proc/cpuinfo to see tme support by the CPU or not.
  if (!ReadAndTrimString(context_->root_dir().Append(kRelativeProcCpuInfoPath),
                         &file_content)) {
    CreateErrorAndSendBack(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read " +
            context_->root_dir().Append(kRelativeProcCpuInfoPath).value());
    return;
  }

  std::vector<std::string> lines = base::SplitString(
      file_content, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  std::string flags_line;
  // Parse for the line starting with "flags" in /proc/cpuinfo for CPU 0 only
  // (until first empty line).
  for (const auto& line : lines) {
    if (line.empty()) {
      break;
    }
    if ("flags" == line.substr(0, line.find('\t'))) {
      flags_line = line;
      break;
    }
  }
  // "tme" flag indicates tme supported by CPU.
  if (flags_line.find("tme") == std::string::npos) {
    CreateResultAndSendBack();
    return;
  }

  mem_info_.memory_encryption_info = mojo_ipc::MemoryEncryptionInfo::New();
  // Read tme Capability Register.
  context_->executor()->ReadMsr(
      kTmeCapabilityMsr,
      base::BindOnce(&MemoryFetcher::HandleReadTmeCapabilityMsr,
                     weak_factory_.GetWeakPtr()));
}

void MemoryFetcher::FetchMemoryEncryptionInfo() {
  auto mktme_path = context_->root_dir().Append(kRelativeMktmePath);
  // If mktme support on the platform, fetch mktme telemetry. Otherwise, fetch
  // tme telemery.
  if (base::PathExists(mktme_path)) {
    // Existence of /sys/kernel/mm/mktme folder indicates mktme support on
    // platform.
    FetchMktmeInfo();
    return;
  }
  // Fetches tme info.
  FetchTmeInfo();
}

void MemoryFetcher::FetchMemoryInfo(FetchMemoryInfoCallback callback) {
  pending_callbacks_.push_back(std::move(callback));
  if (pending_callbacks_.size() > 1)
    return;
  ParseProcMeminfo(&mem_info_);
  ParseProcVmStat(&mem_info_);
  FetchMemoryEncryptionInfo();
}

}  // namespace diagnostics
