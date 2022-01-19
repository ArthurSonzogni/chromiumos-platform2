// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_U2FHID_SERVICE_IMPL_H_
#define U2FD_U2FHID_SERVICE_IMPL_H_

#include "u2fd/u2fhid_service.h"

#include <functional>
#include <memory>

#include <attestation/proto_bindings/interface.pb.h>
#include <base/optional.h>
#include <brillo/dbus/dbus_method_response.h>
#include <metrics/metrics_library.h>

#include "u2fd/u2f_msg_handler.h"
#include "u2fd/u2fhid.h"
#include "u2fd/uhid_device.h"
#include "u2fd/user_state.h"

namespace u2f {

// U2F HID service. Initialized by U2F Daemon.
class U2fHidServiceImpl : public U2fHidService {
 public:
  U2fHidServiceImpl(bool legacy_kh_fallback,
                    uint32_t vendor_id,
                    uint32_t product_id);
  U2fHidServiceImpl(const U2fHidServiceImpl&) = delete;
  U2fHidServiceImpl& operator=(const U2fHidServiceImpl&) = delete;

  ~U2fHidServiceImpl() override {}

  bool InitializeDBusProxies(dbus::Bus* bus) override;

  bool CreateU2fHid(bool allow_g2f_attestation,
                    bool include_g2f_allowlisting_data,
                    std::function<void()> request_user_presence,
                    UserState* user_state,
                    MetricsLibraryInterface* metrics) override;

  // Returns a certified copy of the G2F certificate from attestationd, or
  // base::nullopt on error. The size of the G2F certificate is variable, and
  // must be specified in |g2f_cert_size|.
  base::Optional<attestation::GetCertifiedNvIndexReply> GetCertifiedG2fCert(
      int g2f_cert_size) override;

  TpmVendorCommandProxy* tpm_proxy() override { return &tpm_proxy_; }

 private:
  const bool legacy_kh_fallback_;

  // Virtual USB Device ID
  const uint32_t vendor_id_;
  const uint32_t product_id_;

  TpmVendorCommandProxy tpm_proxy_;
  dbus::ObjectProxy* attestation_proxy_;  // Not Owned.

  // Virtual USB Device
  std::unique_ptr<U2fHid> u2fhid_;
  std::unique_ptr<U2fMessageHandler> u2f_msg_handler_;
};

}  // namespace u2f

#endif  // U2FD_U2FHID_SERVICE_IMPL_H_
