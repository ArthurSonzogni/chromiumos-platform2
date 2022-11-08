// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mojom/mojo_passpoint_service.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "mojom/passpoint.mojom.h"
#include "shill/certificate_file.h"
#include "shill/manager.h"
#include "shill/wifi/passpoint_credentials.h"
#include "shill/wifi/wifi_provider.h"

namespace shill {

constexpr char kPEMHeader[] = "-----BEGIN CERTIFICATE-----";
constexpr char kPEMFooter[] = "-----END CERTIFICATE-----";

MojoPasspointService::MojoPasspointService(Manager* manager)
    : manager_(manager) {}

MojoPasspointService::~MojoPasspointService() = default;

void MojoPasspointService::GetPasspointSubscription(
    const std::string& id, GetPasspointSubscriptionCallback callback) {
  WiFiProvider* provider = manager_->wifi_provider();
  CHECK(provider);

  PasspointCredentialsRefPtr creds = provider->FindCredentials(id);
  if (!creds) {
    LOG(WARNING) << "Credentials " << id << " not found";
    std::move(callback).Run(nullptr);
    return;
  }

  std::string ca_pem;
  if (!creds->eap().ca_cert_pem().empty()) {
    std::string content = CertificateFile::ExtractHexData(
        base::JoinString(creds->eap().ca_cert_pem(), "\n"));
    ca_pem = base::StringPrintf("%s\n%s\n%s\n", kPEMHeader, content.c_str(),
                                kPEMFooter);
  }

  std::move(callback).Run(
      chromeos::connectivity::mojom::PasspointSubscription::New(
          creds->id(), creds->domains(), creds->friendly_name(),
          creds->android_package_name(), ca_pem));
}

}  // namespace shill
