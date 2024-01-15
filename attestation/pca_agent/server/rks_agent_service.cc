// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/pca_agent/server/rks_agent_service.h"

#include <attestation/proto_bindings/interface.pb.h>
#include <brillo/dbus/dbus_object.h>

#include "attestation/pca-agent/dbus_adaptors/org.chromium.PcaAgent.h"

namespace attestation {
namespace pca_agent {

RksAgentService::RksAgentService() : org::chromium::RksAgentAdaptor(this) {}

void RksAgentService::GetCertificate(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<RksCertificateAndSignature>>
        response) {
  // TODO(b/320220928): Implement recoverable key store certificate fetching.
  response->Return(RksCertificateAndSignature());
}

}  // namespace pca_agent
}  // namespace attestation
