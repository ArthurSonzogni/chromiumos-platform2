// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_U2F_CORP_PROCESSOR_INTERFACE_H_
#define U2FD_U2F_CORP_PROCESSOR_INTERFACE_H_

#include <functional>

#include <session_manager/dbus-proxies.h>

#include "u2fd/client/tpm_vendor_cmd.h"
#include "u2fd/client/u2f_apdu.h"
#include "u2fd/client/u2f_corp_firmware_version.h"
#include "u2fd/client/u2f_corp_processor.h"

namespace u2f {

// Processes incoming Corp-specific protocol messages, and produces
// corresponding responses.
class U2fCorpProcessorInterface {
 public:
  U2fCorpProcessorInterface();
  U2fCorpProcessorInterface(const U2fCorpProcessorInterface&) = delete;
  U2fCorpProcessorInterface& operator=(const U2fCorpProcessorInterface&) =
      delete;
  ~U2fCorpProcessorInterface();

  // The corp processor needs to connect to session manager signal, and send
  // commands through the tpm proxy.
  void Initialize(U2fCorpFirmwareVersion fw_version,
                  org::chromium::SessionManagerInterfaceProxy* sm_proxy,
                  TpmVendorCommandProxy* tpm_proxy,
                  std::function<void()> request_presence);
  U2fResponseApdu ProcessApdu(const U2fCommandApdu& apdu);
  void Reset();

 private:
  void* handle_;
  U2fCorpProcessor* processor_;
};

}  // namespace u2f

#endif  // U2FD_U2F_CORP_PROCESSOR_INTERFACE_H_
