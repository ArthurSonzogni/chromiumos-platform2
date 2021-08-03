// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/server/tpm_manager_service.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_type.h>
#include <base/strings/stringprintf.h>
#include <base/synchronization/lock.h>
#include <crypto/sha2.h>
#include <inttypes.h>
#include <libhwsec-foundation/tpm/tpm_version.h>

namespace {

constexpr int kDictionaryAttackResetPeriodInHours = 1;

#if USE_TPM2
// Timeout waiting for Trunks daemon readiness.
constexpr base::TimeDelta kTrunksDaemonTimeout =
    base::TimeDelta::FromSeconds(30);
// Delay between subsequent attempts to initialize connection to Trunks daemon.
constexpr base::TimeDelta kTrunksDaemonInitAttemptDelay =
    base::TimeDelta::FromMicroseconds(300);
#endif

// Clears owner password in |local_data| if all dependencies have been removed
// and it has not yet been cleared.
// Returns true if |local_data| has been modified, false otherwise.
bool ClearOwnerPasswordIfPossible(tpm_manager::LocalData* local_data) {
  if (local_data->has_owner_password() &&
      local_data->owner_dependency().empty()) {
    local_data->clear_owner_password();
    return true;
  }
  return false;
}

int GetFingerprint(uint32_t family,
                   uint64_t spec_level,
                   uint32_t manufacturer,
                   uint32_t tpm_model,
                   uint64_t firmware_version,
                   std::string vendor_specific) {
  // The exact encoding doesn't matter as long as its unambiguous, stable and
  // contains all information present in the version fields.
  std::string encoded_parameters =
      base::StringPrintf("%08" PRIx32 "%016" PRIx64 "%08" PRIx32 "%08" PRIx32
                         "%016" PRIx64 "%016zx",
                         family, spec_level, manufacturer, tpm_model,
                         firmware_version, vendor_specific.size());
  encoded_parameters.append(vendor_specific);
  std::string hash = crypto::SHA256HashString(encoded_parameters);

  // Return the first 31 bits from |hash|.
  int result =
      static_cast<uint8_t>(hash[0]) | static_cast<uint8_t>(hash[1]) << 8 |
      static_cast<uint8_t>(hash[2]) << 16 | static_cast<uint8_t>(hash[3]) << 24;
  return result & 0x7fffffff;
}

}  // namespace

namespace tpm_manager {

namespace {

GetTpmNonsensitiveStatusReply ToGetTpmNonSensitiveStatusReply(
    const GetTpmStatusReply& from) {
  GetTpmNonsensitiveStatusReply to;
  to.set_status(from.status());
  to.set_is_owned(from.owned());
  to.set_is_enabled(from.enabled());
  const LocalData& sensitive = from.local_data();
  to.set_is_owner_password_present(!sensitive.owner_password().empty());
  // This works regardless of TPM version.
  to.set_has_reset_lock_permissions(
      !sensitive.lockout_password().empty() ||
      sensitive.owner_delegate().has_reset_lock_permissions());
  return to;
}

GetTpmStatusRequest ToGetTpmStatusRequest(
    const GetTpmNonsensitiveStatusRequest& from) {
  GetTpmStatusRequest to;
  to.set_ignore_cache(from.ignore_cache());
  return to;
}

}  // namespace

TpmManagerService::TpmManagerService(bool wait_for_ownership,
                                     bool perform_preinit,
                                     LocalDataStore* local_data_store)
    : TpmManagerService(wait_for_ownership,
                        perform_preinit,
                        local_data_store,
                        nullptr,
                        nullptr,
                        nullptr,
                        &default_tpm_manager_metrics_) {
  CHECK(local_data_store_);
}

TpmManagerService::TpmManagerService(bool wait_for_ownership,
                                     bool perform_preinit,
                                     LocalDataStore* local_data_store,
                                     TpmStatus* tpm_status,
                                     TpmInitializer* tpm_initializer,
                                     TpmNvram* tpm_nvram,
                                     TpmManagerMetrics* tpm_manager_metrics)
    : dictionary_attack_timer_(
          base::TimeDelta::FromHours(kDictionaryAttackResetPeriodInHours)),
      local_data_store_(local_data_store),
      tpm_status_(tpm_status),
      tpm_initializer_(tpm_initializer),
      tpm_nvram_(tpm_nvram),
      tpm_manager_metrics_(tpm_manager_metrics),
      update_tpm_status_pending_(false),
      update_tpm_status_cache_dirty_(true),
      wait_for_ownership_(wait_for_ownership),
      perform_preinit_(perform_preinit) {}

TpmManagerService::~TpmManagerService() {
  worker_thread_->Stop();
}

bool TpmManagerService::Initialize() {
  origin_task_runner_ = base::ThreadTaskRunnerHandle::Get();
  worker_thread_.reset(
      new ServiceWorkerThread("TpmManager Service Worker", this));
  worker_thread_->StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0));

  update_tpm_status_pending_ = true;

  PostTaskToWorkerThreadWithoutRequest<GetTpmStatusReply>(
      base::BindOnce(&TpmManagerService::UpdateTpmStatusCallback,
                     base::Unretained(this)),
      &TpmManagerService::InitializeTask);

  ReportVersionFingerprint();
  VLOG(1) << "Worker thread started.";
  return true;
}

void TpmManagerService::ReportVersionFingerprint() {
  auto callback = base::BindOnce(
      [](tpm_manager::TpmManagerMetrics* tpm_manager_metrics,
         const tpm_manager::GetVersionInfoReply& reply) {
        if (reply.status() == STATUS_SUCCESS) {
          uint32_t family = reply.family();
          uint64_t spec_level = reply.spec_level();
          uint32_t manufacturer = reply.manufacturer();
          uint32_t tpm_model = reply.tpm_model();
          uint64_t firmware_version = reply.firmware_version();
          std::string vendor_specific = reply.vendor_specific();
          tpm_manager_metrics->ReportVersionFingerprint(
              GetFingerprint(family, spec_level, manufacturer, tpm_model,
                             firmware_version, vendor_specific));
        }
      },
      base::Unretained(tpm_manager_metrics_));
  GetVersionInfo(tpm_manager::GetVersionInfoRequest(), std::move(callback));
}

void TpmManagerService::InitializeTask(
    const std::shared_ptr<GetTpmStatusReply>& reply) {
  VLOG(1) << "Initializing service...";

  if (!tpm_status_ || !tpm_initializer_ || !tpm_nvram_) {
    // Setup default objects.
    TPM_SELECT_BEGIN;
    TPM2_SECTION({
      default_trunks_factory_ = std::make_unique<trunks::TrunksFactoryImpl>();
      // Tolerate some delay in trunksd being up and ready.
      base::TimeTicks deadline = base::TimeTicks::Now() + kTrunksDaemonTimeout;
      while (!default_trunks_factory_->Initialize() &&
             base::TimeTicks::Now() < deadline) {
        base::PlatformThread::Sleep(kTrunksDaemonInitAttemptDelay);
      }
      default_tpm_status_ =
          std::make_unique<Tpm2StatusImpl>(*default_trunks_factory_);
      tpm_status_ = default_tpm_status_.get();
      default_tpm_initializer_ = std::make_unique<Tpm2InitializerImpl>(
          *default_trunks_factory_, local_data_store_, tpm_status_);
      tpm_initializer_ = default_tpm_initializer_.get();
      default_tpm_nvram_ = std::make_unique<Tpm2NvramImpl>(
          *default_trunks_factory_, local_data_store_, tpm_status_);
      tpm_nvram_ = default_tpm_nvram_.get();
    });
    TPM1_SECTION({
      default_tpm_status_ = std::make_unique<TpmStatusImpl>();
      tpm_status_ = default_tpm_status_.get();
      default_tpm_initializer_ =
          std::make_unique<TpmInitializerImpl>(local_data_store_, tpm_status_);
      tpm_initializer_ = default_tpm_initializer_.get();
      default_tpm_nvram_ = std::make_unique<TpmNvramImpl>(local_data_store_);
      tpm_nvram_ = default_tpm_nvram_.get();
    });
    OTHER_TPM_SECTION();
    TPM_SELECT_END;
  }
  if (!tpm_status_->IsTpmEnabled()) {
    LOG(WARNING) << __func__ << ": TPM is disabled.";
    reply->set_enabled(false);
    reply->set_status(STATUS_SUCCESS);
    return;
  }
  reply->set_enabled(true);
  tpm_initializer_->VerifiedBootHelper();

  TpmStatus::TpmOwnershipStatus ownership_status;
  if (!tpm_status_->GetTpmOwned(&ownership_status)) {
    LOG(ERROR) << __func__
               << ": failed to get tpm ownership status, maybe it's the "
                  "dictionary attack lockout.";
    // GetStatus could fail because the TPM is under DA lockout, so we'll try to
    // reset lockout then try again.
    ResetDictionaryAttackCounterIfNeeded();
    if (!tpm_status_->GetTpmOwned(&ownership_status)) {
      LOG(ERROR) << __func__
                 << ": get tpm ownership status still failed. Giving up.";
      reply->set_status(STATUS_DEVICE_ERROR);
      return;
    }
    LOG(INFO) << __func__
              << ": get tpm ownership status suceeded after dictionary attack "
                 "lockout reset.";
  }

  // The precondition of DA reset is not satisfied; resets the timer so it
  // doesn't get triggered immediately.
  if (ownership_status != TpmStatus::kTpmOwned && wait_for_ownership_) {
    dictionary_attack_timer_.Reset();
  }
  worker_thread_->task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &TpmManagerService::PeriodicResetDictionaryAttackCounterTask,
          base::Unretained(this)));

  reply->set_owned(TpmStatus::kTpmOwned == ownership_status);
  if (ownership_status == TpmStatus::kTpmOwned) {
    VLOG(1) << "Tpm is already owned.";
    if (!tpm_initializer_->EnsurePersistentOwnerDelegate()) {
      // Only treat the failure as a warning because the daemon can be partly
      // operational still.
      LOG(WARNING)
          << __func__
          << ": Failed to ensure owner delegate is ready with ownership taken.";
    }
    LocalData local_data;
    if (local_data_store_ && local_data_store_->Read(&local_data)) {
      ReportSecretStatus(local_data);
      *(reply->mutable_local_data()) = std::move(local_data);
    }
    DisableDictionaryAttackMitigationIfNeeded();
    reply->set_status(STATUS_SUCCESS);
    NotifyTpmIsOwned();
    return;
  }

  // TPM is not fully owned yet. There might be stale data in the local data
  // store. Checks and removes them if needed.
  tpm_initializer_->PruneStoredPasswords();
  tpm_nvram_->PrunePolicies();

  if (!wait_for_ownership_) {
    VLOG(1) << "Initializing TPM.";

    const base::TimeTicks take_ownerhip_start_time = base::TimeTicks::Now();
    bool already_owned;
    if (!tpm_initializer_->InitializeTpm(&already_owned)) {
      LOG(WARNING) << __func__ << ": TPM initialization failed.";
      dictionary_attack_timer_.Reset();
      reply->set_status(STATUS_NOT_AVAILABLE);
      return;
    }
    if (!already_owned) {
      tpm_manager_metrics_->ReportTimeToTakeOwnership(base::TimeTicks::Now() -
                                                      take_ownerhip_start_time);
    }
    reply->set_owned(true);
  } else if (perform_preinit_) {
    VLOG(1) << "Pre-initializing TPM.";
    tpm_initializer_->PreInitializeTpm();
  }
  LocalData local_data;
  if (local_data_store_ && local_data_store_->Read(&local_data)) {
    *(reply->mutable_local_data()) = std::move(local_data);
  }
  reply->set_status(STATUS_SUCCESS);
  if (reply->owned()) {
    NotifyTpmIsOwned();
  }
}

void TpmManagerService::ReportSecretStatus(const LocalData& local_data) {
  SecretStatus status = {
      .has_owner_password = !local_data.owner_password().empty(),
      .has_endorsement_password = !local_data.endorsement_password().empty(),
      .has_lockout_password = !local_data.lockout_password().empty(),
      .has_owner_delegate = !local_data.owner_delegate().secret().empty() &&
                            !local_data.owner_delegate().blob().empty(),
      .has_reset_lock_permissions =
          !local_data.lockout_password().empty() ||
          local_data.owner_delegate().has_reset_lock_permissions(),
  };
  tpm_manager_metrics_->ReportSecretStatus(status);
}

void TpmManagerService::NotifyTpmIsOwned() {
  DCHECK_EQ(base::PlatformThread::CurrentId(), worker_thread_->GetThreadId());
  if (!ownership_taken_callback_.is_null()) {
    ownership_taken_callback_.Run();
    ownership_taken_callback_.Reset();
  }
}

void TpmManagerService::MarkTpmStatusCacheDirty() {
  if (base::PlatformThread::CurrentId() == worker_thread_->GetThreadId()) {
    // This should run on origin thread
    origin_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&TpmManagerService::MarkTpmStatusCacheDirty,
                                  base::Unretained(this)));
    return;
  }

  CHECK_NE(base::PlatformThread::CurrentId(), worker_thread_->GetThreadId());

  update_tpm_status_cache_dirty_ = true;
}

void TpmManagerService::GetTpmStatus(const GetTpmStatusRequest& request,
                                     GetTpmStatusCallback callback) {
  if (update_tpm_status_cache_dirty_ || request.ignore_cache()) {
    get_tpm_status_waiting_callbacks_.emplace_back(std::move(callback));
  } else {
    std::move(callback).Run(get_tpm_status_cache_);
    return;
  }
  if (update_tpm_status_pending_) {
    return;
  }
  update_tpm_status_pending_ = true;
  PostTaskToWorkerThread<GetTpmStatusReply>(
      request,
      base::BindOnce(&TpmManagerService::UpdateTpmStatusCallback,
                     base::Unretained(this)),
      &TpmManagerService::GetTpmStatusTask);
}

void TpmManagerService::GetTpmNonsensitiveStatus(
    const GetTpmNonsensitiveStatusRequest& request,
    GetTpmNonsensitiveStatusCallback callback) {
  // This function has a different way to proceed the request from other
  // request; the callback is wrapped to `GetTpmStatusCallback` followed by a
  // handle of `GetTpmStatus()`. Before sending the response,
  // `ToGetTpmNonSensitiveStatusReply()` absracts the sensitive secret in
  // `GetTpmStatusReply` away.
  GetTpmStatusCallback wrapped_callback = base::BindOnce(
      [](GetTpmNonsensitiveStatusCallback cb, const GetTpmStatusReply& reply) {
        std::move(cb).Run(ToGetTpmNonSensitiveStatusReply(reply));
      },
      std::move(callback));

  GetTpmStatus(ToGetTpmStatusRequest(request), std::move(wrapped_callback));
}

void TpmManagerService::UpdateTpmStatusCallback(
    const GetTpmStatusReply& reply) {
  DCHECK_NE(base::PlatformThread::CurrentId(), worker_thread_->GetThreadId());
  update_tpm_status_cache_dirty_ = reply.status() != STATUS_SUCCESS;
  update_tpm_status_pending_ = false;
  get_tpm_status_cache_ = reply;
  std::vector<GetTpmStatusCallback> callbacks;
  callbacks.swap(get_tpm_status_waiting_callbacks_);
  for (auto& callback : callbacks) {
    std::move(callback).Run(reply);
  }
}

void TpmManagerService::GetTpmStatusTask(
    const GetTpmStatusRequest& request,
    const std::shared_ptr<GetTpmStatusReply>& reply) {
  VLOG(1) << __func__;

  if (!tpm_status_) {
    LOG(ERROR) << __func__ << ": tpm status is uninitialized.";
    reply->set_status(STATUS_NOT_AVAILABLE);
    return;
  }

  reply->set_enabled(tpm_status_->IsTpmEnabled());

  TpmStatus::TpmOwnershipStatus ownership_status;
  if (!tpm_status_->GetTpmOwned(&ownership_status)) {
    LOG(ERROR) << __func__ << ": failed to get tpm ownership status";
    reply->set_status(STATUS_DEVICE_ERROR);
    return;
  }
  reply->set_owned(TpmStatus::kTpmOwned == ownership_status);

  LocalData local_data;
  if (local_data_store_ && local_data_store_->Read(&local_data)) {
    *reply->mutable_local_data() = local_data;
  }

  reply->set_status(STATUS_SUCCESS);
}

void TpmManagerService::GetVersionInfo(const GetVersionInfoRequest& request,
                                       GetVersionInfoCallback callback) {
  {
    base::AutoLock lock(version_info_cache_lock_);
    if (version_info_cache_) {
      std::move(callback).Run(*version_info_cache_);
      return;
    }
  }

  PostTaskToWorkerThread<GetVersionInfoReply>(
      request, std::move(callback), &TpmManagerService::GetVersionInfoTask);
}

void TpmManagerService::GetVersionInfoTask(
    const GetVersionInfoRequest& request,
    const std::shared_ptr<GetVersionInfoReply>& reply) {
  VLOG(1) << __func__;

  // It's possible that cache was not available when the request came to the
  // main thread but became available when the task is being processed here.
  // Checks the cache again to save one TPM call.
  if (version_info_cache_) {
    *reply = *version_info_cache_;
    return;
  }

  if (!tpm_status_) {
    LOG(ERROR) << __func__ << ": tpm status is uninitialized.";
    reply->set_status(STATUS_NOT_AVAILABLE);
    return;
  }

  uint32_t family;
  uint64_t spec_level;
  uint32_t manufacturer;
  uint32_t tpm_model;
  uint64_t firmware_version;
  std::vector<uint8_t> vendor_specific;
  if (!tpm_status_->GetVersionInfo(&family, &spec_level, &manufacturer,
                                   &tpm_model, &firmware_version,
                                   &vendor_specific)) {
    LOG(ERROR) << __func__ << ": failed to get version info from tpm status.";
    reply->set_status(STATUS_DEVICE_ERROR);
    return;
  }

  reply->set_family(family);
  reply->set_spec_level(spec_level);
  reply->set_manufacturer(manufacturer);
  reply->set_tpm_model(tpm_model);
  reply->set_firmware_version(firmware_version);
  reply->set_vendor_specific(reinterpret_cast<char*>(vendor_specific.data()),
                             vendor_specific.size());
  reply->set_status(STATUS_SUCCESS);

  {
    base::AutoLock lock(version_info_cache_lock_);
    version_info_cache_ = *reply;
  }
}

void TpmManagerService::GetSupportedFeatures(
    const GetSupportedFeaturesRequest& request,
    GetSupportedFeaturesCallback callback) {
  {
    base::AutoLock lock(supported_features_cache_lock_);
    if (supported_features_cache_) {
      std::move(callback).Run(*supported_features_cache_);
      return;
    }
  }
  PostTaskToWorkerThread<GetSupportedFeaturesReply>(
      request, std::move(callback),
      &TpmManagerService::GetSupportedFeaturesTask);
}

void TpmManagerService::GetSupportedFeaturesTask(
    const GetSupportedFeaturesRequest& request,
    const std::shared_ptr<GetSupportedFeaturesReply>& reply) {
  // It's possible that cache was not available when the request came to the
  // main thread but became available when the task is being processed here.
  // Checks the cache again to save one TPM call.
  if (supported_features_cache_) {
    *reply = *supported_features_cache_;
    return;
  }

  reply->set_support_u2f(tpm_status_->SupportU2f());
  reply->set_status(STATUS_SUCCESS);

  {
    base::AutoLock lock(supported_features_cache_lock_);
    supported_features_cache_ = *reply;
  }
}

void TpmManagerService::GetDictionaryAttackInfo(
    const GetDictionaryAttackInfoRequest& request,
    GetDictionaryAttackInfoCallback callback) {
  PostTaskToWorkerThread<GetDictionaryAttackInfoReply>(
      request, std::move(callback),
      &TpmManagerService::GetDictionaryAttackInfoTask);
}

void TpmManagerService::GetDictionaryAttackInfoTask(
    const GetDictionaryAttackInfoRequest& request,
    const std::shared_ptr<GetDictionaryAttackInfoReply>& reply) {
  VLOG(1) << __func__;

  if (!tpm_status_) {
    LOG(ERROR) << __func__ << ": tpm status is uninitialized.";
    reply->set_status(STATUS_NOT_AVAILABLE);
    return;
  }

  uint32_t counter;
  uint32_t threshold;
  bool lockout;
  uint32_t lockout_time_remaining;
  if (!tpm_status_->GetDictionaryAttackInfo(&counter, &threshold, &lockout,
                                            &lockout_time_remaining)) {
    LOG(ERROR) << __func__ << ": failed to get DA info";
    reply->set_status(STATUS_DEVICE_ERROR);
    return;
  }

  reply->set_dictionary_attack_counter(counter);
  reply->set_dictionary_attack_threshold(threshold);
  reply->set_dictionary_attack_lockout_in_effect(lockout);
  reply->set_dictionary_attack_lockout_seconds_remaining(
      lockout_time_remaining);
  reply->set_status(STATUS_SUCCESS);
}

void TpmManagerService::ResetDictionaryAttackLock(
    const ResetDictionaryAttackLockRequest& request,
    ResetDictionaryAttackLockCallback callback) {
  if (request.is_async()) {
    ResetDictionaryAttackLockReply reply;
    reply.set_status(STATUS_SUCCESS);
    std::move(callback).Run(reply);
    PostTaskToWorkerThread<ResetDictionaryAttackLockReply>(
        request, base::DoNothing(),
        &TpmManagerService::ResetDictionaryAttackLockTask);
    return;
  }
  PostTaskToWorkerThread<ResetDictionaryAttackLockReply>(
      request, std::move(callback),
      &TpmManagerService::ResetDictionaryAttackLockTask);
}

void TpmManagerService::ResetDictionaryAttackLockTask(
    const ResetDictionaryAttackLockRequest& request,
    const std::shared_ptr<ResetDictionaryAttackLockReply>& reply) {
  VLOG(1) << __func__;

  if (!tpm_initializer_) {
    LOG(ERROR) << __func__ << ": request received before tpm manager service "
               << "is initialized.";
    reply->set_status(STATUS_NOT_AVAILABLE);
    return;
  }
  if (!ResetDictionaryAttackCounterIfNeeded()) {
    LOG(ERROR) << __func__ << ": failed to reset DA lock.";
    reply->set_status(STATUS_DEVICE_ERROR);
  } else {
    reply->set_status(STATUS_SUCCESS);
  }
  dictionary_attack_timer_.Reset();
}

void TpmManagerService::TakeOwnership(const TakeOwnershipRequest& request,
                                      TakeOwnershipCallback callback) {
  if (request.is_async()) {
    TakeOwnershipReply reply;
    reply.set_status(STATUS_SUCCESS);
    std::move(callback).Run(reply);
    PostTaskToWorkerThread<TakeOwnershipReply>(
        request, base::DoNothing(), &TpmManagerService::TakeOwnershipTask);
    return;
  }
  PostTaskToWorkerThread<TakeOwnershipReply>(
      request, std::move(callback), &TpmManagerService::TakeOwnershipTask);
}

void TpmManagerService::TakeOwnershipTask(
    const TakeOwnershipRequest& request,
    const std::shared_ptr<TakeOwnershipReply>& reply) {
  VLOG(1) << __func__;
  if (!tpm_status_->IsTpmEnabled()) {
    reply->set_status(STATUS_NOT_AVAILABLE);
    return;
  }

  const base::TimeTicks take_ownerhip_start_time = base::TimeTicks::Now();
  bool already_owned;
  if (!tpm_initializer_->InitializeTpm(&already_owned)) {
    LOG(ERROR) << __func__ << ": failed to initialize TPM";
    reply->set_status(STATUS_DEVICE_ERROR);
    return;
  }
  if (!already_owned) {
    tpm_manager_metrics_->ReportTimeToTakeOwnership(base::TimeTicks::Now() -
                                                    take_ownerhip_start_time);
  }

  MarkTpmStatusCacheDirty();
  NotifyTpmIsOwned();
  if (!ResetDictionaryAttackCounterIfNeeded()) {
    LOG(WARNING) << __func__ << ": DA reset failed after taking ownership.";
  }
  dictionary_attack_timer_.Reset();
  // Forcedly disable DA mitigation to be extra sure the DA mitigation is
  // disabled for a device through OOBE.
  DisableDictionaryAttackMitigationIfNeeded();
  reply->set_status(STATUS_SUCCESS);
}

void TpmManagerService::RemoveOwnerDependency(
    const RemoveOwnerDependencyRequest& request,
    RemoveOwnerDependencyCallback callback) {
  PostTaskToWorkerThread<RemoveOwnerDependencyReply>(
      request, std::move(callback),
      &TpmManagerService::RemoveOwnerDependencyTask);
}

void TpmManagerService::RemoveOwnerDependencyTask(
    const RemoveOwnerDependencyRequest& request,
    const std::shared_ptr<RemoveOwnerDependencyReply>& reply) {
  VLOG(1) << __func__;
  LocalData local_data;
  if (!local_data_store_->Read(&local_data)) {
    reply->set_status(STATUS_DEVICE_ERROR);
    return;
  }
  RemoveOwnerDependencyFromLocalData(request.owner_dependency(), &local_data);
  if (auto_clear_stored_owner_password_) {
    ClearOwnerPasswordIfPossible(&local_data);
  }
  if (!local_data_store_->Write(local_data)) {
    reply->set_status(STATUS_DEVICE_ERROR);
    return;
  }
  reply->set_status(STATUS_SUCCESS);
  MarkTpmStatusCacheDirty();
}

void TpmManagerService::RemoveOwnerDependencyFromLocalData(
    const std::string& owner_dependency, LocalData* local_data) {
  google::protobuf::RepeatedPtrField<std::string>* dependencies =
      local_data->mutable_owner_dependency();
  for (int i = 0; i < dependencies->size(); i++) {
    if (dependencies->Get(i) == owner_dependency) {
      dependencies->SwapElements(i, (dependencies->size() - 1));
      dependencies->RemoveLast();
      break;
    }
  }
}

void TpmManagerService::ClearStoredOwnerPassword(
    const ClearStoredOwnerPasswordRequest& request,
    ClearStoredOwnerPasswordCallback callback) {
  PostTaskToWorkerThread<ClearStoredOwnerPasswordReply>(
      request, std::move(callback),
      &TpmManagerService::ClearStoredOwnerPasswordTask);
}

void TpmManagerService::ClearStoredOwnerPasswordTask(
    const ClearStoredOwnerPasswordRequest& request,
    const std::shared_ptr<ClearStoredOwnerPasswordReply>& reply) {
  VLOG(1) << __func__;
  LocalData local_data;
  if (!local_data_store_->Read(&local_data)) {
    reply->set_status(STATUS_DEVICE_ERROR);
    return;
  }
  if (ClearOwnerPasswordIfPossible(&local_data)) {
    if (!local_data_store_->Write(local_data)) {
      reply->set_status(STATUS_DEVICE_ERROR);
      return;
    }
  }
  reply->set_status(STATUS_SUCCESS);
  MarkTpmStatusCacheDirty();
}

void TpmManagerService::DefineSpace(const DefineSpaceRequest& request,
                                    DefineSpaceCallback callback) {
  PostTaskToWorkerThread<DefineSpaceReply>(request, std::move(callback),
                                           &TpmManagerService::DefineSpaceTask);
}

void TpmManagerService::DefineSpaceTask(
    const DefineSpaceRequest& request,
    const std::shared_ptr<DefineSpaceReply>& reply) {
  VLOG(1) << __func__;
  std::vector<NvramSpaceAttribute> attributes;
  for (int i = 0; i < request.attributes_size(); ++i) {
    attributes.push_back(request.attributes(i));
  }
  reply->set_result(
      tpm_nvram_->DefineSpace(request.index(), request.size(), attributes,
                              request.authorization_value(), request.policy()));
  MarkTpmStatusCacheDirty();
}

void TpmManagerService::DestroySpace(const DestroySpaceRequest& request,
                                     DestroySpaceCallback callback) {
  PostTaskToWorkerThread<DestroySpaceReply>(
      request, std::move(callback), &TpmManagerService::DestroySpaceTask);
}

void TpmManagerService::DestroySpaceTask(
    const DestroySpaceRequest& request,
    const std::shared_ptr<DestroySpaceReply>& reply) {
  VLOG(1) << __func__;
  reply->set_result(tpm_nvram_->DestroySpace(request.index()));
  MarkTpmStatusCacheDirty();
}

void TpmManagerService::WriteSpace(const WriteSpaceRequest& request,
                                   WriteSpaceCallback callback) {
  PostTaskToWorkerThread<WriteSpaceReply>(request, std::move(callback),
                                          &TpmManagerService::WriteSpaceTask);
}

void TpmManagerService::WriteSpaceTask(
    const WriteSpaceRequest& request,
    const std::shared_ptr<WriteSpaceReply>& reply) {
  VLOG(1) << __func__;
  std::string authorization_value = request.authorization_value();
  if (request.use_owner_authorization()) {
    authorization_value = GetOwnerPassword();
    if (authorization_value.empty()) {
      LOG(ERROR) << __func__ << ": Owner auth missing while requested.";
      reply->set_result(NVRAM_RESULT_ACCESS_DENIED);
      return;
    }
  }
  reply->set_result(tpm_nvram_->WriteSpace(request.index(), request.data(),
                                           authorization_value));
}

void TpmManagerService::ReadSpace(const ReadSpaceRequest& request,
                                  ReadSpaceCallback callback) {
  PostTaskToWorkerThread<ReadSpaceReply>(request, std::move(callback),
                                         &TpmManagerService::ReadSpaceTask);
}

void TpmManagerService::ReadSpaceTask(
    const ReadSpaceRequest& request,
    const std::shared_ptr<ReadSpaceReply>& reply) {
  VLOG(1) << __func__;
  std::string authorization_value = request.authorization_value();
  if (request.use_owner_authorization()) {
    authorization_value = GetOwnerPassword();
    if (authorization_value.empty()) {
      reply->set_result(NVRAM_RESULT_ACCESS_DENIED);
      return;
    }
  }
  reply->set_result(tpm_nvram_->ReadSpace(
      request.index(), reply->mutable_data(), authorization_value));
}

void TpmManagerService::LockSpace(const LockSpaceRequest& request,
                                  LockSpaceCallback callback) {
  PostTaskToWorkerThread<LockSpaceReply>(request, std::move(callback),
                                         &TpmManagerService::LockSpaceTask);
}

void TpmManagerService::LockSpaceTask(
    const LockSpaceRequest& request,
    const std::shared_ptr<LockSpaceReply>& reply) {
  VLOG(1) << __func__;
  std::string authorization_value = request.authorization_value();
  if (request.use_owner_authorization()) {
    authorization_value = GetOwnerPassword();
    if (authorization_value.empty()) {
      reply->set_result(NVRAM_RESULT_ACCESS_DENIED);
      return;
    }
  }
  reply->set_result(tpm_nvram_->LockSpace(request.index(), request.lock_read(),
                                          request.lock_write(),
                                          authorization_value));
}

void TpmManagerService::ListSpaces(const ListSpacesRequest& request,
                                   ListSpacesCallback callback) {
  PostTaskToWorkerThread<ListSpacesReply>(request, std::move(callback),
                                          &TpmManagerService::ListSpacesTask);
}

void TpmManagerService::ListSpacesTask(
    const ListSpacesRequest& request,
    const std::shared_ptr<ListSpacesReply>& reply) {
  VLOG(1) << __func__;
  std::vector<uint32_t> index_list;
  reply->set_result(tpm_nvram_->ListSpaces(&index_list));
  if (reply->result() == NVRAM_RESULT_SUCCESS) {
    for (auto index : index_list) {
      reply->add_index_list(index);
    }
  }
}

void TpmManagerService::GetSpaceInfo(const GetSpaceInfoRequest& request,
                                     GetSpaceInfoCallback callback) {
  PostTaskToWorkerThread<GetSpaceInfoReply>(
      request, std::move(callback), &TpmManagerService::GetSpaceInfoTask);
}

void TpmManagerService::GetSpaceInfoTask(
    const GetSpaceInfoRequest& request,
    const std::shared_ptr<GetSpaceInfoReply>& reply) {
  VLOG(1) << __func__;
  std::vector<NvramSpaceAttribute> attributes;
  uint32_t size = 0;
  bool is_read_locked = false;
  bool is_write_locked = false;
  NvramSpacePolicy policy = NVRAM_POLICY_NONE;
  reply->set_result(tpm_nvram_->GetSpaceInfo(request.index(), &size,
                                             &is_read_locked, &is_write_locked,
                                             &attributes, &policy));
  if (reply->result() == NVRAM_RESULT_SUCCESS) {
    reply->set_size(size);
    reply->set_is_read_locked(is_read_locked);
    reply->set_is_write_locked(is_write_locked);
    for (auto attribute : attributes) {
      reply->add_attributes(attribute);
    }
    reply->set_policy(policy);
  }
}

std::string TpmManagerService::GetOwnerPassword() {
  LocalData local_data;
  if (local_data_store_->Read(&local_data)) {
    return local_data.owner_password();
  }
  LOG(ERROR) << "TPM owner password requested but not available.";
  return std::string();
}

bool TpmManagerService::ResetDictionaryAttackCounterIfNeeded() {
  uint32_t counter = 0;
  uint32_t threshold = 0;
  bool lockout = false;
  uint32_t time_remaining = 0;
  if (!tpm_status_->GetDictionaryAttackInfo(&counter, &threshold, &lockout,
                                            &time_remaining)) {
    // Reports the metrics but no early return since reset itself might work.
    tpm_manager_metrics_->ReportDictionaryAttackResetStatus(
        DictionaryAttackResetStatus::kCounterQueryFailed);
  } else {
    tpm_manager_metrics_->ReportDictionaryAttackCounter(counter);
    if (counter == 0) {
      tpm_manager_metrics_->ReportDictionaryAttackResetStatus(
          DictionaryAttackResetStatus::kResetNotNecessary);
      return true;
    }
  }
  auto status = tpm_initializer_->ResetDictionaryAttackLock();
  tpm_manager_metrics_->ReportDictionaryAttackResetStatus(status);
  return status == DictionaryAttackResetStatus::kResetAttemptSucceeded;
}

void TpmManagerService::PeriodicResetDictionaryAttackCounterTask() {
  VLOG(1) << __func__;
  base::TimeDelta time_remaining = dictionary_attack_timer_.TimeRemaining();
  // if the timer is up, run the task and reset the timer.
  if (time_remaining.is_zero()) {
    if (!ResetDictionaryAttackCounterIfNeeded()) {
      LOG(WARNING) << __func__ << ": DA reset failed.";
    } else {
      LOG(INFO) << __func__ << ": DA reset succeeded.";
    }
    dictionary_attack_timer_.Reset();
    time_remaining = dictionary_attack_timer_.TimeRemaining();
  } else {
    LOG(INFO) << __func__ << ": Time is not up yet.";
  }
  worker_thread_->task_runner()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          &TpmManagerService::PeriodicResetDictionaryAttackCounterTask,
          base::Unretained(this)),
      time_remaining);
}

void TpmManagerService::DisableDictionaryAttackMitigationIfNeeded() {
  bool is_enabled = false;
  if (!tpm_status_->IsDictionaryAttackMitigationEnabled(&is_enabled)) {
    LOG(WARNING) << __func__
                 << ": Failed to check if DA mitigation mechanism is "
                    "enabled...Still attempting to disable it.";
  } else if (!is_enabled) {
    return;
  }

  switch (tpm_initializer_->DisableDictionaryAttackMitigation()) {
    case TpmInitializerStatus::kSuccess:
      break;
    case TpmInitializerStatus::kFailure:
      LOG(ERROR) << __func__ << ": Failed to disable DA mitigation.";
      return;
    case TpmInitializerStatus::kNotSupport:
      return;
  }
}

void TpmManagerService::ShutdownTask() {
  default_tpm_status_.reset();
  default_tpm_initializer_.reset();
  default_tpm_nvram_.reset();

  TPM_SELECT_BEGIN;
  TPM2_SECTION({
    // Resets |default_trunks_factory_| last because other components hold its
    // reference.
    default_trunks_factory_.reset();
  });
  OTHER_TPM_SECTION();
  TPM_SELECT_END;
}

template <typename ReplyProtobufType>
void TpmManagerService::TaskRelayCallback(
    base::OnceCallback<void(const ReplyProtobufType&)> callback,
    const std::shared_ptr<ReplyProtobufType>& reply) {
  std::move(callback).Run(*reply);
}

template <typename ReplyProtobufType,
          typename RequestProtobufType,
          typename ReplyCallbackType,
          typename TaskType>
void TpmManagerService::PostTaskToWorkerThread(
    const RequestProtobufType& request,
    ReplyCallbackType callback,
    TaskType task) {
  auto result = std::make_shared<ReplyProtobufType>();
  base::OnceClosure background_task =
      base::BindOnce(task, base::Unretained(this), request, result);
  base::OnceClosure reply =
      base::BindOnce(&TpmManagerService::TaskRelayCallback<ReplyProtobufType>,
                     weak_factory_.GetWeakPtr(), std::move(callback), result);
  worker_thread_->task_runner()->PostTaskAndReply(
      FROM_HERE, std::move(background_task), std::move(reply));
}

template <typename ReplyProtobufType,
          typename ReplyCallbackType,
          typename TaskType>
void TpmManagerService::PostTaskToWorkerThreadWithoutRequest(
    ReplyCallbackType callback, TaskType task) {
  auto result = std::make_shared<ReplyProtobufType>();
  base::OnceClosure background_task =
      base::BindOnce(task, base::Unretained(this), result);
  base::OnceClosure reply =
      base::BindOnce(&TpmManagerService::TaskRelayCallback<ReplyProtobufType>,
                     weak_factory_.GetWeakPtr(), std::move(callback), result);
  worker_thread_->task_runner()->PostTaskAndReply(
      FROM_HERE, std::move(background_task), std::move(reply));
}

}  // namespace tpm_manager
