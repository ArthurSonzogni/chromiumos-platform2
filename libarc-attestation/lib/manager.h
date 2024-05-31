// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBARC_ATTESTATION_LIB_MANAGER_H_
#define LIBARC_ATTESTATION_LIB_MANAGER_H_

#include <memory>
#include <utility>
#include <vector>

#include <base/threading/thread.h>
#include <brillo/secure_blob.h>
#include <libarc-attestation/lib/exponential_backoff.h>
#include <libarc-attestation/lib/interface.h>
#include <libarc-attestation/lib/manager_base.h>
#include <libarc-attestation/lib/provisioner.h>
#include <libarc-attestation/lib/version_attester.h>

namespace arc_attestation {

// ArcAttestationManager is a singleton class that service all API calls.
class ArcAttestationManager : public ArcAttestationManagerBase {
 public:
  ArcAttestationManager();
  ~ArcAttestationManager() override = default;

  void Setup() override;
  AndroidStatus ProvisionDkCert(bool blocking) override;
  AndroidStatus GetDkCertChain(std::vector<brillo::Blob>& cert_out) override;
  AndroidStatus SignWithP256Dk(const brillo::Blob& input,
                               brillo::Blob& signature) override;
  AndroidStatus QuoteCrOSBlob(const brillo::Blob& challenge,
                              brillo::Blob& output) override;
  AndroidStatus GetEndorsementPublicKey(
      brillo::Blob& ek_public_key_out) override;

  Provisioner* GetProvisionerForTesting() { return provisioner_.get(); }

  VersionAttester* GetVersionAttesterForTesting() {
    return version_attester_.get();
  }

 private:
  // All library operations run on this thread.
  std::unique_ptr<base::Thread> library_thread_;
  // Task runner for the library thread.
  scoped_refptr<base::SingleThreadTaskRunner> library_task_runner_;
  // The provisioner for provisioning the certs. Caller can assume this is
  // always available after the constructor.
  std::unique_ptr<Provisioner> provisioner_;
  // Version Attester deals with quoting the OS version.
  std::unique_ptr<VersionAttester> version_attester_;

  // Runs the background provisioning, returns true when provisioned.
  bool BackgroundProvision();

  std::unique_ptr<ExponentialBackoff> backoff_retry_;
};

}  // namespace arc_attestation

#endif  // LIBARC_ATTESTATION_LIB_MANAGER_H_
