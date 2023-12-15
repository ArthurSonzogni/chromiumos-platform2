// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/recoverable_key_store/backend_cert_provider_impl.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/rand_util.h>
#include <brillo/secure_blob.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/platform.h"
#include "cryptohome/recoverable_key_store/type.h"

namespace cryptohome {

namespace {
constexpr char kCertXmlFile[] = "cert.xml";
constexpr char kSignatureXmlFile[] = "sig.xml";
}  // namespace

RecoverableKeyStoreBackendCertProviderImpl::
    RecoverableKeyStoreBackendCertProviderImpl(Platform* platform)
    : platform_(platform),
      cert_xml_file_(RecoverableKeyStoreBackendCertDir().Append(kCertXmlFile)),
      sig_xml_file_(
          RecoverableKeyStoreBackendCertDir().Append(kSignatureXmlFile)) {
  CHECK(platform_);
  InitializeWithPersistedCert();
  StartFetching(base::BindRepeating(
      &RecoverableKeyStoreBackendCertProviderImpl::OnCertificateFetched,
      weak_factory_.GetWeakPtr()));
}

std::optional<RecoverableKeyStoreBackendCert>
RecoverableKeyStoreBackendCertProviderImpl::GetBackendCert() const {
  if (cert_list_.certs.empty()) {
    return std::nullopt;
  }
  size_t index =
      static_cast<size_t>(base::RandGenerator(cert_list_.certs.size()));
  return RecoverableKeyStoreBackendCert{
      .version = cert_list_.version,
      .public_key = cert_list_.certs[index].public_key,
  };
}

void RecoverableKeyStoreBackendCertProviderImpl::InitializeWithPersistedCert() {
  std::string cert_xml, sig_xml;
  if (!platform_->ReadFileToString(cert_xml_file_, &cert_xml)) {
    return;
  }
  if (!platform_->ReadFileToString(sig_xml_file_, &sig_xml)) {
    LOG(WARNING) << "cert.xml exists, but sig.xml doesn't.";
    return;
  }
  std::optional<RecoverableKeyStoreCertList> cert_list =
      VerifyAndParseRecoverableKeyStoreBackendCertXmls(cert_xml, sig_xml);
  if (!cert_list.has_value()) {
    LOG(ERROR)
        << "Failed to verify the key store backend certificates on disk.";
    return;
  }
  cert_list_ = std::move(*cert_list);
}

void RecoverableKeyStoreBackendCertProviderImpl::StartFetching(
    base::RepeatingCallback<void(const std::string& cert_xml,
                                 const std::string& sig_xml)> on_cert_fetched) {
  // TODO(b/309734008): Implement certificate fetching.
}

void RecoverableKeyStoreBackendCertProviderImpl::OnCertificateFetched(
    const std::string& cert_xml, const std::string& sig_xml) {
  if (!cert_list_.certs.empty()) {
    std::optional<uint64_t> version = GetCertXmlVersion(cert_xml);
    if (!version.has_value()) {
      LOG(ERROR) << "Failed to parse version of the fetched certificate.";
      return;
    }
    if (cert_list_.version >= *version) {
      LOG(INFO) << "Version of fetched certificate isn't newer, so update "
                   "isn't necessary.";
      return;
    }
  }
  std::optional<RecoverableKeyStoreCertList> cert_list =
      VerifyAndParseRecoverableKeyStoreBackendCertXmls(cert_xml, sig_xml);
  if (!cert_list.has_value()) {
    LOG(ERROR) << "Failed to parse and verify the fetched certificate.";
    return;
  }
  if (!PersistCertXmls(cert_xml, sig_xml)) {
    LOG(ERROR) << "Failed to persist fetched certificates on disk.";
    return;
  }
  LOG(INFO)
      << "Recoverable key store backend certificate list updated to version "
      << cert_list->version << ".";
  cert_list_ = std::move(*cert_list);
}

bool RecoverableKeyStoreBackendCertProviderImpl::PersistCertXmls(
    const std::string& cert_xml, const std::string& sig_xml) {
  if (!platform_->WriteStringToFile(cert_xml_file_, cert_xml)) {
    return false;
  }
  if (!platform_->WriteStringToFile(sig_xml_file_, sig_xml)) {
    return false;
  }
  return true;
}

}  // namespace cryptohome
