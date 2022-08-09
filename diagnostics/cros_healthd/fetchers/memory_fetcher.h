// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_MEMORY_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_MEMORY_FETCHER_H_

#include <optional>
#include <string>
#include <vector>

#include "diagnostics/cros_healthd/executor/constants.h"
#include "diagnostics/cros_healthd/executor/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace mojom = chromeos::cros_healthd::mojom;

// The MemoryFetcher class is responsible for gathering memory info.
class MemoryFetcher final : public BaseFetcher {
 public:
  using FetchMemoryInfoCallback =
      base::OnceCallback<void(mojom::MemoryResultPtr)>;
  using BaseFetcher::BaseFetcher;
  // Returns a structure with either the device's memory info or the error that
  // occurred fetching the information.
  void FetchMemoryInfo(FetchMemoryInfoCallback callback);

 private:
  using OptionalProbeErrorPtr = std::optional<mojom::ProbeErrorPtr>;
  void FetchMemoryEncryptionInfo();
  void FetchMktmeInfo();
  void FetchTmeInfo();
  void HandleReadTmeCapabilityMsr(mojom::NullableUint64Ptr val);
  void HandleReadTmeActivateMsr(mojom::NullableUint64Ptr val);
  void ExtractTmeInfoFromMsr();
  void CreateResultAndSendBack();
  void CreateErrorAndSendBack(mojom::ErrorType error_type,
                              const std::string& message);
  void SendBackResult(mojom::MemoryResultPtr result);
  void ParseProcMemInfo(mojom::MemoryInfo* info);
  void ParseProcVmStat(mojom::MemoryInfo* info);
  uint64_t tme_capability_value_;
  uint64_t tme_activate_value_;
  mojom::MemoryInfo mem_info_;
  std::vector<FetchMemoryInfoCallback> pending_callbacks_;
  base::WeakPtrFactory<MemoryFetcher> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_MEMORY_FETCHER_H_
