// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_TPM_VENDOR_CMD_H_
#define U2FD_TPM_VENDOR_CMD_H_

#include <string>

#include <base/macros.h>

#include "trunks/trunks_dbus_proxy.h"

namespace u2f {

// The TPM response code is all zero for success.
// Errors are a little complicated:
//
//   Bits 31:12 must be zero.
//
//   Bit 11     S=0   Error
//   Bit 10     T=1   Vendor defined response code
//   Bit  9     r=0   reserved
//   Bit  8     V=1   Conforms to TPMv2 spec
//   Bit  7     F=0   Conforms to Table 14, Format-Zero Response Codes
//   Bits 6:0   num   128 possible failure reasons
const uint32_t kVendorRcErr = 0x00000500;
// Command not implemented on the firmware side.
const uint32_t kVendorRcNoSuchCommand = kVendorRcErr | 0x7f;

// TpmVendorCommandProxy sends vendor commands to the TPM security chip
// by using the D-Bus connection to the trunksd daemon which communicates
// with the physical TPM through the kernel driver exposing /dev/tpm0.
class TpmVendorCommandProxy : public trunks::TrunksDBusProxy {
 public:
  TpmVendorCommandProxy();
  ~TpmVendorCommandProxy() override;

  // Sends the VENDOR_CC_U2F_APDU command to the TPM with |req| as the
  // ISO7816-4:2005 APDU data and writes in |resp| sent back by the TPM.
  // Returns the TPM response code.
  int SendU2fApdu(const std::string& req, std::string* resp_out);

  // Sets the operating mode of the U2F feature in the TPM.
  // Returns the TPM response code.
  int SetU2fVendorMode(uint8_t mode);

  // Reads the TPM firmware U2F protocol implementation in |version|
  // by sending a U2F_VERSION APDU encapsulated in a TPM vendor commands.
  // Returns the TPM response code.
  int GetU2fVersion(std::string* version_out);

 private:
  // Sends the TPM command with vendor-specific command code |cc| and the
  // payload in |input|, get the reply in |output|. Returns the TPM response
  // code.
  uint32_t VendorCommand(uint16_t cc,
                         const std::string& input,
                         std::string* output);

  // Retrieve and record in the log the individual attestation certificate.
  void LogIndividualCertificate();

  DISALLOW_COPY_AND_ASSIGN(TpmVendorCommandProxy);
};

}  // namespace u2f

#endif  // U2FD_TPM_VENDOR_CMD_H_
