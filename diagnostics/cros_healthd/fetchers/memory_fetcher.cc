// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/memory_fetcher.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/types/expected.h>
#include <chromeos/dbus/service_constants.h>
#include <concierge/dbus-proxies.h>
#include <session_manager/dbus-proxies.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/executor/constants.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/meminfo_reader.h"
#include "diagnostics/cros_healthd/utils/dbus_utils.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/procfs_utils.h"
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
// Estimated ratio of original_data_size / compressed_data_size in zram.
constexpr int64_t kEstimatedSwapCompressionFactor = 3;

// Returns `MemoryInfo` from reading `/proc/meminfo`. Returns unexpected error
// if error occurs.
base::expected<MemoryInfo, mojom::ProbeErrorPtr> ParseProcMemInfo(
    Context* context) {
  auto memory_info = context->meminfo_reader()->GetInfo();
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
    LOG(WARNING) << "Get unknown crypto algorithm, tme_capability: "
                 << tme_capability << ", tme_activate: " << tme_activate;
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

// Check if the memory value exceeds maximum of uint32 and log error.
void CheckValueAndLogError(std::string_view name, uint64_t value) {
  if (value > UINT32_MAX) {
    LOG(ERROR) << name << " exceeds maximum of uint32";
  }
}

void FinishFetchingCrosVmSmaps(
    Context* context,
    FetchMemoryInfoCallback callback,
    mojom::MemoryInfoPtr info,
    std::unique_ptr<GuestMemoryInfo> guest,
    const base::FilePath& root_dir,
    uint32_t process_id,
    const base::flat_map<uint32_t, std::string>& io_contents) {
  if (io_contents.empty() || !io_contents.contains(process_id)) {
    LOG(ERROR) << "Error while reading crosvm smaps file";
    FetchMemoryEncryptionInfo(context, std::move(callback), std::move(info),
                              root_dir);
    return;
  }

  std::optional<ProcSmaps> smaps = ParseProcSmaps(io_contents.at(process_id));
  if (!smaps) {
    LOG(ERROR) << "Error while parsing crosvm smaps file";
    FetchMemoryEncryptionInfo(context, std::move(callback), std::move(info),
                              root_dir);
    return;
  }
  guest->crosvm_rss = smaps->crosvm_guest_rss;
  guest->crosvm_swap = smaps->crosvm_guest_swap;

  const int64_t original_available_kib = info->available_memory_kib;
  const int64_t adjusted_available_guest = ComputeAdjustedAvailable(*guest);
  info->available_memory_kib += adjusted_available_guest / 1024;
  LOG(INFO) << "Original available memory: " << original_available_kib
            << " kib. Adjusted: " << info->available_memory_kib << " kib";

  FetchMemoryEncryptionInfo(context, std::move(callback), std::move(info),
                            root_dir);
}

void HandleGetBalloonInfo(
    Context* context,
    FetchMemoryInfoCallback callback,
    mojom::MemoryInfoPtr info,
    const base::FilePath& root_dir,
    std::unique_ptr<GuestMemoryInfo> guest,
    const std::string& sanitized_username,
    brillo::Error* error,
    const vm_tools::concierge::GetBalloonInfoResponse& response) {
  if (error || !response.success()) {
    // Failed to retrieve the Balloon info. Give up getting the ARCVM
    // information.
    FetchMemoryEncryptionInfo(context, std::move(callback), std::move(info),
                              root_dir);
    return;
  }

  guest->available_memory = response.balloon_info().available_memory();
  guest->free_memory = response.balloon_info().free_memory();
  guest->balloon_size = response.balloon_info().balloon_size();

  std::optional<int> pid = GetArcVmPid(root_dir);
  if (!pid) {
    // Failed to retrieve the ARCVM PID. Give up getting the ARCVM information.
    FetchMemoryEncryptionInfo(context, std::move(callback), std::move(info),
                              root_dir);
    return;
  }

  // Next step is to get /proc/PID/smaps of the ARCVM.
  context->executor()->GetProcessContents(
      mojom::Executor::ProcFile::kSmaps,
      /*pids=*/{static_cast<uint32_t>(*pid)},
      base::BindOnce(&FinishFetchingCrosVmSmaps, context, std::move(callback),
                     std::move(info), std::move(guest), root_dir, *pid));
}

void HandleGetVmInfo(Context* context,
                     FetchMemoryInfoCallback callback,
                     mojom::MemoryInfoPtr info,
                     const base::FilePath& root_dir,
                     const std::string& sanitized_username,
                     brillo::Error* error,
                     const vm_tools::concierge::GetVmInfoResponse& response) {
  if (error || !response.success()) {
    // Failed to retrieve the VM info. Give up getting the ARCVM information.
    FetchMemoryEncryptionInfo(context, std::move(callback), std::move(info),
                              root_dir);
    return;
  }

  auto guest = std::make_unique<GuestMemoryInfo>();
  guest->allocated_memory = response.vm_info().allocated_memory();

  // Next step is to get the Balloon info.
  vm_tools::concierge::GetBalloonInfoRequest request;
  request.set_name("arcvm");
  request.set_owner_id(sanitized_username);

  auto [on_success, on_error] = SplitDbusCallback(base::BindOnce(
      &HandleGetBalloonInfo, context, std::move(callback), std::move(info),
      root_dir, std::move(guest), sanitized_username));
  context->concierge_proxy()->GetBalloonInfoAsync(
      request, std::move(on_success), std::move(on_error));
}

void HandleRetrievePrimarySession(Context* context,
                                  FetchMemoryInfoCallback callback,
                                  mojom::MemoryInfoPtr info,
                                  const base::FilePath& root_dir,
                                  brillo::Error* error,
                                  const std::string& username,
                                  const std::string& sanitized_username) {
  if (error) {
    // Failed to retrieve the primary session. Give up getting the ARCVM
    // information.
    FetchMemoryEncryptionInfo(context, std::move(callback), std::move(info),
                              root_dir);
    return;
  }

  // Currently, the code only cares about ARCVM as usage of other VMs
  // is much lower than ARCVM.
  vm_tools::concierge::GetVmInfoRequest request;
  request.set_name("arcvm");
  request.set_owner_id(sanitized_username);

  // Next step is to get the ARCVM info.
  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&HandleGetVmInfo, context, std::move(callback),
                     std::move(info), root_dir, sanitized_username));
  context->concierge_proxy()->GetVmInfoAsync(request, std::move(on_success),
                                             std::move(on_error));
}

// Handles ReadFile() response for /proc/iomem, and runs
// FetchMemoryEncryptionInfo() to proceed.
void HandleReadProcIomem(Context* context,
                         FetchMemoryInfoCallback callback,
                         mojom::MemoryInfoPtr info,
                         const base::FilePath& root_dir,
                         const std::optional<std::string>& content) {
  // If /proc/iomem is read successfully, use this content to update
  // info->total_memory_kib with more accurate information.
  if (content.has_value()) {
    auto total = ParseIomemContent(content.value());
    // /proc/iomem still lacks the memory reserved outside of the kernel
    // (ex. firmware). Round up to the next GiB to fill the gap.
    if (total.has_value()) {
      const uint64_t gib = 1 << 30;
      const uint64_t rounded = ((total.value() + gib - 1) / gib) * gib;
      info->total_memory_kib = rounded / 1024;
    }
  }

  // The next step is to get the session information, which is needed to
  // get the guest VM information. Note that there will only ever be at
  // most one ARCVM instance and it will always be for the primary session
  // but the primary session information is still needed to talk to
  // concierge(b/305120263).
  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&HandleRetrievePrimarySession, context,
                     std::move(callback), std::move(info), root_dir));
  context->session_manager_proxy()->RetrievePrimarySessionAsync(
      std::move(on_success), std::move(on_error));
}

}  // namespace

// We want to add available memory in the ARCVM guest to the system wide
// available memory total. However, doing so directly has the potential to
// overcount in two ways:
//
//  - Free memory that has never been touched (or not touched since returned
//    from the balloon) is not backed by any memory in the host, and thus
//    doesn't contribute to system wide available memory at all.
//  - Guest memory that has been touched may be cold enough that the host has
//    evicted it to zram, which is completely transparent to the guest. While
//    this memory is backed by physical memory, it is only backed by ~33% of
//    the expected amount.
//
// We can compute the amount of uncommitted memory by taking the amount of
// memory assigned to the guest (guest_memory_size - balloon_size) and then
// subtracting the amount of memory actually consumed by the crosvm_guest
// memfd (its RSS plus swap). In general, the vast majority of this should
// be free pages in the guest.
//
// To deal with zram, the safest thing to assume is that all guest memory
// in zram is reclaimable in the guest. This will likely significantly
// underestimate the amount of reclaimable memory, but that's better than
// presenting false information to the user.
//
// See go/crosmdu for the original proposal.
int64_t ComputeAdjustedAvailable(const GuestMemoryInfo& guest) {
  const int64_t uncommitted = guest.allocated_memory - guest.balloon_size -
                              guest.crosvm_rss - guest.crosvm_swap;
  const int64_t reclaimable = guest.available_memory - guest.free_memory;
  const int64_t discounted_reclaimable =
      std::max(reclaimable - guest.crosvm_swap, static_cast<int64_t>(0)) +
      std::min(reclaimable, guest.crosvm_swap) /
          kEstimatedSwapCompressionFactor;
  const int64_t adjusted_available =
      std::max(guest.free_memory - uncommitted, static_cast<int64_t>(0)) +
      discounted_reclaimable;
  return adjusted_available;
}

void FetchMemoryInfo(Context* context, FetchMemoryInfoCallback callback) {
  const auto& root_dir = GetRootDir();
  auto info = mojom::MemoryInfo::New();

  auto meminfo_result = ParseProcMemInfo(context);
  if (!meminfo_result.has_value()) {
    std::move(callback).Run(
        mojom::MemoryResult::NewError(std::move(meminfo_result.error())));
    return;
  }
  const auto& meminfo = meminfo_result.value();
  info->total_memory_kib = meminfo.total_memory_kib;
  CheckValueAndLogError("total_memory_kib", meminfo.total_memory_kib);
  info->free_memory_kib = meminfo.free_memory_kib;
  CheckValueAndLogError("free_memory_kib", meminfo.free_memory_kib);
  info->available_memory_kib = meminfo.available_memory_kib;
  CheckValueAndLogError("available_memory_kib", meminfo.available_memory_kib);

  info->buffers_kib = meminfo.buffers_kib;
  info->page_cache_kib = meminfo.page_cache_kib;
  info->shared_memory_kib = meminfo.shared_memory_kib;

  info->active_memory_kib = meminfo.active_memory_kib;
  info->inactive_memory_kib = meminfo.inactive_memory_kib;

  info->total_swap_memory_kib = meminfo.total_swap_memory_kib;
  info->free_swap_memory_kib = meminfo.free_swap_memory_kib;
  info->cached_swap_memory_kib = meminfo.cached_swap_memory_kib;

  info->total_slab_memory_kib = meminfo.total_slab_memory_kib;
  info->reclaimable_slab_memory_kib = meminfo.reclaimable_slab_memory_kib;
  info->unreclaimable_slab_memory_kib = meminfo.unreclaimable_slab_memory_kib;

  auto page_faults_result = ParseProcVmStat(root_dir);
  if (!page_faults_result.has_value()) {
    std::move(callback).Run(
        mojom::MemoryResult::NewError(std::move(page_faults_result.error())));
    return;
  }
  info->page_faults_since_last_boot = page_faults_result.value();

  // MemTotal in /proc/meminfo lacks some memory reserved by the kernel.
  // Read /proc/iomem to get the more accurate information via the executor
  // as the root permissions are needed.
  context->executor()->ReadFile(
      mojom::Executor::File::kProcIomem,
      base::BindOnce(&HandleReadProcIomem, context, std::move(callback),
                     std::move(info), root_dir));
}

}  // namespace diagnostics
