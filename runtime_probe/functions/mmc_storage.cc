// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/mmc_storage.h"

#include <pcrecpp.h>

#include <optional>
#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <brillo/errors/error.h>
#include <brillo/strings/string_utils.h>
#include <debugd/dbus-proxies.h>

#include "runtime_probe/system/context.h"
#include "runtime_probe/utils/bus_utils.h"
#include "runtime_probe/utils/file_utils.h"
#include "runtime_probe/utils/value_utils.h"

namespace runtime_probe {
namespace {
// D-Bus related constant to issue dbus call to debugd.
constexpr auto kDebugdMmcOption = "extcsd_read";
constexpr auto kDebugdMmcDefaultTimeout = 10 * 1000;  // in ms

constexpr auto kMmcFWVersionByteCount = 8;

const std::vector<std::string> kMmcFields{"name", "oemid", "manfid"};
// Attributes in optional fields:
// prv: SD and MMCv4 only
// hwrev: SD and MMCv1 only
const std::vector<std::string> kMmcOptionalFields{"hwrev", "prv", "serial"};

// Check if the string represented by |input_string| is printable.
bool IsPrintable(const std::string& input_string) {
  for (const auto& cha : input_string) {
    if (!isprint(cha))
      return false;
  }
  return true;
}

// Return the formatted string "%s (%s)" % |v|, |v_decode|.
inline std::string VersionFormattedString(const std::string& v,
                                          const std::string& v_decode) {
  return v + " (" + v_decode + ")";
}

// Extracts the eMMC 5.0 firmware version of storage device specified
// by |node_path| from EXT_CSD[254:262] via D-Bus call to debugd MMC method.
std::string GetStorageFwVersion() {
  VLOG(2) << "Checking eMMC firmware version...";

  std::string ext_csd_res;
  brillo::ErrorPtr err;

  auto debugd = Context::Get()->debugd_proxy();
  VLOG(2) << "Issuing D-Bus call to debugd to retrieve eMMC 5.0 firmware info.";
  if (!debugd->Mmc(kDebugdMmcOption, &ext_csd_res, &err,
                   kDebugdMmcDefaultTimeout)) {
    std::string err_message = "(no error message)";
    if (err)
      err_message = err->GetMessage();
    LOG(ERROR) << "Failed to get mmc extcsd results by D-Bus call to debugd. "
                  "Error message: "
               << err_message;
    return "";
  }

  // The output of firmware version looks like hexdump of ASCII strings or
  // hexadecimal values, which depends on vendors.

  // Example of version "ABCDEFGH" (ASCII hexdump)
  // [FIRMWARE_VERSION[261]]: 0x48
  // [FIRMWARE_VERSION[260]]: 0x47
  // [FIRMWARE_VERSION[259]]: 0x46
  // [FIRMWARE_VERSION[258]]: 0x45
  // [FIRMWARE_VERSION[257]]: 0x44
  // [FIRMWARE_VERSION[256]]: 0x43
  // [FIRMWARE_VERSION[255]]: 0x42
  // [FIRMWARE_VERSION[254]]: 0x41

  // Example of version 3 (hexadecimal values hexdump)
  // [FIRMWARE_VERSION[261]]: 0x00
  // [FIRMWARE_VERSION[260]]: 0x00
  // [FIRMWARE_VERSION[259]]: 0x00
  // [FIRMWARE_VERSION[258]]: 0x00
  // [FIRMWARE_VERSION[257]]: 0x00
  // [FIRMWARE_VERSION[256]]: 0x00
  // [FIRMWARE_VERSION[255]]: 0x00
  // [FIRMWARE_VERSION[254]]: 0x03

  pcrecpp::RE re(R"(^\[FIRMWARE_VERSION\[\d+\]\]: (.*)$)",
                 pcrecpp::RE_Options());

  const auto ext_csd_lines = base::SplitString(
      ext_csd_res, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // hex_version_components store each bytes as two-chars string of format "ff".
  // char_version is a string where each byte is stored as char in each
  // character.
  std::vector<std::string> hex_version_components;
  std::string char_version{""};

  // The memory snapshots of version output from mmc are in reverse order.
  for (auto it = ext_csd_lines.rbegin(); it != ext_csd_lines.rend(); it++) {
    std::string cur_version_str;
    if (!re.PartialMatch(*it, &cur_version_str))
      continue;

    // "0xff" => "ff"
    const auto cur_version_component =
        cur_version_str.substr(2, std::string::npos);

    hex_version_components.push_back(cur_version_component);

    int32_t cur_version_char;
    if (!base::HexStringToInt(cur_version_str, &cur_version_char)) {
      LOG(ERROR) << "Failed to convert one byte hex representation "
                 << cur_version_str << " to char.";
      return "";
    }
    char_version += static_cast<char>(cur_version_char);
  }

  if (hex_version_components.size() != kMmcFWVersionByteCount) {
    LOG(WARNING)
        << "Failed to parse firmware version from mmc extcsd read correctly.";
    return "";
  }

  const auto hex_version = brillo::string_utils::JoinRange(
      "", hex_version_components.begin(), hex_version_components.end());
  VLOG(2) << "eMMC 5.0 firmware version is " << hex_version;
  if (IsPrintable(char_version)) {
    return VersionFormattedString(hex_version, char_version);

  } else {
    // Represent the version in the little endian format.
    const std::string hex_version_le = brillo::string_utils::JoinRange(
        "", hex_version_components.rbegin(), hex_version_components.rend());
    uint64_t version_decode_le;
    if (!base::HexStringToUInt64(hex_version_le, &version_decode_le)) {
      LOG(ERROR) << "Failed to convert " << hex_version_le
                 << " to 64-bit unsigned integer";
      return "";
    }
    return VersionFormattedString(hex_version,
                                  std::to_string(version_decode_le));
  }
}

bool CheckStorageTypeMatch(const base::FilePath& mmc_path) {
  VLOG(2) << "Checking if storage \"" << mmc_path << "\" is eMMC.";

  if (!base::PathExists(mmc_path)) {
    LOG(ERROR) << "The link " << mmc_path
               << " to the mmc subsystem doesn't exist.";
    return false;
  }

  std::string type;
  if (!ReadAndTrimFileToString(mmc_path.Append("type"), type)) {
    VLOG(2) << "Failed to read mmc type from \"" << mmc_path.Append("type");
    return false;
  }
  if (type != "MMC") {
    VLOG(2) << "Mmc type of " << mmc_path << " is \"" << type
            << "\", not \"MMC\" (which means eMMC).";
    return false;
  }

  VLOG(2) << "\"" << mmc_path << "\" is an eMMC.";
  return true;
}

}  // namespace

std::optional<base::Value> MmcStorageFunction::ProbeFromSysfs(
    const base::FilePath& node_path) const {
  CHECK(!node_path.empty());
  VLOG(2) << "Processing the node \"" << node_path << "\"";

  const auto mmc_path = node_path.Append("device");
  if (!CheckStorageTypeMatch(mmc_path)) {
    return std::nullopt;
  }

  auto mmc_res = MapFilesToDict(mmc_path, kMmcFields, kMmcOptionalFields);
  if (!mmc_res) {
    VLOG(1) << "eMMC-specific fields do not exist on storage \"" << node_path
            << "\"";
    return std::nullopt;
  }
  PrependToDVKey(&*mmc_res, "mmc_");

  // We assume that all mmc subsystem is under a mmc_host subsystem. Find the
  // mmc_host by checking the parent dir of the realpath of mmc.
  base::FilePath mmc_host_path =
      base::MakeAbsoluteFilePath(mmc_path.Append(".."));
  auto mmc_host_res = GetDeviceBusDataFromSysfsNode(mmc_host_path);
  if (mmc_host_res) {
    PrependToDVKey(&*mmc_host_res, "mmc_host_");
    mmc_res->GetDict().Merge(std::move(mmc_host_res->GetDict()));
  } else {
    // Setting a bus type field help us prevent this components from matching
    // the one with mmc_host in the future.
    mmc_res->GetDict().Set("mmc_host_bus_type", "uninterested");
  }

  // The type used to be "MMC". Don't set it to "MMC" again to prevent old mmc
  // components without the host fields matching the new components.
  mmc_res->GetDict().Set("type", "MMC_ASSEMBLY");
  return mmc_res;
}

std::optional<base::Value> MmcStorageFunction::ProbeFromStorageTool(
    const base::FilePath& node_path) const {
  base::Value result(base::Value::Type::DICT);
  auto storage_fw_version = GetStorageFwVersion();
  if (!storage_fw_version.empty())
    result.GetDict().Set("storage_fw_version", storage_fw_version);
  return result;
}

}  // namespace runtime_probe
