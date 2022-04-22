// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_CPU_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_CPU_FETCHER_H_

#include <map>
#include <string>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"

#include "diagnostics/cros_healthd/executor/constants.h"
#include "diagnostics/cros_healthd/executor/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/fetchers/async_fetcher.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"
#include "diagnostics/mojom/public/nullable_primitives.mojom.h"

namespace diagnostics {

// Directory containing SoC ID info.
inline constexpr char kRelativeSoCDevicesDir[] = "sys/bus/soc/devices/";
// File containing Arm device tree compatible string.
inline constexpr char kRelativeCompatibleFile[] =
    "sys/firmware/devicetree/base/compatible";

// Relative path from root of the CPU directory.
inline constexpr char kRelativeCpuDir[] = "sys/devices/system/cpu";
// File read from the CPU directory.
inline constexpr char kPresentFileName[] = "present";
// Files read from the C-state directory.
inline constexpr char kCStateNameFileName[] = "name";
inline constexpr char kCStateTimeFileName[] = "time";
// Files read from the CPU policy directory.
inline constexpr char kScalingMaxFreqFileName[] = "scaling_max_freq";
inline constexpr char kScalingCurFreqFileName[] = "scaling_cur_freq";
inline constexpr char kCpuinfoMaxFreqFileName[] = "cpuinfo_max_freq";
// Path from relative cpu dir to the vulnerabilities directory.
inline constexpr char kVulnerabilityDirName[] = "vulnerabilities";
// Path from relative cpu dir to the SMT directory.
inline constexpr char kSmtDirName[] = "smt";
// File to find the status of SMT.
inline constexpr char kSmtActiveFileName[] = "active";
inline constexpr char kSmtControlFileName[] = "control";

// File to read Keylocker information.
inline constexpr char kRelativeCryptoFilePath[] = "proc/crypto";

// File to see if KVM exists.
inline constexpr char kRelativeKvmFilePath[] = "dev/kvm";

// The different bits that indicates what kind of CPU virtualization is enabled
// and locked.
inline constexpr uint64_t kIA32FeatureLocked = 1llu << 0;
inline constexpr uint64_t kIA32FeatureEnableVmxInsideSmx = 1llu << 1;
inline constexpr uint64_t kIA32FeatureEnableVmxOutsideSmx = 1llu << 2;
inline constexpr uint64_t kVmCrLockedBit = 1llu << 3;
inline constexpr uint64_t kVmCrSvmeDisabledBit = 1llu << 4;

// Returns an absolute path to the C-state directory for the logical CPU with ID
// |logical_id|. On a real device, this will be
// /sys/devices/system/cpu/cpu|logical_id|/cpuidle.
base::FilePath GetCStateDirectoryPath(const base::FilePath& root_dir,
                                      int logical_id);

// Returns an absolute path to the CPU freq directory for the logical CPU with
// ID |logical_id|. On a real device, this will be
// /sys/devices/system/cpu/cpufreq/policy|logical_id| if the CPU has a governing
// policy, or /sys/devices/system/cpu/|logical_id|/cpufreq without.
base::FilePath GetCpuFreqDirectoryPath(const base::FilePath& root_dir,
                                       int logical_id);

// Returns the parsed vulnerability status from reading the vulnerability
// message. This function is exported for testing.
chromeos::cros_healthd::mojom::VulnerabilityInfo::Status
GetVulnerabilityStatusFromMessage(const std::string& message);

// The CpuFetcher class is responsible for gathering CPU info reported by
// cros_healthd.
class CpuFetcher final
    : public AsyncFetcherInterface<chromeos::cros_healthd::mojom::CpuResult> {
 public:
  using AsyncFetcherInterface::AsyncFetcherInterface;

 private:
  // AsyncFetcherInterface override
  void FetchImpl(ResultCallback callback) override;

  // Aggregates data from |processor_info| and |logical_ids_to_stat_contents| to
  // form the final CpuResultPtr. It's assumed that all CPUs on the device share
  // the same |architecture|.
  chromeos::cros_healthd::mojom::CpuResultPtr GetCpuInfoFromProcessorInfo();

  // Reads and parses the total number of threads available on the device and
  // store into |num_total_threads|. Returns true on success and false
  // otherwise.
  bool FetchNumTotalThreads();

  // Record the cpu architecture into |architecture|. Returns true on success
  // and false otherwise.
  bool FetchArchitecture();

  // Record the keylocker information into |architecture|. Returns true on
  // success and false otherwise.
  bool FetchKeylockerInfo();

  // Fetch cpu temperature channels and store into |temperature_channels|.
  // Returns true on success and false otherwise.
  bool FetchCpuTemperatures();

  // Read and parse general virtualization info and store into |virtualization|.
  // Returns true on success and false otherwise.
  bool FetchVirtualization();

  // Read and parse cpu vulnerabilities and store into |vulnerabilities|.
  // Returns true on success and false otherwise.
  bool FetchVulnerabilities();

  // Calls |callback_| and passes the result. If |all_callback_called| or
  // |error_| is set, the result is a ProbeError, otherwise it is |cpu_info_|.
  void HandleCallbackComplete(bool all_callback_called);

  // Callback function to handle ReadMsr() call reading vmx registers.
  void HandleVmxReadMsr(uint32_t index, mojom::NullableUint64Ptr val);

  // Callback function to handle ReadMsr() call reading svm registers.
  void HandleSvmReadMsr(uint32_t index, mojom::NullableUint64Ptr val);

  // Calls ReadMsr based on the virtualization capability of each physical cpu.
  void FetchPhysicalCpusVirtualizationInfo(CallbackBarrier& barrier);

  // Logs |message| and sets |error_|. Only do the logging if |error_| has been
  // set.
  void LogAndSetError(chromeos::cros_healthd::mojom::ErrorType type,
                      const std::string& message);

  // Stores the callback received from FetchImpl.
  ResultCallback callback_;
  // Stores the error that will be returned. HandleCallbackComplete will report
  // error if this is set.
  chromeos::cros_healthd::mojom::ProbeErrorPtr error_;
  // Stores the final cpu info that will be returned.
  chromeos::cros_healthd::mojom::CpuInfoPtr cpu_info_;

  // Maintains a map that maps each physical cpu id to its first corresponding
  // logical cpu id.
  std::map<uint32_t, uint32_t> physical_id_to_first_logical_id_;
  // Must be the last member of the class.
  base::WeakPtrFactory<CpuFetcher> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_CPU_FETCHER_H_
