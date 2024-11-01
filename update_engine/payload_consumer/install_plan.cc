// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_consumer/install_plan.h"

#include <algorithm>
#include <utility>

#include <base/format_macros.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "update_engine/common/utils.h"
#include "update_engine/payload_consumer/payload_constants.h"

using std::string;
using std::vector;

namespace chromeos_update_engine {

namespace {

constexpr char kMiniOsPartitionName[] = "minios";

string PayloadUrlsToString(
    const decltype(InstallPlan::Payload::payload_urls)& payload_urls) {
  return "(" + base::JoinString(payload_urls, ",") + ")";
}

string VectorToString(const vector<std::pair<string, string>>& input,
                      const string& separator) {
  vector<string> vec;
  std::transform(input.begin(), input.end(), std::back_inserter(vec),
                 [](const auto& pair) {
                   return base::JoinString({pair.first, pair.second}, ": ");
                 });
  return base::JoinString(vec, separator);
}

}  // namespace

string InstallPayloadTypeToString(InstallPayloadType type) {
  switch (type) {
    case InstallPayloadType::kUnknown:
      return "unknown";
    case InstallPayloadType::kFull:
      return "full";
    case InstallPayloadType::kDelta:
      return "delta";
  }
  return "invalid type";
}

bool InstallPlan::operator==(const InstallPlan& that) const {
  return ((is_resume == that.is_resume) &&
          (download_url == that.download_url) && (payloads == that.payloads) &&
          (source_slot == that.source_slot) &&
          (target_slot == that.target_slot) && (partitions == that.partitions));
}

bool InstallPlan::operator!=(const InstallPlan& that) const {
  return !((*this) == that);
}

void InstallPlan::Dump() const {
  LOG(INFO) << "InstallPlan: \n" << ToString();
}

string InstallPlan::ToString() const {
  string url_str = download_url;
  if (base::StartsWith(url_str, "fd://",
                       base::CompareCase::INSENSITIVE_ASCII)) {
    int fd = std::stoi(url_str.substr(strlen("fd://")));
    url_str = utils::GetFilePath(fd);
  }

  vector<string> result_str;
  result_str.emplace_back(VectorToString(
      {
          {"type", (is_resume ? "resume" : "new_update")},
          {"version", version},
          {"source_slot", BootControlInterface::SlotName(source_slot)},
          {"target_slot", BootControlInterface::SlotName(target_slot)},
          {"minios_target_slot",
           BootControlInterface::SlotName(minios_target_slot)},
          {"minios_source_slot",
           BootControlInterface::SlotName(minios_src_slot)},
          {"initial url", url_str},
          {"hash_checks_mandatory", utils::ToString(hash_checks_mandatory)},
          {"signature_checks_mandatory",
           utils::ToString(signature_checks_mandatory)},
          {"powerwash_required", utils::ToString(powerwash_required)},
          {"switch_slot_on_reboot", utils::ToString(switch_slot_on_reboot)},
          {"switch_minios_slot", utils::ToString(switch_minios_slot)},
          {"run_post_install", utils::ToString(run_post_install)},
          {"is_rollback", utils::ToString(is_rollback)},
          {"rollback_data_save_requested",
           utils::ToString(rollback_data_save_requested)},
          {"write_verity", utils::ToString(write_verity)},
          {"can_download_be_canceled",
           utils::ToString(can_download_be_canceled)},
      },
      "\n"));

  for (const auto& partition : partitions) {
    result_str.emplace_back(VectorToString(
        {
            {"Partition", partition.name},
            {"source_size", base::NumberToString(partition.source_size)},
            {"source_path", partition.source_path},
            {"source_hash", base::HexEncode(partition.source_hash.data(),
                                            partition.source_hash.size())},
            {"target_size", base::NumberToString(partition.target_size)},
            {"target_path", partition.target_path},
            {"target_hash", base::HexEncode(partition.target_hash.data(),
                                            partition.target_hash.size())},
            {"run_postinstall", utils::ToString(partition.run_postinstall)},
            {"postinstall_path", partition.postinstall_path},
            {"filesystem_type", partition.filesystem_type},
        },
        "\n  "));
  }

  for (unsigned int i = 0; i < payloads.size(); ++i) {
    const auto& payload = payloads[i];
    result_str.emplace_back(VectorToString(
        {
            {"Payload", base::NumberToString(i)},
            {"urls", PayloadUrlsToString(payload.payload_urls)},
            {"size", base::NumberToString(payload.size)},
            {"metadata_size", base::NumberToString(payload.metadata_size)},
            {"metadata_signature", payload.metadata_signature},
            {"hash", base::HexEncode(payload.hash.data(), payload.hash.size())},
            {"type", InstallPayloadTypeToString(payload.type)},
            {"fingerprint", payload.fp},
            {"app_id", payload.app_id},
            {"already_applied", utils::ToString(payload.already_applied)},
        },
        "\n  "));
  }

  return base::JoinString(result_str, "\n");
}

bool InstallPlan::LoadPartitionsFromSlots(BootControlInterface* boot_control) {
  bool result = true;
  for (Partition& partition : partitions) {
    auto current_target_slot = target_slot;
    auto current_source_slot = source_slot;

    if (base::ToLowerASCII(partition.name) == kMiniOsPartitionName) {
      current_target_slot = minios_target_slot;
      current_source_slot = minios_src_slot;
    }

    if (current_source_slot != BootControlInterface::kInvalidSlot &&
        partition.source_size > 0) {
      result =
          boot_control->GetPartitionDevice(partition.name, current_source_slot,
                                           &partition.source_path) &&
          result;
    } else {
      partition.source_path.clear();
    }

    if (current_target_slot != BootControlInterface::kInvalidSlot &&
        partition.target_size > 0) {
      result =
          boot_control->GetPartitionDevice(partition.name, current_target_slot,
                                           &partition.target_path) &&
          result;

    } else {
      partition.target_path.clear();
    }
  }
  return result;
}

bool InstallPlan::Partition::operator==(
    const InstallPlan::Partition& that) const {
  return (name == that.name && source_path == that.source_path &&
          source_size == that.source_size && source_hash == that.source_hash &&
          target_path == that.target_path && target_size == that.target_size &&
          target_hash == that.target_hash &&
          run_postinstall == that.run_postinstall &&
          postinstall_path == that.postinstall_path &&
          filesystem_type == that.filesystem_type &&
          postinstall_optional == that.postinstall_optional);
}

}  // namespace chromeos_update_engine
