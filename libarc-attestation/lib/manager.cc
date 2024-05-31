// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>
#include <vector>

#include <base/synchronization/waitable_event.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <libarc-attestation/lib/manager.h>
#include <libarc-attestation/lib/provisioner.h>
#include <libarc-attestation/lib/version_attester.h>

namespace arc_attestation {

namespace {

// 1 minute, unit in ms.
constexpr double kBackgroundProvisionRetryStartingPeriod = 1 * 60 * 1000;
// Delay increases by 1.7x by every failure.
constexpr double kBackgroundProvisionRetryMultiplier = 1.7;

void RunInTaskRunnerBlocking(scoped_refptr<base::SingleThreadTaskRunner> runner,
                             base::OnceClosure task) {
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  runner->PostTask(
      FROM_HERE, std::move(task).Then(base::BindOnce(
                     &base::WaitableEvent::Signal, base::Unretained(&event))));
  event.Wait();
}

}  // namespace

ArcAttestationManager::ArcAttestationManager() : backoff_retry_(nullptr) {}

void ArcAttestationManager::Setup() {
  // Start the library thread.
  library_thread_ = std::make_unique<base::Thread>("libarc-attestation-worker");
  base::Thread::Options options;
  options.message_pump_type = base::MessagePumpType::IO;
  library_thread_->StartWithOptions(std::move(options));
  library_task_runner_ = library_thread_->task_runner();

  // Setup the provisioner.
  provisioner_ = std::make_unique<Provisioner>(library_task_runner_);
  version_attester_ = std::make_unique<VersionAttester>(provisioner_.get());

  backoff_retry_ = std::make_unique<ExponentialBackoff>(
      kBackgroundProvisionRetryStartingPeriod,
      kBackgroundProvisionRetryMultiplier,
      base::BindRepeating(&ArcAttestationManager::BackgroundProvision,
                          base::Unretained(this)),
      library_task_runner_);
}

AndroidStatus ArcAttestationManager::ProvisionDkCert(bool blocking) {
  // Quick check on if it's already provisioned.
  if (provisioner_->is_provisioned())
    return AndroidStatus::ok();
  if (!blocking) {
    // If we're not provisioned yet then we should not proceed to provisioning
    // here because this call should be non blocking.
    library_task_runner_->PostTask(FROM_HERE,
                                   base::BindOnce(
                                       [](ArcAttestationManager* manager) {
                                         manager->backoff_retry_->TriggerTry();
                                       },
                                       this));
    return AndroidStatus::from_keymint_code(
        AndroidStatus::KeymintSpecificErrorCode::
            SECURE_HW_COMMUNICATION_FAILED);
  }
  AndroidStatus result;
  RunInTaskRunnerBlocking(
      library_task_runner_.get(),
      base::BindOnce(
          [](ArcAttestationManager* manager, AndroidStatus* result) {
            *result = manager->provisioner_->ProvisionCert();
          },
          this, &result));
  return result;
}

bool ArcAttestationManager::BackgroundProvision() {
  if (provisioner_->is_provisioned()) {
    return true;
  }
  provisioner_->ProvisionCert();

  if (!provisioner_->is_provisioned()) {
    return false;
  }
  return true;
}

AndroidStatus ArcAttestationManager::GetDkCertChain(
    std::vector<brillo::Blob>& cert_out) {
  AndroidStatus result;
  RunInTaskRunnerBlocking(
      library_task_runner_.get(),
      base::BindOnce(
          [](ArcAttestationManager* manager, AndroidStatus* result,
             std::vector<brillo::Blob>* cert_out) {
            *result = manager->provisioner_->GetDkCertChain(*cert_out);
          },
          this, &result, &cert_out));
  return result;
}

// Sign with the device key.
AndroidStatus ArcAttestationManager::SignWithP256Dk(const brillo::Blob& input,
                                                    brillo::Blob& signature) {
  AndroidStatus result;
  RunInTaskRunnerBlocking(
      library_task_runner_.get(),
      base::BindOnce(
          [](ArcAttestationManager* manager, AndroidStatus* result,
             const brillo::Blob& input, brillo::Blob* signature) {
            *result = manager->provisioner_->SignWithP256Dk(input, *signature);
          },
          this, &result, input, &signature));
  return result;
}

// Quote the ChromeOS specific blob for ARC Attestation.
AndroidStatus ArcAttestationManager::QuoteCrOSBlob(
    const brillo::Blob& challenge, brillo::Blob& output) {
  AndroidStatus result;
  RunInTaskRunnerBlocking(
      library_task_runner_.get(),
      base::BindOnce(
          [](ArcAttestationManager* manager, AndroidStatus* result,
             const brillo::Blob& challenge, brillo::Blob* output) {
            *result =
                manager->version_attester_->QuoteCrOSBlob(challenge, *output);
          },
          this, &result, challenge, &output));
  return result;
}

AndroidStatus ArcAttestationManager::GetEndorsementPublicKey(
    brillo::Blob& ek_public_key_out) {
  AndroidStatus result;
  RunInTaskRunnerBlocking(
      library_task_runner_.get(),
      base::BindOnce(
          [](ArcAttestationManager* manager, AndroidStatus* result,
             brillo::Blob* ek_public_key_out) {
            *result = manager->provisioner_->GetEndorsementPublicKey(
                *ek_public_key_out);
          },
          this, &result, &ek_public_key_out));
  return result;
}

}  // namespace arc_attestation
