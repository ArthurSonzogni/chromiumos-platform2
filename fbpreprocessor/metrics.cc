// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/metrics.h"

#include <memory>
#include <string_view>
#include <utility>

#include <base/containers/fixed_flat_map.h>
#include <base/strings/strcat.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>
#include <metrics/metrics_library.h>

#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/pseudonymization_manager.h"

namespace fbpreprocessor {
namespace {
// Type of firmware dumps reported to UMA. These values are persisted to logs.
// Entries should not be renumbered and numeric values should never be reused
// since they will be used by UMA to interpret the data. The values of this enum
// must be kept in sync with the type definitions in fbpreprocessor.proto.
enum class UMAFirmwareType {
  kUnknown = DebugDump::TYPE_UNSPECIFIED,
  kWiFi = DebugDump::WIFI,
  kBluetooth = DebugDump::BLUETOOTH,
  kMaxValue = kBluetooth,
};

constexpr std::string_view kPrefix{"Platform.FbPreprocessor."};

constexpr auto kDumpTypeName =
    base::MakeFixedFlatMap<FirmwareDump::Type, std::string_view>({
        {FirmwareDump::Type::kWiFi, "WiFi"},
        {FirmwareDump::Type::kBluetooth, "Bluetooth"},
    });

std::string_view ToString(FirmwareDump::Type type) {
  if (kDumpTypeName.contains(type)) {
    return kDumpTypeName.at(type);
  }
  return "UnknownType";
}

UMAFirmwareType ConvertToUMAType(fbpreprocessor::FirmwareDump::Type type) {
  switch (type) {
    case FirmwareDump::Type::kWiFi:
      return UMAFirmwareType::kWiFi;
    case FirmwareDump::Type::kBluetooth:
      return UMAFirmwareType::kBluetooth;
  }
  return UMAFirmwareType::kUnknown;
}
}  // namespace

Metrics::Metrics() : library_(std::make_unique<MetricsLibrary>()) {}
Metrics::~Metrics() = default;

void Metrics::SetLibraryForTesting(
    std::unique_ptr<MetricsLibraryInterface> lib) {
  library_ = std::move(lib);
}

bool Metrics::SendNumberOfAvailableDumps(FirmwareDump::Type fw_type, int num) {
  return library_->SendLinearToUMA(
      base::StrCat({kPrefix, ToString(fw_type), ".Output.Number"}), num,
      PseudonymizationManager::kMaxProcessedDumps + 1);
}

bool Metrics::SendAllowedStatus(FirmwareDump::Type fw_type,
                                CollectionAllowedStatus status) {
  return library_->SendEnumToUMA(
      base::StrCat({kPrefix, ToString(fw_type), ".Collection.Allowed"}),
      status);
}

bool Metrics::SendPseudonymizationFirmwareType(FirmwareDump::Type fw_type) {
  return library_->SendEnumToUMA(
      base::StrCat({kPrefix, "Pseudonymization.DumpType"}),
      ConvertToUMAType(fw_type));
}

bool Metrics::SendPseudonymizationResult(FirmwareDump::Type fw_type,
                                         PseudonymizationResult result) {
  return library_->SendEnumToUMA(
      base::StrCat({kPrefix, ToString(fw_type), ".Pseudonymization.Result"}),
      result);
}

}  // namespace fbpreprocessor
