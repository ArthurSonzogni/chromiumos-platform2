// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_TPM_VENDOR_CMD_H_
#define U2FD_TPM_VENDOR_CMD_H_

#include <string>

#include <base/macros.h>
#include <trunks/cr50_headers/u2f.h>
#include <trunks/trunks_dbus_proxy.h>

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
// Response was invalid (TPM response code was not available).
const uint32_t kVendorRcInvalidResponse = 0xffffffff;

// TpmVendorCommandProxy sends vendor commands to the TPM security chip
// by using the D-Bus connection to the trunksd daemon which communicates
// with the physical TPM through the kernel driver exposing /dev/tpm0.
class TpmVendorCommandProxy : public trunks::TrunksDBusProxy {
 public:
  TpmVendorCommandProxy();
  ~TpmVendorCommandProxy() override;

  // Sets the operating mode of the U2F feature in the TPM.
  // Returns the TPM response code.
  uint32_t SetU2fVendorMode(uint8_t mode);

  // Queries the TPM firmware if it has vendor system information available
  // for the U2F feature and returns it in |sysinfo_out| if it does.
  void GetVendorSysInfo(std::string* sysinfo_out);

  // Sends the VENDOR_CC_U2F_GENERATE command to cr50, and populates
  // resp_out with the reply.
  // Returns the TPM response code, or kVendorRcInvalidResponse if the
  // response was invalid.
  uint32_t SendU2fGenerate(const U2F_GENERATE_REQ& req,
                           U2F_GENERATE_RESP* resp_out);

  // Sends the VENDOR_CC_U2F_SIGN command to cr50, and populates
  // resp_out with the reply.
  // If U2F_SIGN_REQ specifies flags indicating a 'check-only' request,
  // no response body will be returned from cr50, and so resp_out will
  // not be populated. In this case resp_out may be set to nullptr.
  // Returns the TPM response code, or kVendorRcInvalidResponse if the
  // response was invalid.
  uint32_t SendU2fSign(const U2F_SIGN_REQ& req, U2F_SIGN_RESP* resp_out);

  // Sends the VENDOR_CC_U2F_ATTEST command to cr50, and populates
  // resp_out with the reply.
  // Returns the TPM response code, or kVendorRcInvalidResponse if the
  // response was invalid.
  uint32_t SendU2fAttest(const U2F_ATTEST_REQ& req, U2F_ATTEST_RESP* resp_out);

  // Retrieves the G2F certificate from vNVRAM in cr50 and writes it to
  // cert_out. Note that the certificate read from vNVRAM may include
  // several '0' bytes of padding at the end of the buffer. The length
  // of the certificate can be determined by parsing it.
  // Returns the TPM response code, or kVendorRcInvalidResponse if the
  // response was invalid.
  uint32_t GetG2fCertificate(std::string* cert_out);

 private:
  // Sends the TPM command with vendor-specific command code |cc| and the
  // payload in |input|, get the reply in |output|. Returns the TPM response
  // code.
  uint32_t VendorCommand(uint16_t cc,
                         const std::string& input,
                         std::string* output);

  // Sends the TPM command with vendor-specific command code |cc| and the
  // payload in |input|, get the reply in |output|. Returns the TPM response
  // code, or kVendorRcInvalidResponse if the response code was
  // TPM_RC_SUCCESS but the response was the wrong length for the specified
  // output type.
  template <typename Request, typename Response>
  uint32_t VendorCommandStruct(uint16_t cc,
                               const Request& input,
                               Response* output);

  // Sends the VENDOR_CC_U2F_APDU command to the TPM with |req| as the
  // ISO7816-4:2005 APDU data and writes in |resp| sent back by the TPM.
  // Returns the TPM response code.
  uint32_t SendU2fApdu(const std::string& req, std::string* resp_out);

  // Retrieve and record in the log the individual attestation certificate.
  void LogIndividualCertificate();

  // Cr50 loses U2F state during deep sleep; this function sends a command
  // that will re-load it. Returns true iff state was successfully reloaded.
  bool ReloadCr50State();

  // Mode set on the most recent call to SetU2fVendorMode();
  uint8_t last_u2f_vendor_mode_;

  DISALLOW_COPY_AND_ASSIGN(TpmVendorCommandProxy);
};

}  // namespace u2f

#endif  // U2FD_TPM_VENDOR_CMD_H_
