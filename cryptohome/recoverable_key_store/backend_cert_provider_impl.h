// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_RECOVERABLE_KEY_STORE_BACKEND_CERT_PROVIDER_IMPL_H_
#define CRYPTOHOME_RECOVERABLE_KEY_STORE_BACKEND_CERT_PROVIDER_IMPL_H_

#include "cryptohome/recoverable_key_store/backend_cert_provider.h"

#include <memory>
#include <optional>
#include <string>

#include <attestation/proto_bindings/pca_agent.pb.h>
#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <brillo/secure_blob.h>
#include <libstorage/platform/platform.h>
#include <pca_agent-client/pca_agent/dbus-proxies.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/recoverable_key_store/backend_cert_verify.h"
#include "cryptohome/recoverable_key_store/type.h"

namespace cryptohome {

class RecoverableKeyStoreBackendCertProviderImpl
    : public RecoverableKeyStoreBackendCertProvider {
 public:
  RecoverableKeyStoreBackendCertProviderImpl(
      libstorage::Platform* platform,
      std::unique_ptr<org::chromium::RksAgentProxyInterface> fetcher);

  RecoverableKeyStoreBackendCertProviderImpl(
      const RecoverableKeyStoreBackendCertProviderImpl&) = delete;
  RecoverableKeyStoreBackendCertProviderImpl& operator=(
      const RecoverableKeyStoreBackendCertProviderImpl&) = delete;

  // A random certificate from the current available list will be returned, for
  // load-balancing.
  std::optional<RecoverableKeyStoreBackendCert> GetBackendCert() const override;

 private:
  // For testing.
  friend class RecoverableKeyStoreBackendProviderPeer;

  // Initialize the backend cert provider with the certificates persisted on
  // disk.
  void InitializeWithPersistedCert();

  // Starts the fetch routine, and each fetched certs will be reported to
  // OnCertificateFetched.
  void StartFetching();

  // Callback for WaitForServiceToBeAvailable. We can start connecting to the
  // fetcher signals after the service is ready.
  void OnFetcherServiceAvailable(bool is_available);

  // Callbacks for |RegisterCertificateFetchedSignalHandler|.
  void OnCertificateFetchedSignalRegistration(const std::string& interface,
                                              const std::string& signal_name,
                                              bool success);
  void OnCertificateFetchedSignal(
      const attestation::pca_agent::RksCertificateAndSignature& reply);

  // If the certificate list fetched is newer, verify and parse the certificate.
  // If successful, update |cert_list_| and the on-disk certificates.
  void OnCertificateFetched(const std::string& cert_xml,
                            const std::string& sig_xml);

  // Persist the certificate and signature xml files to disk.
  bool PersistCertXmls(const std::string& cert_xml, const std::string& sig_xml);

  libstorage::Platform* const platform_;

  std::unique_ptr<org::chromium::RksAgentProxyInterface> fetcher_;

  const base::FilePath cert_xml_file_;
  const base::FilePath sig_xml_file_;

  // The uninitialized list will have empty |certs| vector field, and the
  // uninitialized |version| field will never be accessed in this case.
  RecoverableKeyStoreCertList cert_list_;

  base::WeakPtrFactory<RecoverableKeyStoreBackendCertProviderImpl>
      weak_factory_{this};
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_RECOVERABLE_KEY_STORE_BACKEND_CERT_PROVIDER_IMPL_H_
