// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/pinweaver.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/sys_byteorder.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <trunks/tpm_utility.h>

#define __packed __attribute((packed))
#define __aligned(x) __attribute((aligned(x)))
#include <pinweaver/pinweaver_types.h>

#include "libhwsec/backend/tpm2/backend.h"
#include "libhwsec/error/tpm2_error.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

using ErrorCode = PinWeaverTpm2::CredentialTreeResult::ErrorCode;
using LogEntry = Backend::PinWeaver::GetLogResult::LogEntry;
using LogEntryType = Backend::PinWeaver::GetLogResult::LogEntryType;

namespace {

constexpr uint8_t kPinWeaverProtocolVersion = 1;

StatusOr<std::string> EncodeAuxHashes(const std::vector<brillo::Blob>& h_aux) {
  std::string result;
  result.reserve(h_aux.size() * PW_HASH_SIZE);
  for (const brillo::Blob& hash : h_aux) {
    if (PW_HASH_SIZE != hash.size()) {
      return MakeStatus<TPMError>("Mismatch AUX hash length",
                                  TPMRetryAction::kNoRetry);
    }
    result.append(hash.begin(), hash.end());
  }
  return result;
}

ErrorCode ConvertPWStatus(uint32_t pinweaver_status) {
  if (pinweaver_status != 0 /* EC_SUCCESS */) {
    LOG(WARNING) << "Pinweaver status: " << pinweaver_status;
  }

  switch (pinweaver_status) {
    case 0:  // EC_SUCCESS
      return ErrorCode::kSuccess;
    case PW_ERR_LOWENT_AUTH_FAILED:
      return ErrorCode::kInvalidLeSecret;
    case PW_ERR_RESET_AUTH_FAILED:
      return ErrorCode::kInvalidResetSecret;
    case PW_ERR_RATE_LIMIT_REACHED:
      return ErrorCode::kTooManyAttempts;
    case PW_ERR_PATH_AUTH_FAILED:
      return ErrorCode::kHashTreeOutOfSync;
    // This could happen (by design) only if the device is hacked. Treat the
    // error as like an invalid PIN was provided.
    case PW_ERR_PCR_NOT_MATCH:
      return ErrorCode::kPolicyNotMatch;
  }

  return ErrorCode::kUnknown;
}

Status ErrorCodeToStatus(ErrorCode err) {
  if (err == ErrorCode::kSuccess) {
    return OkStatus();
  }
  return MakeStatus<TPMError>(
      base::StringPrintf("PinWeaver error 0x%x",
                         static_cast<unsigned int>(err)),
      TPMRetryAction::kNoRetry);
}

std::vector<LogEntry> ConvertPinWeaverLog(
    const std::vector<trunks::PinWeaverLogEntry>& log) {
  std::vector<LogEntry> result;
  for (const auto& log_entry : log) {
    LogEntry entry;
    if (log_entry.has_insert_leaf()) {
      entry.type = LogEntryType::kInsert;
      entry.mac = brillo::BlobFromString(log_entry.insert_leaf().hmac());
    } else if (log_entry.has_remove_leaf()) {
      entry.type = LogEntryType::kRemove;
    } else if (log_entry.has_auth()) {
      entry.type = LogEntryType::kCheck;
    } else if (log_entry.has_reset_tree()) {
      entry.type = LogEntryType::kReset;
    } else {
      entry.type = LogEntryType::kInvalid;
    }
    entry.root = brillo::BlobFromString(log_entry.root());
    entry.label = log_entry.label();

    result.push_back(entry);
  }
  return result;
}

std::optional<brillo::Blob> BlobOrNullopt(const std::string& str) {
  if (str.empty())
    return std::nullopt;
  return brillo::BlobFromString(str);
}

}  // namespace

StatusOr<bool> PinWeaverTpm2::IsEnabled() {
  return GetVersion().ok();
}

StatusOr<uint8_t> PinWeaverTpm2::GetVersion() {
  if (protocol_version_.has_value()) {
    return protocol_version_.value();
  }

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  uint8_t version = 255;

  auto status = MakeStatus<TPM2Error>(context.tpm_utility->PinWeaverIsSupported(
      kPinWeaverProtocolVersion, &version));

  if (!status.ok()) {
    if (status->ErrorCode() != trunks::SAPI_RC_ABI_MISMATCH) {
      return MakeStatus<TPMError>("Failed to check pinweaver support")
          .Wrap(std::move(status));
    }
    status = MakeStatus<TPM2Error>(
        context.tpm_utility->PinWeaverIsSupported(0, &version));
  }

  if (!status.ok()) {
    return MakeStatus<TPMError>("Failed to check pinweaver support")
        .Wrap(std::move(status));
  }

  protocol_version_ =
      std::min(version, static_cast<uint8_t>(kPinWeaverProtocolVersion));
  return protocol_version_.value();
}

StatusOr<PinWeaverTpm2::CredentialTreeResult> PinWeaverTpm2::Reset(
    uint32_t bits_per_level, uint32_t length_labels) {
  ASSIGN_OR_RETURN(uint8_t version, GetVersion());

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  uint32_t pinweaver_status = 0;
  std::string root;

  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context.tpm_utility->PinWeaverResetTree(
                      version, bits_per_level, length_labels / bits_per_level,
                      &pinweaver_status, &root)))
      .WithStatus<TPMError>("Failed to reset tree in pinweaver");

  RETURN_IF_ERROR(ErrorCodeToStatus(ConvertPWStatus(pinweaver_status)));

  return CredentialTreeResult{
      .error = ErrorCode::kSuccess,
      .new_root = brillo::BlobFromString(root),
  };
}

StatusOr<PinWeaverTpm2::CredentialTreeResult> PinWeaverTpm2::InsertCredential(
    const std::vector<OperationPolicySetting>& policies,
    const uint64_t label,
    const std::vector<brillo::Blob>& h_aux,
    const brillo::SecureBlob& le_secret,
    const brillo::SecureBlob& he_secret,
    const brillo::SecureBlob& reset_secret,
    const DelaySchedule& delay_schedule) {
  ASSIGN_OR_RETURN(uint8_t version, GetVersion());
  ASSIGN_OR_RETURN(std::string encoded_aux, EncodeAuxHashes(h_aux));

  std::vector<ConfigTpm2::PcrValue> pcr_values;

  for (const OperationPolicySetting& policy : policies) {
    if (policy.permission.auth_value.has_value()) {
      return MakeStatus<TPMError>("Unsupported policy",
                                  TPMRetryAction::kNoRetry);
    }

    ASSIGN_OR_RETURN(
        const ConfigTpm2::PcrValue& pcr_value,
        backend_.GetConfigTpm2().ToPcrValue(policy.device_config_settings),
        _.WithStatus<TPMError>("Failed to convert setting to PCR value"));

    pcr_values.push_back(pcr_value);
  }

  if (version == 0 && !pcr_values.empty()) {
    return MakeStatus<TPMError>("PinWeaver Version 0 doesn't support PCR",
                                TPMRetryAction::kNoRetry);
  }

  trunks::ValidPcrCriteria pcr_criteria;
  for (const ConfigTpm2::PcrValue& pcr_value : pcr_values) {
    trunks::ValidPcrValue* new_value = pcr_criteria.add_valid_pcr_values();
    new_value->set_bitmask(&pcr_value.bitmask, 2);
    new_value->set_digest(pcr_value.digest);
  }

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  uint32_t pinweaver_status = 0;
  std::string root;
  std::string cred_metadata_string;
  std::string mac_string;

  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context.tpm_utility->PinWeaverInsertLeaf(
          version, label, encoded_aux, le_secret, he_secret, reset_secret,
          delay_schedule, pcr_criteria, &pinweaver_status, &root,
          &cred_metadata_string, &mac_string)))
      .WithStatus<TPMError>("Failed to insert leaf in pinweaver");

  RETURN_IF_ERROR(ErrorCodeToStatus(ConvertPWStatus(pinweaver_status)));

  return CredentialTreeResult{
      .error = ErrorCode::kSuccess,
      .new_root = brillo::BlobFromString(root),
      .new_cred_metadata = BlobOrNullopt(cred_metadata_string),
      .new_mac = BlobOrNullopt(mac_string),
  };
}

StatusOr<PinWeaverTpm2::CredentialTreeResult> PinWeaverTpm2::CheckCredential(
    const uint64_t label,
    const std::vector<brillo::Blob>& h_aux,
    const brillo::Blob& orig_cred_metadata,
    const brillo::SecureBlob& le_secret) {
  ASSIGN_OR_RETURN(uint8_t version, GetVersion());
  ASSIGN_OR_RETURN(std::string encoded_aux, EncodeAuxHashes(h_aux));

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  uint32_t pinweaver_status = 0;
  std::string root;
  uint32_t seconds_to_wait = 0;
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  std::string cred_metadata_string;
  std::string mac_string;

  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context.tpm_utility->PinWeaverTryAuth(
                      version, le_secret, encoded_aux,
                      brillo::BlobToString(orig_cred_metadata),
                      &pinweaver_status, &root, &seconds_to_wait, &he_secret,
                      &reset_secret, &cred_metadata_string, &mac_string)))
      .WithStatus<TPMError>("Failed to try auth in pinweaver");

  return CredentialTreeResult{
      .error = ConvertPWStatus(pinweaver_status),
      .new_root = brillo::BlobFromString(root),
      .new_cred_metadata = BlobOrNullopt(cred_metadata_string),
      .new_mac = BlobOrNullopt(mac_string),
      .he_secret = std::move(he_secret),
      .reset_secret = std::move(reset_secret),
  };
}

StatusOr<PinWeaverTpm2::CredentialTreeResult> PinWeaverTpm2::RemoveCredential(
    const uint64_t label,
    const std::vector<std::vector<uint8_t>>& h_aux,
    const std::vector<uint8_t>& mac) {
  ASSIGN_OR_RETURN(uint8_t version, GetVersion());
  ASSIGN_OR_RETURN(std::string encoded_aux, EncodeAuxHashes(h_aux));

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  uint32_t pinweaver_status = 0;
  std::string root;

  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context.tpm_utility->PinWeaverRemoveLeaf(
          version, label, encoded_aux, brillo::BlobToString(mac),
          &pinweaver_status, &root)))
      .WithStatus<TPMError>("Failed to remove leaf in pinweaver");

  RETURN_IF_ERROR(ErrorCodeToStatus(ConvertPWStatus(pinweaver_status)));

  return CredentialTreeResult{
      .error = ErrorCode::kSuccess,
      .new_root = brillo::BlobFromString(root),
  };
}

StatusOr<PinWeaverTpm2::CredentialTreeResult> PinWeaverTpm2::ResetCredential(
    const uint64_t label,
    const std::vector<std::vector<uint8_t>>& h_aux,
    const std::vector<uint8_t>& orig_cred_metadata,
    const brillo::SecureBlob& reset_secret) {
  ASSIGN_OR_RETURN(uint8_t version, GetVersion());
  ASSIGN_OR_RETURN(std::string encoded_aux, EncodeAuxHashes(h_aux));

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  uint32_t pinweaver_status = 0;
  std::string root;
  std::string cred_metadata_string;
  std::string mac_string;

  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context.tpm_utility->PinWeaverResetAuth(
          version, reset_secret, encoded_aux,
          brillo::BlobToString(orig_cred_metadata), &pinweaver_status, &root,
          &cred_metadata_string, &mac_string)))
      .WithStatus<TPMError>("Failed to reset auth in pinweaver");

  return CredentialTreeResult{
      .error = ConvertPWStatus(pinweaver_status),
      .new_root = brillo::BlobFromString(root),
      .new_cred_metadata = BlobOrNullopt(cred_metadata_string),
      .new_mac = BlobOrNullopt(mac_string),
  };
}

StatusOr<PinWeaverTpm2::GetLogResult> PinWeaverTpm2::GetLog(
    const brillo::Blob& cur_disk_root_hash) {
  ASSIGN_OR_RETURN(uint8_t version, GetVersion());

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  uint32_t pinweaver_status = 0;
  std::string root;
  std::vector<trunks::PinWeaverLogEntry> log_ret;

  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context.tpm_utility->PinWeaverGetLog(
                      version, brillo::BlobToString(cur_disk_root_hash),
                      &pinweaver_status, &root, &log_ret)))
      .WithStatus<TPMError>("Failed to get pinweaver log");

  RETURN_IF_ERROR(ErrorCodeToStatus(ConvertPWStatus(pinweaver_status)));

  return GetLogResult{
      .root_hash = brillo::BlobFromString(root),
      .log_entries = ConvertPinWeaverLog(log_ret),
  };
}

StatusOr<PinWeaverTpm2::ReplayLogOperationResult>
PinWeaverTpm2::ReplayLogOperation(const brillo::Blob& log_entry_root,
                                  const std::vector<brillo::Blob>& h_aux,
                                  const brillo::Blob& orig_cred_metadata) {
  ASSIGN_OR_RETURN(uint8_t version, GetVersion());
  ASSIGN_OR_RETURN(std::string encoded_aux, EncodeAuxHashes(h_aux));

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  uint32_t pinweaver_status = 0;
  std::string root;
  std::string cred_metadata_string;
  std::string mac_string;

  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context.tpm_utility->PinWeaverLogReplay(
          version, brillo::BlobToString(log_entry_root), encoded_aux,
          brillo::BlobToString(orig_cred_metadata), &pinweaver_status, &root,
          &cred_metadata_string, &mac_string)))
      .WithStatus<TPMError>("Failed to replay log in pinweaver");

  RETURN_IF_ERROR(ErrorCodeToStatus(ConvertPWStatus(pinweaver_status)));

  return ReplayLogOperationResult{
      .new_cred_metadata = brillo::BlobFromString(cred_metadata_string),
      .new_mac = brillo::BlobFromString(mac_string),
  };
}

StatusOr<int> PinWeaverTpm2::GetWrongAuthAttempts(
    const brillo::Blob& cred_metadata) {
  // The assumption is that leaf_public_data_t structure will have the existing
  // part immutable in the future.
  if (cred_metadata.size() <
      offsetof(unimported_leaf_data_t, payload) +
          offsetof(leaf_public_data_t, attempt_count) +
          sizeof(std::declval<leaf_public_data_t>().attempt_count)) {
    return MakeStatus<TPMError>("GetWrongAuthAttempts metadata too short",
                                TPMRetryAction::kNoRetry);
  }

  // The below should equal to this:
  //
  // reinterpret_cast<struct leaf_public_data_t*>(
  //   reinterpret_cast<struct unimported_leaf_data_t*>(
  //     cred_metadata.data()
  //   )->payload
  // )->attempt_count.v;
  //
  // But we should use memcpy to prevent misaligned accesses and endian issue.

  uint32_t count;

  static_assert(sizeof(std::declval<attempt_count_t>().v) == sizeof(count));

  const uint8_t* ptr = cred_metadata.data() +
                       offsetof(unimported_leaf_data_t, payload) +
                       offsetof(leaf_public_data_t, attempt_count) +
                       offsetof(attempt_count_t, v);

  memcpy(&count, ptr, sizeof(count));

  return base::ByteSwapToLE32(count);
}

StatusOr<PinWeaverTpm2::DelaySchedule> PinWeaverTpm2::GetDelaySchedule(
    const brillo::Blob& cred_metadata) {
  // The assumption is that leaf_public_data_t structure will have the existing
  // part immutable in the future.
  if (cred_metadata.size() <
      offsetof(unimported_leaf_data_t, payload) +
          offsetof(leaf_public_data_t, delay_schedule) +
          sizeof(std::declval<leaf_public_data_t>().delay_schedule)) {
    return MakeStatus<TPMError>("GetDelaySchedule metadata too short",
                                TPMRetryAction::kNoRetry);
  }

  // The below should equal to this:
  //
  // reinterpret_cast<struct delay_schedule_entry_t*>(
  //   reinterpret_cast<struct unimported_leaf_data_t*>(
  //     cred_metadata.data()
  //   )->payload
  // )->delay_schedule;
  //
  // But we should use memcpy to prevent misaligned accesses and endian issue.

  DelaySchedule delay_schedule;

  static_assert(sizeof(std::declval<leaf_public_data_t>().delay_schedule) ==
                sizeof(delay_schedule_entry_t) * PW_SCHED_COUNT);

  for (size_t i = 0; i < PW_SCHED_COUNT; i++) {
    uint32_t attempt_count = 0;
    uint32_t time_diff = 0;

    static_assert(sizeof(std::declval<attempt_count_t>().v) ==
                  sizeof(attempt_count));
    static_assert(sizeof(std::declval<time_diff_t>().v) == sizeof(time_diff));

    const uint8_t* entry_ptr = cred_metadata.data() +
                               offsetof(unimported_leaf_data_t, payload) +
                               offsetof(leaf_public_data_t, delay_schedule) +
                               sizeof(delay_schedule_entry_t) * i;

    const uint8_t* attempt_count_ptr =
        entry_ptr + offsetof(delay_schedule_entry_t, attempt_count) +
        offsetof(attempt_count_t, v);

    memcpy(&attempt_count, attempt_count_ptr, sizeof(attempt_count));
    attempt_count = base::ByteSwapToLE32(attempt_count);

    const uint8_t* time_diff_ptr = entry_ptr +
                                   offsetof(delay_schedule_entry_t, time_diff) +
                                   offsetof(time_diff_t, v);

    memcpy(&time_diff, time_diff_ptr, sizeof(time_diff));
    time_diff = base::ByteSwapToLE32(time_diff);

    if (attempt_count == 0 && time_diff == 0) {
      break;
    }

    delay_schedule[attempt_count] = time_diff;
  }

  return delay_schedule;
}

StatusOr<uint32_t> PinWeaverTpm2::GetDelayInSeconds(
    const brillo::Blob& cred_metadata) {
  ASSIGN_OR_RETURN(DelaySchedule delay_schedule,
                   GetDelaySchedule(cred_metadata));
  ASSIGN_OR_RETURN(int wrong_attempts, GetWrongAuthAttempts(cred_metadata));

  // The format for a delay schedule entry is as follows:
  // (number_of_incorrect_attempts, delay_before_next_attempt)

  // Find the matching delay from delay_schedule.
  // We want to find the field that is the last one less or equal to
  // wrong_attempts. Use upper bound to find the first field that is greater
  // than wrong_attempts, and the previous one of it is the field we want.
  auto iter = delay_schedule.upper_bound(wrong_attempts);
  if (iter == delay_schedule.begin()) {
    // Zero delay for this case.
    return 0;
  }
  --iter;

  // TODO(b/234715681): Calculate the more accurate delay if we need it in the
  // future.
  return iter->second;
}

}  // namespace hwsec
