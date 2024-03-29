// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <vector>

#include <gtest/gtest.h>

#include "cryptohome/fido/authenticator_data.h"

namespace cryptohome {
namespace fido_device {

// A valid registration operation response.
constexpr uint8_t kValidMakeCredentialResponse[] = {
    // clang-format off
0xA3,                                            // map(3)
0x63,                                            // text(3)
0x66, 0x6D, 0x74,                                // "fmt"
0x66,                                            // text(6)
0x70, 0x61, 0x63, 0x6B, 0x65, 0x64,              // "packed"
0x67,                                            // text(7)
0x61, 0x74, 0x74, 0x53, 0x74, 0x6D, 0x74,        // "attStmt"
0xA3,                                            // map(3)
0x63,                                            // text(3)
0x61, 0x6C, 0x67,                                // "alg"
0x26,                                            // negative(6)
0x63,                                            // text(3)
0x73, 0x69, 0x67,                                // "sig"
0x58, 0x48,                                      // bytes(72)
0x30, 0x46, 0x02, 0x21, 0x00, 0xFC, 0x4A, 0x35,
0xBF, 0x20, 0x4C, 0xE4, 0x9F, 0x24, 0xF2, 0x9C,
0x88, 0xC6, 0xCC, 0xD7, 0x13, 0x5B, 0x92, 0xA7,
0xDB, 0x6E, 0xF2, 0x3A, 0xB2, 0xD4, 0xD6, 0xAE,
0x20, 0x32, 0xA7, 0xD6, 0xAD, 0x02, 0x21, 0x00,
0xC8, 0x54, 0x4D, 0x03, 0x0E, 0x64, 0x3E, 0xE6,
0x0D, 0x1F, 0xCE, 0xC3, 0x23, 0x43, 0x9D, 0x31,
0x7B, 0x77, 0xB8, 0x51, 0x40, 0x03, 0xC1, 0xE2,
0x3E, 0x16, 0xD9, 0x3F, 0x22, 0x86, 0xD2, 0xB5,  // ...
0x63,                                            // text(3)
0x78, 0x35, 0x63,                                // "x5c"
0x81,                                            // array(1)
0x59, 0x02, 0xC0,                                // bytes(704)
0x30, 0x82, 0x02, 0xBC, 0x30, 0x82, 0x01, 0xA4,
0xA0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x04, 0x03,
0xAD, 0xF0, 0x12, 0x30, 0x0D, 0x06, 0x09, 0x2A,
0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B,
0x05, 0x00, 0x30, 0x2E, 0x31, 0x2C, 0x30, 0x2A,
0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x23, 0x59,
0x75, 0x62, 0x69, 0x63, 0x6F, 0x20, 0x55, 0x32,
0x46, 0x20, 0x52, 0x6F, 0x6F, 0x74, 0x20, 0x43,
0x41, 0x20, 0x53, 0x65, 0x72, 0x69, 0x61, 0x6C,
0x20, 0x34, 0x35, 0x37, 0x32, 0x30, 0x30, 0x36,
0x33, 0x31, 0x30, 0x20, 0x17, 0x0D, 0x31, 0x34,
0x30, 0x38, 0x30, 0x31, 0x30, 0x30, 0x30, 0x30,
0x30, 0x30, 0x5A, 0x18, 0x0F, 0x32, 0x30, 0x35,
0x30, 0x30, 0x39, 0x30, 0x34, 0x30, 0x30, 0x30,
0x30, 0x30, 0x30, 0x5A, 0x30, 0x6D, 0x31, 0x0B,
0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
0x02, 0x53, 0x45, 0x31, 0x12, 0x30, 0x10, 0x06,
0x03, 0x55, 0x04, 0x0A, 0x0C, 0x09, 0x59, 0x75,
0x62, 0x69, 0x63, 0x6F, 0x20, 0x41, 0x42, 0x31,
0x22, 0x30, 0x20, 0x06, 0x03, 0x55, 0x04, 0x0B,
0x0C, 0x19, 0x41, 0x75, 0x74, 0x68, 0x65, 0x6E,
0x74, 0x69, 0x63, 0x61, 0x74, 0x6F, 0x72, 0x20,
0x41, 0x74, 0x74, 0x65, 0x73, 0x74, 0x61, 0x74,
0x69, 0x6F, 0x6E, 0x31, 0x26, 0x30, 0x24, 0x06,
0x03, 0x55, 0x04, 0x03, 0x0C, 0x1D, 0x59, 0x75,
0x62, 0x69, 0x63, 0x6F, 0x20, 0x55, 0x32, 0x46,
0x20, 0x45, 0x45, 0x20, 0x53, 0x65, 0x72, 0x69,
0x61, 0x6C, 0x20, 0x36, 0x31, 0x37, 0x33, 0x30,
0x38, 0x33, 0x34, 0x30, 0x59, 0x30, 0x13, 0x06,
0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01,
0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03,
0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0x19, 0x9E,
0x87, 0x9C, 0x16, 0x2D, 0xB7, 0xDC, 0x39, 0xEE,
0x4A, 0x42, 0xA0, 0x46, 0x16, 0xA5, 0xB3, 0x09,
0xFE, 0xCA, 0x09, 0x2F, 0x76, 0xBE, 0x09, 0x48,
0xF9, 0x6D, 0x6E, 0x95, 0xCA, 0xE4, 0xCC, 0x65,
0xCD, 0x54, 0xA0, 0x59, 0xCF, 0xBD, 0xC7, 0xC9,
0xB3, 0x1B, 0x2B, 0x1D, 0x6C, 0x18, 0x44, 0x79,
0xC2, 0xC0, 0x61, 0xF4, 0x18, 0xAA, 0x95, 0x4B,
0x59, 0x6A, 0x2C, 0x1C, 0xFA, 0x17, 0xA3, 0x6C,
0x30, 0x6A, 0x30, 0x22, 0x06, 0x09, 0x2B, 0x06,
0x01, 0x04, 0x01, 0x82, 0xC4, 0x0A, 0x02, 0x04,
0x15, 0x31, 0x2E, 0x33, 0x2E, 0x36, 0x2E, 0x31,
0x2E, 0x34, 0x2E, 0x31, 0x2E, 0x34, 0x31, 0x34,
0x38, 0x32, 0x2E, 0x31, 0x2E, 0x37, 0x30, 0x13,
0x06, 0x0B, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82,
0xE5, 0x1C, 0x02, 0x01, 0x01, 0x04, 0x04, 0x03,
0x02, 0x04, 0x30, 0x30, 0x21, 0x06, 0x0B, 0x2B,
0x06, 0x01, 0x04, 0x01, 0x82, 0xE5, 0x1C, 0x01,
0x01, 0x04, 0x04, 0x12, 0x04, 0x10, 0xFA, 0x2B,
0x99, 0xDC, 0x9E, 0x39, 0x42, 0x57, 0x8F, 0x92,
0x4A, 0x30, 0xD2, 0x3C, 0x41, 0x18, 0x30, 0x0C,
0x06, 0x03, 0x55, 0x1D, 0x13, 0x01, 0x01, 0xFF,
0x04, 0x02, 0x30, 0x00, 0x30, 0x0D, 0x06, 0x09,
0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01,
0x0B, 0x05, 0x00, 0x03, 0x82, 0x01, 0x01, 0x00,
0x28, 0xEB, 0xB3, 0x67, 0xFE, 0xD1, 0xD8, 0xF0,
0xE2, 0x89, 0xEB, 0xCA, 0x9F, 0xF6, 0xD8, 0x07,
0x57, 0xC6, 0x0E, 0x9A, 0xE5, 0x7C, 0xB1, 0x72,
0x8C, 0x9B, 0x1C, 0x38, 0xCA, 0xBB, 0xBD, 0x84,
0xD9, 0x23, 0x7D, 0xA8, 0x31, 0xAC, 0x21, 0x94,
0x9F, 0x0F, 0x2D, 0xFC, 0x0C, 0x31, 0x6B, 0xFD,
0xB1, 0x75, 0xB3, 0x6E, 0x63, 0xA2, 0x2B, 0xBB,
0x58, 0x0E, 0xAD, 0xCA, 0x52, 0x80, 0xD0, 0x79,
0x84, 0x0E, 0x5A, 0x1E, 0x25, 0x72, 0x62, 0x5A,
0x3B, 0xFB, 0x87, 0x60, 0x33, 0xDB, 0xFB, 0x22,
0xA9, 0x69, 0xC9, 0x38, 0xB8, 0x9C, 0xE1, 0x71,
0x35, 0x94, 0x00, 0xA1, 0x25, 0x2D, 0x97, 0x02,
0xA9, 0x12, 0x93, 0xD5, 0x45, 0x19, 0xE9, 0x60,
0xDD, 0x22, 0xCE, 0x8A, 0x27, 0xEB, 0x05, 0xEB,
0x7E, 0x79, 0xB7, 0x50, 0xC0, 0x02, 0xFE, 0xD9,
0x01, 0x6B, 0x71, 0x1E, 0xC9, 0xAD, 0x74, 0x50,
0x1B, 0xD9, 0x14, 0xCB, 0xBE, 0x8E, 0xD9, 0x57,
0x12, 0x81, 0xB7, 0x4F, 0x44, 0xEB, 0x07, 0x7C,
0xE6, 0x1E, 0xCB, 0x06, 0xAB, 0x85, 0xA9, 0x72,
0x55, 0x26, 0x7E, 0xE8, 0xE3, 0x98, 0x2B, 0xF4,
0x3F, 0x0C, 0xB2, 0x1A, 0x38, 0x2D, 0x23, 0x5E,
0xB9, 0xE4, 0xCE, 0x6D, 0xB2, 0x98, 0xC4, 0x05,
0x42, 0x50, 0x40, 0x23, 0x2B, 0x2B, 0x61, 0xE1,
0x0C, 0xD7, 0x0C, 0x62, 0x15, 0xBC, 0x03, 0xB7,
0xE9, 0x40, 0x71, 0xB7, 0x0E, 0x12, 0xD1, 0xC4,
0x7F, 0x96, 0x65, 0x5A, 0x2E, 0xF9, 0x9D, 0x4C,
0xE5, 0x5A, 0x7F, 0x1B, 0x4B, 0x1F, 0xF9, 0x14,
0xEE, 0x13, 0x6D, 0x9E, 0x61, 0x20, 0x47, 0x14,
0x88, 0x64, 0x69, 0x88, 0x80, 0x44, 0x31, 0x16,
0x65, 0x38, 0x89, 0xB8, 0x64, 0x86, 0xD9, 0xC9,
0xC9, 0xFF, 0xBC, 0x93, 0x85, 0x45, 0x35, 0x69,
0xB3, 0x45, 0x74, 0x4B, 0x8C, 0xA0, 0xB4, 0x37,  // ...
0x68,                                            // text(8)
0x61, 0x75, 0x74, 0x68, 0x44, 0x61, 0x74, 0x61,  // "authData"
0x58, 0xC4,                                      // bytes(196)
0xC4, 0x6C, 0xEF, 0x82, 0xAD, 0x1B, 0x54, 0x64,
0x77, 0x59, 0x1D, 0x00, 0x8B, 0x08, 0x75, 0x9E,
0xC3, 0xE6, 0xD2, 0xEC, 0xB4, 0xF3, 0x94, 0x74,
0xBF, 0xEA, 0x69, 0x69, 0x92, 0x5D, 0x03, 0xB7,
0x41, 0x00, 0x00, 0x00, 0x06, 0xFA, 0x2B, 0x99,
0xDC, 0x9E, 0x39, 0x42, 0x57, 0x8F, 0x92, 0x4A,
0x30, 0xD2, 0x3C, 0x41, 0x18, 0x00, 0x40, 0xFA,
0x6A, 0x13, 0x57, 0x66, 0x88, 0x59, 0x2B, 0x91,
0xAE, 0x53, 0x17, 0x87, 0xD4, 0x50, 0xB3, 0x02,
0x67, 0x7F, 0x70, 0x48, 0x8D, 0x83, 0xA1, 0xD1,
0xF2, 0x11, 0xE2, 0x6E, 0x46, 0xF7, 0xED, 0x41,
0x5C, 0x68, 0xF5, 0xBD, 0x1E, 0xC1, 0x54, 0x86,
0xA9, 0x7D, 0x85, 0xCB, 0x0A, 0x6C, 0xEB, 0x3E,
0xF1, 0xFC, 0xFD, 0x76, 0x73, 0xBD, 0xB0, 0x61,
0x4B, 0x89, 0x39, 0xA0, 0x9E, 0x2D, 0x7B, 0xA5,
0x01, 0x02, 0x03, 0x26, 0x20, 0x01, 0x21, 0x58,
0x20, 0xF8, 0xE3, 0x9C, 0xD2, 0x04, 0x84, 0x28,
0xAC, 0x06, 0x9B, 0xB4, 0x98, 0x63, 0x4F, 0x47,
0xD1, 0x38, 0xDD, 0x48, 0xD5, 0x6D, 0xC1, 0x86,
0xF5, 0xE9, 0x3A, 0x10, 0xAD, 0xD8, 0x4F, 0xCE,
0x42, 0x22, 0x58, 0x20, 0xC0, 0xD3, 0x05, 0x7E,
0x9F, 0xBB, 0x4D, 0xDA, 0xE4, 0x01, 0x80, 0x23,
0x61, 0x81, 0xFE, 0xBB, 0x45, 0xBB, 0xBC, 0xB6,
0x92, 0x15, 0x47, 0x7A, 0x94, 0xB9, 0xE4, 0x1F,
0x39, 0xAF, 0xC1, 0xF5                           // ...
    // clang-format on
};

TEST(ParserTest, MakeCredentialResponseTest) {
  std::vector<uint8_t> input(std::begin(kValidMakeCredentialResponse),
                             std::end(kValidMakeCredentialResponse));
  auto attested_credential_data =
      AuthenticatorData::ParseMakeCredentialResponse(input);
  EXPECT_TRUE(attested_credential_data.has_value());
}

}  // namespace fido_device
}  // namespace cryptohome
