// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/network_interface_fetcher.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_enumerator.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_tokenizer.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <re2/re2.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using OptionalProbeErrorPtr = std::optional<mojom::ProbeErrorPtr>;
constexpr auto kInterfaceNameRegex = R"(\s*Interface\s+([A-Za-z0-9]+)\s*)";
constexpr auto kLinkNoConnectionRegex = R"((Not\s+connected.)\s*)";
constexpr auto kAccessPointRegex =
    R"(Connected\s+to\s+(\w{2}:\w{2}:\w{2}:\w{2}:\w{2}:\w{2}).*)";
constexpr auto kEncryptionRegex = R"(\s*(capability:\s+\w+\s+Privacy).+)";
// Parse the fifth line from BSS line to get encryption state.
constexpr uint32_t kEncryptionLineOffset = 5;

// This function will return the value with OutputType of first token if the
// second token matches with <unit_name>.
// Example with buffer "500 MBit/s", unit_name "Mbit/s", the function shall
// return 500.
template <typename OutputType>
bool GetDoubleValueWithUnit(const std::string& buffer,
                            const std::string& unit_name,
                            OutputType* out) {
  base::StringTokenizer t(buffer, " ");
  double value;
  if (t.GetNext() && base::StringToDouble(t.token(), &value) && t.GetNext() &&
      t.token() == unit_name) {
    *out = static_cast<OutputType>(value);
    return true;
  }
  return false;
}

}  // namespace

// This function handles the callback from executor()->GetScanDump. It will
// extract data of tx power from "iw <interface> scan dump" command.
void NetworkInterfaceFetcher::HandleScanDump(
    mojom::ExecutedProcessResultPtr result) {
  DCHECK(wireless_info_);
  DCHECK(wireless_info_->wireless_link_info);
  std::string err = result->err;
  int32_t return_code = result->return_code;
  if (!err.empty() || return_code != EXIT_SUCCESS) {
    LOG(ERROR) << "executor()->GetScanDump failed with error code: "
               << return_code;
    CreateErrorToSendBack(mojom::ErrorType::kSystemUtilityError,
                          "executor()->GetScanDump failed with error code: " +
                              std::to_string(return_code));
    return;
  }
  std::string output = result->out;
  std::vector<std::string> lines = base::SplitString(
      output, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  std::string encryption;
  wireless_info_->wireless_link_info->encyption_on = false;
  // Example of bss_str line: "BSS b0:e4:d5:6f:65:1b(on wlan0) -- associated".
  std::string bss_str =
      "BSS " + wireless_info_->wireless_link_info->access_point_address_str +
      "(on " + wireless_info_->interface_name + ") -- associated";
  for (uint32_t index = 0; index < lines.size(); index++) {
    if (lines[index] == bss_str) {
      // Only look at Privacy bit of AP that the WiFi adapter connected to.
      if (((index + kEncryptionLineOffset) < lines.size()) &&
          (RE2::FullMatch(lines[index + kEncryptionLineOffset],
                          kEncryptionRegex, &encryption))) {
        wireless_info_->wireless_link_info->encyption_on = true;
      }
      break;
    }
  }
  CreateResultToSendBack();
}

// This function handles the callback from executor()->GetInfo. It will
// extract data of tx power from "iw <interface> info" command.
void NetworkInterfaceFetcher::HandleInfoAndExecuteGetScanDump(
    mojom::ExecutedProcessResultPtr result) {
  DCHECK(wireless_info_);
  DCHECK(wireless_info_->wireless_link_info);
  std::string err = result->err;
  int32_t return_code = result->return_code;
  if (!err.empty() || return_code != EXIT_SUCCESS) {
    LOG(ERROR) << "executor()->GetInfo failed with error code: " << return_code;
    CreateErrorToSendBack(mojom::ErrorType::kSystemUtilityError,
                          "executor()->GetInfo failed with error code: " +
                              std::to_string(return_code));
    return;
  }
  std::string output = result->out;
  std::vector<std::string> lines = base::SplitString(
      output, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  bool tx_power_found = false;
  for (const auto& line : lines) {
    base::StringTokenizer t(line, " ");
    if (t.GetNext() && t.token() == "txpower") {
      double tx_power;
      if (t.GetNext() && base::StringToDouble(t.token(), &tx_power) &&
          t.GetNext() && t.token() == "dBm") {
        wireless_info_->wireless_link_info->tx_power_dBm = (int32_t)tx_power;
        tx_power_found = true;
        break;
      }
    }
  }

  if (!tx_power_found) {
    CreateErrorToSendBack(mojom::ErrorType::kParseError,
                          std::string(__func__) + ": output parse error.");
    return;
  }
  context_->executor()->GetScanDump(
      wireless_info_->interface_name,
      base::BindOnce(&NetworkInterfaceFetcher::HandleScanDump,
                     weak_factory_.GetWeakPtr()));
}

// This function handles the callback from executor()->GetLink. It will
// extract data of access point, bit rates, signal level from
// "iw <interface> link" command.
void NetworkInterfaceFetcher::HandleLinkAndExecuteIwExecuteGetInfo(
    mojom::ExecutedProcessResultPtr result) {
  DCHECK(wireless_info_);
  std::string err = result->err;
  int32_t return_code = result->return_code;
  if (!err.empty() || return_code != EXIT_SUCCESS) {
    LOG(ERROR) << "executor()->GetLink failed with error code: " << return_code;
    CreateErrorToSendBack(mojom::ErrorType::kSystemUtilityError,
                          "executor()->GetLink failed with error code: " +
                              std::to_string(return_code));
    return;
  }
  std::string regex_result;
  std::string output = result->out;
  // if device is not connected, return without link information.
  if (RE2::FullMatch(output, kLinkNoConnectionRegex, &regex_result)) {
    CreateResultToSendBack();
    return;
  }
  auto link_info = mojom::WirelessLinkInfo::New();
  std::vector<std::string> lines = base::SplitString(
      output, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // Extract the first line.
  std::string first_line = lines[0];
  bool access_point_found = false;
  if (!RE2::FullMatch(first_line, kAccessPointRegex, &regex_result)) {
    CreateErrorToSendBack(mojom::ErrorType::kParseError,
                          std::string(__func__) + ": output parse error.");
  }
  link_info->access_point_address_str = regex_result;
  access_point_found = true;

  // Erase the the first line of the vector so StringPairs can be used.
  lines.erase(lines.begin());

  // The buffer the original with the first time removed.
  std::string output_left = base::JoinString(lines, "\n");
  base::StringPairs keyVals;
  if (!base::SplitStringIntoKeyValuePairs(output_left, ':', '\n', &keyVals)) {
    CreateErrorToSendBack(mojom::ErrorType::kParseError,
                          std::string(__func__) + ": output parse error.");
    return;
  }
  bool rx_bitrate_found = false;
  bool tx_bitrate_found = false;
  bool signal_found = false;
  for (int i = 0; i < keyVals.size(); i++) {
    if (keyVals[i].first == "signal") {
      if (GetDoubleValueWithUnit(keyVals[i].second, "dBm",
                                 &link_info->signal_level_dBm)) {
        if (link_info->signal_level_dBm < -110) {
          link_info->link_quality = 0;
        } else if (link_info->signal_level_dBm > -40) {
          link_info->link_quality = 70;
        } else {
          link_info->link_quality = link_info->signal_level_dBm + 110;
        }
        signal_found = true;
      }
    } else if (keyVals[i].first == "rx bitrate") {
      if (GetDoubleValueWithUnit(keyVals[i].second, "MBit/s",
                                 &link_info->rx_bit_rate_mbps)) {
        rx_bitrate_found = true;
      }
    } else if (keyVals[i].first == "tx bitrate") {
      if (GetDoubleValueWithUnit(keyVals[i].second, "MBit/s",
                                 &link_info->tx_bit_rate_mbps)) {
        tx_bitrate_found = true;
      }
    }
  }

  if (!access_point_found || !signal_found || !rx_bitrate_found ||
      !tx_bitrate_found) {
    CreateErrorToSendBack(mojom::ErrorType::kParseError,
                          std::string(__func__) + ": output parse error.");
    return;
  }
  wireless_info_->wireless_link_info = std::move(link_info);
  context_->executor()->GetInfo(
      wireless_info_->interface_name,
      base::BindOnce(&NetworkInterfaceFetcher::HandleInfoAndExecuteGetScanDump,
                     weak_factory_.GetWeakPtr()));
}

// This function handles the callback from executor()->GetInterfaces. It will
// extract all the wireless interfacees from "iw dev" command.
void NetworkInterfaceFetcher::HandleInterfaceNameAndExecuteGetLink(
    mojom::ExecutedProcessResultPtr result) {
  std::string err = result->err;
  int32_t return_code = result->return_code;
  if (!err.empty() || return_code != EXIT_SUCCESS) {
    LOG(ERROR) << "executor()->GetInterfaces failed with error code: "
               << return_code;
    CreateErrorToSendBack(mojom::ErrorType::kSystemUtilityError,
                          "executor()->GetInterfaces failed with error code: " +
                              std::to_string(return_code));
    return;
  }
  std::string regex_result;
  std::string output = result->out;
  std::vector<std::string> lines = base::SplitString(
      output, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  bool interface_found = false;
  std::string interface_name;
  for (auto& line : lines) {
    if (RE2::FullMatch(line, kInterfaceNameRegex, &interface_name)) {
      interface_found = true;
      break;
    }
  }

  if (!interface_found) {
    CreateErrorToSendBack(mojom::ErrorType::kServiceUnavailable,
                          "No wireless adapter found on the system.");
  }

  if (wireless_info_.is_null()) {
    wireless_info_ = mojom::WirelessInterfaceInfo::New();
  }
  wireless_info_->interface_name = interface_name;

  std::string file_contents;
  wireless_info_->power_management_on = false;
  if (ReadAndTrimString(
          context_->root_dir().Append(kRelativeWirelessPowerSchemePath),
          &file_contents)) {
    uint power_scheme;
    if (!base::StringToUint(file_contents, &power_scheme)) {
      CreateErrorToSendBack(
          mojom::ErrorType::kParseError,
          "Failed to convert power scheme to integer: " + file_contents);
      return;
    }
    if ((power_scheme == 2) || (power_scheme == 3)) {
      wireless_info_->power_management_on = true;
    }
  }

  // Wireless device found. Get link information for the device.
  context_->executor()->GetLink(
      wireless_info_->interface_name,
      base::BindOnce(
          &NetworkInterfaceFetcher::HandleLinkAndExecuteIwExecuteGetInfo,
          weak_factory_.GetWeakPtr()));
}

void NetworkInterfaceFetcher::CreateResultToSendBack(void) {
  DCHECK(wireless_info_);
  std::vector<mojom::NetworkInterfaceInfoPtr> infos;
  auto info = mojom::NetworkInterfaceInfo::NewWirelessInterfaceInfo(
      std::move(wireless_info_));
  infos.push_back(std::move(info));
  SendBackResult(
      mojom::NetworkInterfaceResult::NewNetworkInterfaceInfo(std::move(infos)));
}

void NetworkInterfaceFetcher::SendBackResult(
    mojom::NetworkInterfaceResultPtr result) {
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

void NetworkInterfaceFetcher::CreateErrorToSendBack(
    mojom::ErrorType error_type, const std::string& message) {
  SendBackResult(mojom::NetworkInterfaceResult::NewError(
      CreateAndLogProbeError(error_type, message)));
}

void NetworkInterfaceFetcher::FetchWirelessInterfaceInfo(void) {
  context_->executor()->GetInterfaces(base::BindOnce(
      &NetworkInterfaceFetcher::HandleInterfaceNameAndExecuteGetLink,
      weak_factory_.GetWeakPtr()));
}

// Fetch network interface infomation.
void NetworkInterfaceFetcher::FetchNetworkInterfaceInfo(
    FetchNetworkInterfaceInfoCallback callback) {
  pending_callbacks_.push_back(std::move(callback));
  if (pending_callbacks_.size() > 1)
    return;
  FetchWirelessInterfaceInfo();
}

}  // namespace diagnostics
