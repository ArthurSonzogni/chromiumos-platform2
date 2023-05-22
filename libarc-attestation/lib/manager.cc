// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libarc-attestation/lib/manager.h>

#include <memory>
#include <utility>
#include <vector>

#include <base/synchronization/waitable_event.h>
#include <base/threading/thread.h>

#include <libarc-attestation/lib/provisioner.h>
#include <libarc-attestation/lib/version_attester.h>

namespace arc_attestation {

namespace {

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

ArcAttestationManager::ArcAttestationManager() {}

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
}

AndroidStatus ArcAttestationManager::ProvisionDkCert(bool blocking) {
  // Quick check on if it's already provisioned.
  if (provisioner_->is_provisioned())
    return AndroidStatus::ok();
  if (!blocking) {
    // If we're not provisioned yet then we should not proceed to provisioning
    // here because this call should be no blocking.
    // TODO(b/275067764): We should trigger the retry mechanism in the
    // background.
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

ArcAttestationManagerSingleton::ArcAttestationManagerSingleton()
    : manager_(nullptr) {}

void ArcAttestationManagerSingleton::Setup() {
  CHECK(!manager_);
  manager_ = std::make_unique<ArcAttestationManager>();
  manager_->Setup();
}

// This instance is intentionally allowed to leak as this is a singleton in a
// library.
ArcAttestationManagerSingleton* ArcAttestationManagerSingleton::g_instance;

}  // namespace arc_attestation
