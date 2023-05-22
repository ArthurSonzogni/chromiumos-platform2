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
#include <libarc-attestation/lib/interface.h>
#include <libarc-attestation/lib/provisioner.h>
#include <libarc-attestation/lib/version_attester.h>

namespace arc_attestation {

// ArcAttestationManager is a singleton class that service all API calls.
class ArcAttestationManager {
 public:
  ArcAttestationManager();

  // Setup the threading and other variables.
  void Setup();

  // Provision the Device Key certificate chain.
  AndroidStatus ProvisionDkCert(bool blocking);

  // Retrieve the Device Key certs.
  AndroidStatus GetDkCertChain(std::vector<brillo::Blob>& cert_out);

  // Sign with the device key.
  AndroidStatus SignWithP256Dk(const brillo::Blob& input,
                               brillo::Blob& signature);

  // Quote the ChromeOS specific blob for ARC Attestation.
  AndroidStatus QuoteCrOSBlob(const brillo::Blob& challenge,
                              brillo::Blob& output);

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
};

class ArcAttestationManagerSingleton {
 public:
  // Retrieve the singleton.
  // This is not thread safe and should only be accessed from the API functions
  // because the caller is required to call these functions without
  // overlapping.
  static ArcAttestationManagerSingleton* Get() {
    if (g_instance == nullptr) {
      g_instance = new ArcAttestationManagerSingleton();
      g_instance->Setup();
    }
    return g_instance;
  }

  static ArcAttestationManagerSingleton* CreateForTesting() {
    CHECK(!g_instance);
    g_instance = new ArcAttestationManagerSingleton();
    return g_instance;
  }

  static void DestroyForTesting() {
    if (g_instance) {
      delete g_instance;
      g_instance = nullptr;
    }
  }

  void SetManagerForTesting(std::unique_ptr<ArcAttestationManager> manager) {
    manager_ = std::move(manager);
  }

  ArcAttestationManager* manager() { return manager_.get(); }

 private:
  // The global singleton instance of attestation interface.
  static ArcAttestationManagerSingleton* g_instance;

  // Private constructor because it's singleton.
  ArcAttestationManagerSingleton();

  // Setup the manager.
  void Setup();

  // The manager instance.
  std::unique_ptr<ArcAttestationManager> manager_;
};

}  // namespace arc_attestation

#endif  // LIBARC_ATTESTATION_LIB_MANAGER_H_
