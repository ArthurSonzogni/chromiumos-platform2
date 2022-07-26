// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm1/storage.h"

#include <bitset>
#include <cstdint>
#include <optional>
#include <utility>

#include <absl/container/flat_hash_set.h>
#include <base/strings/stringprintf.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "libhwsec/backend/tpm1/backend.h"
#include "libhwsec/error/tpm_manager_error.h"
#include "libhwsec/error/tpm_nvram_error.h"
#include "libhwsec/structures/no_default_init.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

namespace {

constexpr uint32_t kFwmpIndex = 0x100a;
constexpr uint32_t kInstallAttributesIndex = 0x20000004;
constexpr uint32_t kBootlockboxIndex = 0x20000006;

using Attributes = std::bitset<tpm_manager::NvramSpaceAttribute_ARRAYSIZE>;

struct SpaceInfo {
  NoDefault<uint32_t> index;
  NoDefault<bool> write_with_owner_auth;
  NoDefault<bool> read_with_owner_auth;
  NoDefault<bool> lock_after_write;
  NoDefault<bool> prepare_if_write_locked;
  std::optional<Attributes> init_attributes;
  Attributes require_attributes;
  Attributes deny_attributes;
  bool bind_to_prc0 = false;
  std::optional<const char*> owner_dependency;
};

// Note: These bitset initialization steps would not work if we have more than
// 64 kind of attributes.
constexpr Attributes kFwmpInitAttributes =
    (1ULL << tpm_manager::NVRAM_PERSISTENT_WRITE_LOCK) |
    (1ULL << tpm_manager::NVRAM_PLATFORM_READ);

constexpr Attributes kFwmpRequireAttributes =
    (1ULL << tpm_manager::NVRAM_PERSISTENT_WRITE_LOCK) |
    (1ULL << tpm_manager::NVRAM_PLATFORM_READ);

constexpr Attributes kInstallAttributesRequireAttributes =
    (1ULL << tpm_manager::NVRAM_PERSISTENT_WRITE_LOCK);

constexpr Attributes kInstallAttributesInitAttributes =
    (1ULL << tpm_manager::NVRAM_PERSISTENT_WRITE_LOCK);

constexpr Attributes kBootlockboxInitAttributes =
    (1ULL << tpm_manager::NVRAM_READ_AUTHORIZATION) |
    (1ULL << tpm_manager::NVRAM_BOOT_WRITE_LOCK) |
    (1ULL << tpm_manager::NVRAM_WRITE_AUTHORIZATION);

bool CheckAttributes(const Attributes& require_attributes,
                     const Attributes& deny_attributes,
                     const Attributes& attributes) {
  if ((attributes & require_attributes) != require_attributes) {
    return false;
  }

  if ((attributes & deny_attributes).any()) {
    return false;
  }

  return true;
}

StatusOr<SpaceInfo> GetSpaceInfo(Space space) {
  switch (space) {
    case Space::kFirmwareManagementParameters:
      return SpaceInfo{
          .index = kFwmpIndex,
          .write_with_owner_auth = false,
          .read_with_owner_auth = false,
          .lock_after_write = true,
          .prepare_if_write_locked = true,
          .init_attributes = kFwmpInitAttributes,
          .require_attributes = kFwmpRequireAttributes,
      };
    case Space::kInstallAttributes:
      return SpaceInfo{
          .index = kInstallAttributesIndex,
          .write_with_owner_auth = false,
          .read_with_owner_auth = false,
          .lock_after_write = true,
          .prepare_if_write_locked = true,
          .init_attributes = kInstallAttributesInitAttributes,
          .require_attributes = kInstallAttributesRequireAttributes,
          .bind_to_prc0 = true,
          .owner_dependency = tpm_manager::kTpmOwnerDependency_Nvram,
      };
    case Space::kBootlockbox:
      return SpaceInfo{
          .index = kBootlockboxIndex,
          .write_with_owner_auth = false,
          .read_with_owner_auth = false,
          .lock_after_write = false,
          .prepare_if_write_locked = false,
          .init_attributes = kBootlockboxInitAttributes,
          .owner_dependency = tpm_manager::kTpmOwnerDependency_Bootlockbox,
      };
    default:
      return MakeStatus<TPMError>("Unknown space", TPMRetryAction::kNoRetry);
  }
}

StatusOr<absl::flat_hash_set<uint32_t>> List(
    org::chromium::TpmNvramProxyInterface& tpm_nvram) {
  absl::flat_hash_set<uint32_t> result;

  tpm_manager::ListSpacesRequest request;
  tpm_manager::ListSpacesReply reply;

  if (brillo::ErrorPtr err; !tpm_nvram.ListSpaces(
          request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  RETURN_IF_ERROR(MakeStatus<TPMNvramError>(reply.result()));

  for (int i = 0; i < reply.index_list_size(); ++i) {
    result.insert(reply.index_list(i));
  }

  return result;
}

Status CheckAndRemoveDependency(
    org::chromium::TpmManagerProxyInterface& tpm_manager,
    const SpaceInfo& space_info) {
  if (space_info.owner_dependency.has_value()) {
    tpm_manager::RemoveOwnerDependencyRequest request;
    request.set_owner_dependency(space_info.owner_dependency.value());
    tpm_manager::RemoveOwnerDependencyReply reply;

    if (brillo::ErrorPtr err; !tpm_manager.RemoveOwnerDependency(
            request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
      return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
          .Wrap(std::move(err));
    }

    RETURN_IF_ERROR(MakeStatus<TPMManagerError>(reply.status()));
  }

  return OkStatus();
}

StatusOr<bool> HasOwnerPassword(
    org::chromium::TpmManagerProxyInterface& tpm_manager) {
  tpm_manager::GetTpmNonsensitiveStatusRequest status_request;
  tpm_manager::GetTpmNonsensitiveStatusReply status_reply;

  if (brillo::ErrorPtr err; !tpm_manager.GetTpmNonsensitiveStatus(
          status_request, &status_reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  RETURN_IF_ERROR(MakeStatus<TPMManagerError>(status_reply.status()));

  if (!status_reply.is_owned()) {
    return MakeStatus<TPMError>("TPM is not owned", TPMRetryAction::kLater);
  }

  if (!status_reply.is_owner_password_present()) {
    return false;
  }

  return true;
}

struct DetailSpaceInfo {
  uint32_t size = 0;
  bool is_read_locked = false;
  bool is_write_locked = false;
  Attributes attributes;
  bool bind_to_prc0 = false;
};

StatusOr<DetailSpaceInfo> GetDetailSpaceInfo(
    org::chromium::TpmNvramProxyInterface& tpm_nvram,
    const SpaceInfo& space_info) {
  DetailSpaceInfo result;

  tpm_manager::GetSpaceInfoRequest request;
  request.set_index(space_info.index);
  tpm_manager::GetSpaceInfoReply reply;

  if (brillo::ErrorPtr err; !tpm_nvram.GetSpaceInfo(
          request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  RETURN_IF_ERROR(MakeStatus<TPMNvramError>(reply.result()));

  result.size = reply.size();
  result.is_read_locked = reply.is_read_locked();
  result.is_write_locked = reply.is_write_locked();
  for (int i = 0; i < reply.attributes().size(); ++i) {
    result.attributes[reply.attributes(i)] = true;
  }
  result.bind_to_prc0 = (reply.policy() == tpm_manager::NVRAM_POLICY_PCR0);
  return result;
}

}  // namespace

StatusOr<StorageTpm1::ReadyState> StorageTpm1::IsReady(Space space) {
  // TODO(b/229524745): Add cache for this function.
  ASSIGN_OR_RETURN(const absl::flat_hash_set<uint32_t>& space_list,
                   List(backend_.GetProxy().GetTpmNvram()),
                   _.WithStatus<TPMError>("Failed to list space"));

  ASSIGN_OR_RETURN(const SpaceInfo& space_info, GetSpaceInfo(space));

  DetailSpaceInfo detail_info;
  bool ready = false;
  if (space_list.find(space_info.index) != space_list.end()) {
    ASSIGN_OR_RETURN(
        detail_info,
        GetDetailSpaceInfo(backend_.GetProxy().GetTpmNvram(), space_info),
        _.WithStatus<TPMError>("Failed to get detail space info"));

    ready = CheckAttributes(space_info.require_attributes,
                            space_info.deny_attributes, detail_info.attributes);
  }

  if (!ready) {
    if (!space_info.init_attributes.has_value()) {
      return MakeStatus<TPMError>("This space is not preparable",
                                  TPMRetryAction::kNoRetry);
    }

    ASSIGN_OR_RETURN(
        bool has_owner_pass,
        HasOwnerPassword(backend_.GetProxy().GetTpmManager()),
        _.WithStatus<TPMError>("Failed to get owner password status"));

    if (!has_owner_pass) {
      return MakeStatus<TPMError>("No owner password",
                                  TPMRetryAction::kNoRetry);
    }

    return ReadyState::kPreparable;
  }

  if (detail_info.is_write_locked) {
    // We don't need to remove the dependency for locked space.
    return ReadyState::kWriteLocked;
  }

  RETURN_IF_ERROR(
      CheckAndRemoveDependency(backend_.GetProxy().GetTpmManager(), space_info))
      .WithStatus<TPMError>("Failed to check and remove dependency");
  return ReadyState::kReady;
}

Status StorageTpm1::Prepare(Space space, uint32_t size) {
  ASSIGN_OR_RETURN(ReadyState ready_state, IsReady(space),
                   _.WithStatus<TPMError>("Failed to get space ready state"));

  ASSIGN_OR_RETURN(const SpaceInfo& space_info, GetSpaceInfo(space));

  if (ready_state == ReadyState::kReady) {
    return OkStatus();
  }

  if (ready_state == ReadyState::kWriteLocked &&
      !space_info.prepare_if_write_locked) {
    return OkStatus();
  }

  RETURN_IF_ERROR(Destroy(space))
      .WithStatus<TPMError>("Failed to destroy space when prepare space");

  tpm_manager::DefineSpaceRequest define_request;
  define_request.set_index(space_info.index);
  define_request.set_size(size);
  const Attributes& attrs = space_info.init_attributes.value();
  for (int i = 0; i < attrs.size(); i++) {
    if (attrs[i]) {
      define_request.add_attributes(
          static_cast<tpm_manager::NvramSpaceAttribute>(i));
    }
  }

  tpm_manager::DefineSpaceReply define_reply;

  if (brillo::ErrorPtr err; !backend_.GetProxy().GetTpmNvram().DefineSpace(
          define_request, &define_reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  RETURN_IF_ERROR(MakeStatus<TPMNvramError>(define_reply.result()));

  RETURN_IF_ERROR(
      CheckAndRemoveDependency(backend_.GetProxy().GetTpmManager(), space_info))
      .WithStatus<TPMError>("Failed to check and remove dependency");

  return OkStatus();
}

StatusOr<brillo::Blob> StorageTpm1::Load(Space space) {
  ASSIGN_OR_RETURN(const SpaceInfo& space_info, GetSpaceInfo(space));

  tpm_manager::ReadSpaceRequest request;
  request.set_index(space_info.index);
  request.set_use_owner_authorization(space_info.read_with_owner_auth);
  tpm_manager::ReadSpaceReply reply;

  if (brillo::ErrorPtr err; !backend_.GetProxy().GetTpmNvram().ReadSpace(
          request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  RETURN_IF_ERROR(MakeStatus<TPMNvramError>(reply.result()));

  return brillo::BlobFromString(reply.data());
}

Status StorageTpm1::Store(Space space, const brillo::Blob& blob) {
  ASSIGN_OR_RETURN(const SpaceInfo& space_info, GetSpaceInfo(space));

  tpm_manager::WriteSpaceRequest request;
  request.set_index(space_info.index);
  request.set_data(brillo::BlobToString(blob));
  request.set_use_owner_authorization(space_info.write_with_owner_auth);
  tpm_manager::WriteSpaceReply reply;

  if (brillo::ErrorPtr err; !backend_.GetProxy().GetTpmNvram().WriteSpace(
          request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  RETURN_IF_ERROR(MakeStatus<TPMNvramError>(reply.result()));

  if (space_info.lock_after_write) {
    RETURN_IF_ERROR(Lock(space,
                         LockOptions{
                             .read_lock = false,
                             .write_lock = true,
                         }))
        .WithStatus<TPMError>("Failed to lock after write");
  }

  return OkStatus();
}

Status StorageTpm1::Lock(Space space, LockOptions options) {
  ASSIGN_OR_RETURN(const SpaceInfo& space_info, GetSpaceInfo(space));

  tpm_manager::LockSpaceRequest request;
  request.set_index(space_info.index);
  request.set_lock_write(options.write_lock);
  request.set_lock_read(options.read_lock);
  tpm_manager::LockSpaceReply reply;

  if (brillo::ErrorPtr err; !backend_.GetProxy().GetTpmNvram().LockSpace(
          request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  RETURN_IF_ERROR(MakeStatus<TPMNvramError>(reply.result()));

  ASSIGN_OR_RETURN(
      const DetailSpaceInfo& detail_info,
      GetDetailSpaceInfo(backend_.GetProxy().GetTpmNvram(), space_info),
      _.WithStatus<TPMError>("Failed to get detail space info"));

  if (options.read_lock && !detail_info.is_read_locked) {
    return MakeStatus<TPMError>("Space did not read lock as expected",
                                TPMRetryAction::kNoRetry);
  }

  if (options.write_lock && !detail_info.is_write_locked) {
    return MakeStatus<TPMError>("Space did not write lock as expected",
                                TPMRetryAction::kNoRetry);
  }

  return OkStatus();
}

Status StorageTpm1::Destroy(Space space) {
  ASSIGN_OR_RETURN(const SpaceInfo& space_info, GetSpaceInfo(space));

  ASSIGN_OR_RETURN(const absl::flat_hash_set<uint32_t>& space_list,
                   List(backend_.GetProxy().GetTpmNvram()),
                   _.WithStatus<TPMError>("Failed to list space"));

  if (space_list.find(space_info.index) == space_list.end()) {
    return OkStatus();
  }

  tpm_manager::DestroySpaceRequest request;
  request.set_index(space_info.index);
  tpm_manager::DestroySpaceReply reply;

  if (brillo::ErrorPtr err; !backend_.GetProxy().GetTpmNvram().DestroySpace(
          request, &reply, &err, Proxy::kDefaultDBusTimeoutMs)) {
    return MakeStatus<TPMError>(TPMRetryAction::kCommunication)
        .Wrap(std::move(err));
  }

  RETURN_IF_ERROR(MakeStatus<TPMNvramError>(reply.result()));

  return OkStatus();
}

StatusOr<bool> StorageTpm1::IsWriteLocked(Space space) {
  ASSIGN_OR_RETURN(const SpaceInfo& space_info, GetSpaceInfo(space));

  ASSIGN_OR_RETURN(
      const DetailSpaceInfo& detail_info,
      GetDetailSpaceInfo(backend_.GetProxy().GetTpmNvram(), space_info),
      _.WithStatus<TPMError>("Failed to get detail space info"));

  return detail_info.is_write_locked;
}

}  // namespace hwsec
