// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_U2FHID_SERVICE_H_
#define U2FD_U2FHID_SERVICE_H_

#include <functional>

#include <attestation/proto_bindings/interface.pb.h>
#include <base/optional.h>
#include <brillo/dbus/dbus_method_response.h>
#include <metrics/metrics_library.h>

#include "u2fd/user_state.h"

namespace u2f {

class TpmVendorCommandProxy;
class U2fHid;

// U2F HID service. Initialized by U2F Daemon.
class U2fHidService {
 public:
  virtual ~U2fHidService() = default;

  virtual bool InitializeDBusProxies(dbus::Bus* bus) = 0;

  virtual bool CreateU2fHid(bool allow_g2f_attestation,
                            bool include_g2f_allowlisting_data,
                            std::function<void()> request_user_presence,
                            UserState* user_state,
                            MetricsLibraryInterface* metrics) = 0;

  // Returns a certified copy of the G2F certificate from attestationd, or
  // base::nullopt on error. The size of the G2F certificate is variable, and
  // must be specified in |g2f_cert_size|.
  virtual base::Optional<attestation::GetCertifiedNvIndexReply>
  GetCertifiedG2fCert(int g2f_cert_size) = 0;

  virtual TpmVendorCommandProxy* tpm_proxy() = 0;
};

}  // namespace u2f

#endif  // U2FD_U2FHID_SERVICE_H_
