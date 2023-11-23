// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/memory_fetcher.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/types/expected.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/executor/constants.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/memory_info.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

// Path to procfs, relative to the root directory.
constexpr char kRelativeProcCpuInfoPath[] = "proc/cpuinfo";
constexpr char kRelativeVmstatProcPath[] = "proc/vmstat";
constexpr char kRelativeMktmePath[] = "sys/kernel/mm/mktme";
constexpr char kMktmeActiveFile[] = "active";
constexpr char kMktmeActiveAlgorithmFile[] = "active_algo";
constexpr char kMktmeKeyCountFile[] = "keycnt";
constexpr char kMktmeKeyLengthFile[] = "keylen";
constexpr uint64_t kTmeBypassAllowBit = (uint64_t)1 << 31;
constexpr uint64_t kTmeAllowAesXts128 = 1;
constexpr uint64_t kTmeAllowAesXts256 = (uint64_t)1 << 2;
constexpr uint64_t kTmeEnableBit = (uint64_t)1 << 1;
constexpr uint64_t kTmeBypassBit = (uint64_t)1 << 31;
// tme algorithm mask bits[7:4].
constexpr uint64_t kTmeAlgorithmMask = ((uint64_t)1 << 8) - ((uint64_t)1 << 4);
// AES_XTS_128: bits[7:4] == 0
constexpr uint64_t kTmeAlgorithmAesXts128 = 0;
// AES_XTS_128: bits[7:4] == 2
constexpr uint64_t kTmeAlgorithmAesXts256 = (uint64_t)2 << 4;

// Returns `MemoryInfo` from reading `/proc/meminfo`. Returns unexpected error
// if error occurs.
base::expected<MemoryInfo, mojom::ProbeErrorPtr> ParseProcMemInfo(
    const base::FilePath& root_dir) {
  auto memory_info = MemoryInfo::ParseFrom(root_dir);
  if (!memory_info.has_value()) {
    return base::unexpected(CreateAndLogProbeError(
        mojom::ErrorType::kParseError, "Error parsing /proc/meminfo"));
  }
  return base::ok(memory_info.value());
}

// Returns `page_faults` from reading `/proc/vmstat`. Returns unexpected error
// if error occurs.
base::expected<uint64_t, mojom::ProbeErrorPtr> ParseProcVmStat(
    const base::FilePath& root_dir) {
  std::string file_contents;
  if (!ReadAndTrimString(root_dir.Append(kRelativeVmstatProcPath),
                         &file_contents)) {
    return base::unexpected(CreateAndLogProbeError(
        mojom::ErrorType::kFileReadError, "Unable to read /proc/vmstat"));
  }

  // Parse the vmstat contents for pgfault.
  base::StringPairs keyVals;
  if (!base::SplitStringIntoKeyValuePairs(file_contents, ' ', '\n', &keyVals)) {
    return base::unexpected(CreateAndLogProbeError(
        mojom::ErrorType::kParseError, "Incorrectly formatted /proc/vmstat"));
  }

  for (const auto& [key, value] : keyVals) {
    if (key == "pgfault") {
      uint64_t page_faults;
      if (!base::StringToUint64(value, &page_faults)) {
        return base::unexpected(CreateAndLogProbeError(
            mojom::ErrorType::kParseError, "Incorrectly formatted pgfault"));
      }
      return page_faults;
    }
  }

  return base::unexpected(CreateAndLogProbeError(
      mojom::ErrorType::kParseError, "pgfault not found in /proc/vmstat"));
}

// Parses mktme information and returns. Returns unexpected error if error
// occurs.
base::expected<mojom::MemoryEncryptionInfoPtr, mojom::ProbeErrorPtr>
FetchMktmeInfo(const base::FilePath& mktme_path) {
  auto memory_encryption_info = mojom::MemoryEncryptionInfo::New();
  std::string file_contents;

  // Check if mktme enabled or not.
  if (!ReadAndTrimString(mktme_path, kMktmeActiveFile, &file_contents)) {
    return base::unexpected(
        CreateAndLogProbeError(mojom::ErrorType::kFileReadError,
                               "Unable to read /sys/kernel/mm/mktme/active"));
  }
  uint32_t value;
  if (!base::StringToUint(file_contents, &value)) {
    return base::unexpected(CreateAndLogProbeError(
        mojom::ErrorType::kParseError,
        "Failed to convert mktme enable state to integer: " + file_contents));
  }
  memory_encryption_info->encryption_state =
      (value != 0) ? mojom::EncryptionState::kMktmeEnabled
                   : mojom::EncryptionState::kEncryptionDisabled;

  // Get max number of key support.
  if (!ReadAndTrimString(mktme_path, kMktmeKeyCountFile, &file_contents)) {
    return base::unexpected(
        CreateAndLogProbeError(mojom::ErrorType::kFileReadError,
                               "Unable to read /sys/kernel/mm/mktme/keycnt"));
  }
  if (!base::StringToUint(file_contents,
                          &memory_encryption_info->max_key_number)) {
    return base::unexpected(CreateAndLogProbeError(
        mojom::ErrorType::kParseError,
        "Failed to convert mktme maximum key number to integer: " +
            file_contents));
  }

  // Get key length.
  if (!ReadAndTrimString(mktme_path, kMktmeKeyLengthFile, &file_contents)) {
    return base::unexpected(
        CreateAndLogProbeError(mojom::ErrorType::kFileReadError,
                               "Unable to read /sys/kernel/mm/mktme/keylen"));
  }
  if (!base::StringToUint(file_contents, &memory_encryption_info->key_length)) {
    return base::unexpected(CreateAndLogProbeError(
        mojom::ErrorType::kParseError,
        "Failed to convert mktme key length to integer: " + file_contents));
  }

  // Get active algorithm.
  if (!ReadAndTrimString(mktme_path, kMktmeActiveAlgorithmFile,
                         &file_contents)) {
    return base::unexpected(CreateAndLogProbeError(
        mojom::ErrorType::kFileReadError,
        "Unable to read /sys/kernel/mm/mktme/active_algo"));
  }
  if (file_contents == "AES_XTS_256") {
    memory_encryption_info->active_algorithm =
        mojom::CryptoAlgorithm::kAesXts256;
  } else if (file_contents == "AES_XTS_128") {
    memory_encryption_info->active_algorithm =
        mojom::CryptoAlgorithm::kAesXts128;
  } else {
    memory_encryption_info->active_algorithm = mojom::CryptoAlgorithm::kUnknown;
  }

  return base::ok(std::move(memory_encryption_info));
}

mojom::MemoryEncryptionInfoPtr ExtractTmeInfoFromMsr(uint64_t tme_capability,
                                                     uint64_t tme_activate) {
  auto info = mojom::MemoryEncryptionInfo::New();
  // tme enabled when hardware tme enabled and tme encryption not bypassed.
  bool tme_enable = ((tme_activate & kTmeEnableBit) &&
                     (!(tme_capability & kTmeBypassAllowBit) ||
                      !(tme_activate & kTmeBypassBit)));
  info->encryption_state = tme_enable
                               ? mojom::EncryptionState::kTmeEnabled
                               : mojom::EncryptionState::kEncryptionDisabled;
  info->max_key_number = 1;

  if (((tme_activate & kTmeAlgorithmMask) == kTmeAlgorithmAesXts128) &&
      (tme_capability & kTmeAllowAesXts128)) {
    info->active_algorithm = mojom::CryptoAlgorithm::kAesXts128;
    info->key_length = 128;
  } else if (((tme_activate & kTmeAlgorithmMask) == kTmeAlgorithmAesXts256) &&
             (tme_capability & kTmeAllowAesXts256)) {
    info->active_algorithm = mojom::CryptoAlgorithm::kAesXts256;
    info->key_length = 256;
  } else {
    info->active_algorithm = mojom::CryptoAlgorithm::kUnknown;
    info->key_length = 0;
  }
  return info;
}

void HandleReadTmeActivateMsr(FetchMemoryInfoCallback callback,
                              mojom::MemoryInfoPtr info,
                              uint64_t tme_capability,
                              std::optional<uint64_t> tme_activate) {
  if (!tme_activate.has_value()) {
    std::move(callback).Run(mojom::MemoryResult::NewError(
        CreateAndLogProbeError(mojom::ErrorType::kFileReadError,
                               "Error while reading tme activate msr")));
    return;
  }
  info->memory_encryption_info =
      ExtractTmeInfoFromMsr(tme_capability, tme_activate.value());
  std::move(callback).Run(mojom::MemoryResult::NewMemoryInfo(std::move(info)));
}

void HandleReadTmeCapabilityMsr(Context* context,
                                FetchMemoryInfoCallback callback,
                                mojom::MemoryInfoPtr info,
                                std::optional<uint64_t> tme_capability) {
  if (!tme_capability.has_value()) {
    std::move(callback).Run(mojom::MemoryResult::NewError(
        CreateAndLogProbeError(mojom::ErrorType::kFileReadError,
                               "Error while reading tme capability msr")));
    return;
  }
  // Values of MSR registers IA32_TME_ACTIVATE_MSR (0x982) will be the same in
  // all CPU cores. Therefore, we are only interested in reading the values in
  // CPU0.
  context->executor()->ReadMsr(
      /*msr_reg=*/cpu_msr::kIA32TmeActivate, /*cpu_index=*/0,
      base::BindOnce(&HandleReadTmeActivateMsr, std::move(callback),
                     std::move(info), tme_capability.value()));
}

// Check tme flag in /proc/cpuinfo to see if tme is supported by the CPU or not
// and returns the result. Returns unexpected error if error occurs.
base::expected<bool, mojom::ProbeErrorPtr> IsTmeSupportedByCpu(
    const base::FilePath& root_dir) {
  std::string file_content;
  if (!ReadAndTrimString(root_dir.Append(kRelativeProcCpuInfoPath),
                         &file_content)) {
    return base::unexpected(CreateAndLogProbeError(
        mojom::ErrorType::kFileReadError, "Unable to read /proc/cpuinfo"));
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
  return flags_line.find("tme") != std::string::npos;
}

void FetchTmeInfo(Context* context,
                  FetchMemoryInfoCallback callback,
                  mojom::MemoryInfoPtr info) {
  // Values of MSR registers IA32_TME_CAPABILITY (0x981) will be the same in all
  // CPU cores. Therefore, we are only interested in reading the values in CPU0.
  context->executor()->ReadMsr(
      /*msr_reg=*/cpu_msr::kIA32TmeCapability, /*cpu_index=*/0,
      base::BindOnce(&HandleReadTmeCapabilityMsr, context, std::move(callback),
                     std::move(info)));
}

// Fetches mktme info if mktme is supported. Otherwise, fetches tme info.
void FetchMemoryEncryptionInfo(Context* context,
                               FetchMemoryInfoCallback callback,
                               mojom::MemoryInfoPtr info,
                               const base::FilePath& root_dir) {
  auto mktme_path = root_dir.Append(kRelativeMktmePath);
  // Existence of /sys/kernel/mm/mktme folder indicates mktme support.
  if (base::PathExists(mktme_path)) {
    auto memory_encryption_result = FetchMktmeInfo(mktme_path);
    if (!memory_encryption_result.has_value()) {
      std::move(callback).Run(mojom::MemoryResult::NewError(
          std::move(memory_encryption_result.error())));
      return;
    }
    info->memory_encryption_info = std::move(memory_encryption_result.value());
    std::move(callback).Run(
        mojom::MemoryResult::NewMemoryInfo(std::move(info)));
    return;
  }

  // Check if tme is supported.
  auto tme_supported_result = IsTmeSupportedByCpu(root_dir);
  if (!tme_supported_result.has_value()) {
    std::move(callback).Run(
        mojom::MemoryResult::NewError(std::move(tme_supported_result.error())));
  } else if (!tme_supported_result.value()) {
    std::move(callback).Run(
        mojom::MemoryResult::NewMemoryInfo(std::move(info)));
  } else {
    FetchTmeInfo(context, std::move(callback), std::move(info));
  }
}

}  // namespace

void FetchMemoryInfo(Context* context, FetchMemoryInfoCallback callback) {
  auto root_dir = GetRootDir();
  auto info = mojom::MemoryInfo::New();

  auto meminfo_result = ParseProcMemInfo(root_dir);
  if (!meminfo_result.has_value()) {
    std::move(callback).Run(
        mojom::MemoryResult::NewError(std::move(meminfo_result.error())));
    return;
  }
  info->total_memory_kib = meminfo_result.value().total_memory_kib;
  info->free_memory_kib = meminfo_result.value().free_memory_kib;
  info->available_memory_kib = meminfo_result.value().available_memory_kib;

  auto page_faults_result = ParseProcVmStat(root_dir);
  if (!page_faults_result.has_value()) {
    std::move(callback).Run(
        mojom::MemoryResult::NewError(std::move(page_faults_result.error())));
    return;
  }
  info->page_faults_since_last_boot = page_faults_result.value();

  FetchMemoryEncryptionInfo(context, std::move(callback), std::move(info),
                            root_dir);
}

}  // namespace diagnostics
