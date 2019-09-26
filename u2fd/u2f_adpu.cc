// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <map>
#include <utility>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <trunks/cr50_headers/u2f.h>

#include "u2fd/u2f_adpu.h"
#include "u2fd/util.h"

namespace u2f {

namespace {

// All U2F ADPUs have a CLA value of 0.
constexpr uint8_t kAdpuCla = 0;

// Chrome sends a REGISTER message with the following bogus app ID
// and challenge parameters to cause USB devices to flash their
// LED.

const std::array<uint8_t, 32> kChromeBogusAppId = {
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41};

const std::array<uint8_t, 32> kChromeBogusChallenge = {
    0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
    0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
    0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42};

}  // namespace

//
// U2fCommandAdpu Implementation.
//
//////////////////////////////////////////////////////////////////////

// Parses raw ADPU strings.
class U2fCommandAdpu::Parser {
 public:
  explicit Parser(const std::string& adpu_raw)
      : adpu_raw_(adpu_raw), pos_(adpu_raw.cbegin()) {}

  base::Optional<U2fCommandAdpu> Parse(uint16_t* u2f_status) {
    if (ParseHeader(u2f_status) && ParseLc() && ParseBody() && ParseLe()) {
      return adpu_;
    } else {
      VLOG(2) << "Failed to parse ADPU: "
              << base::HexEncode(adpu_raw_.data(), adpu_raw_.size());
      return base::nullopt;
    }
  }

 private:
  bool ParseHeader(uint16_t* u2f_status) {
    static constexpr uint8_t kAdpuHeaderSize = 4;

    if (Remaining() < kAdpuHeaderSize) {
      return false;
    }

    if (Consume() != kAdpuCla) {
      if (u2f_status) {
        *u2f_status = U2F_SW_CLA_NOT_SUPPORTED;
      }
      return false;
    }

    // We checked we have enough data left, so these will not fail.
    adpu_.header_.ins_ = static_cast<U2fIns>(Consume());
    adpu_.header_.p1_ = Consume();
    adpu_.header_.p2_ = Consume();

    return true;
  }

  bool ParseLc() {
    lc_ = 0;

    // No Lc.
    if (Remaining() == 0)
      return true;

    lc_ = Consume();

    if (lc_ == 0 && Remaining() > 2) {
      // Extended Lc.
      lc_ = Consume() << 8;
      lc_ |= Consume();
    }

    return true;
  }

  bool ParseBody() {
    if (lc_ == 0)
      return true;

    if (Remaining() < lc_)
      return false;

    adpu_.data_.append(pos_, pos_ + lc_);
    pos_ += lc_;

    return true;
  }

  bool ParseLe() {
    if (Remaining() == 0) {
      adpu_.max_response_length_ = 0;
      return true;
    }

    adpu_.max_response_length_ = Consume();

    if (Remaining() > 0) {
      adpu_.max_response_length_ = adpu_.max_response_length_ << 8 | Consume();
      if (adpu_.max_response_length_ == 0)
        adpu_.max_response_length_ = 65536;
    }

    return true;
  }

  uint8_t Consume() {
    uint8_t val = *pos_;
    ++pos_;
    return val;
  }

  size_t Remaining() { return adpu_raw_.cend() - pos_; }

  const std::string& adpu_raw_;
  std::string::const_iterator pos_;

  uint16_t lc_;

  U2fCommandAdpu adpu_;
};

base::Optional<U2fCommandAdpu> U2fCommandAdpu::ParseFromString(
    const std::string& adpu_raw, uint16_t* u2f_status) {
  *u2f_status = 0;
  return U2fCommandAdpu::Parser(adpu_raw).Parse(u2f_status);
}

U2fCommandAdpu U2fCommandAdpu::CreateForU2fIns(U2fIns ins) {
  U2fCommandAdpu adpu;
  adpu.header_.ins_ = ins;
  return adpu;
}

namespace {

void AppendLc(std::string* adpu, size_t lc) {
  if (lc == 0)
    return;

  if (lc < 256) {
    adpu->append(1, lc);
  } else {
    adpu->append(1, lc >> 8);
    adpu->append(1, lc & 0xff);
  }
}

void AppendLe(std::string* adpu, size_t lc, size_t le) {
  if (le == 0)
    return;

  if (le < 256) {
    adpu->append(1, le);
  } else if (le == 256) {
    adpu->append(1, 0);
  } else {
    if (lc == 0)
      adpu->append(1, 0);

    if (le == 65536)
      le = 0;

    adpu->append(1, le >> 8);
    adpu->append(1, le & 0xff);
  }
}

}  // namespace

std::string U2fCommandAdpu::ToString() const {
  std::string adpu;

  adpu.push_back(kAdpuCla);
  adpu.push_back(static_cast<uint8_t>(header_.ins_));
  adpu.push_back(header_.p1_);
  adpu.push_back(header_.p2_);

  AppendLc(&adpu, data_.size());

  adpu.append(data_);

  AppendLe(&adpu, data_.size(), max_response_length_);

  return adpu;
}

//
// Helper for parsing U2F command ADPU request body.
//
//////////////////////////////////////////////////////////////////////

bool ParseAdpuBody(
    const std::string& body,
    std::map<std::pair<int, int>, std::vector<uint8_t>*> fields) {
  for (const auto& field : fields) {
    int field_start = field.first.first;
    int field_length = field.first.second;

    if (field_start < 0 || (field_start + field_length) > body.size())
      return false;

    util::AppendSubstringToVector(body, field_start, field_length,
                                  field.second);
  }
  return true;
}

//
// U2fRegisterRequestAdpu Implementation.
//
//////////////////////////////////////////////////////////////////////

base::Optional<U2fRegisterRequestAdpu> U2fRegisterRequestAdpu::FromCommandAdpu(
    const U2fCommandAdpu& adpu, uint16_t* u2f_status) {
  // Request body for U2F_REGISTER ADPUs are in the following format:
  //
  // Byte(s)  | Description
  // --------------------------
  //  0 - 31  | Challenge
  // 32 - 64  | App ID

  *u2f_status = 0;

  U2fRegisterRequestAdpu reg_adpu;
  if (!ParseAdpuBody(adpu.Body(), {{{0, 32}, &reg_adpu.challenge_},
                                   {{32, 32}, &reg_adpu.app_id_}})) {
    LOG(INFO) << "Received invalid U2F_REGISTER ADPU: "
              << base::HexEncode(adpu.Body().data(), adpu.Body().size());
    if (u2f_status) {
      *u2f_status = U2F_SW_WRONG_LENGTH;
    }
    return base::nullopt;
  }

  // We require that P1 be set to 0x03 (though may optionally have the
  // G2F_ATTEST bit set), implying a test of user presence, and that presence
  // should be consumed.
  if ((adpu.P1() & ~G2F_ATTEST) != U2F_AUTH_ENFORCE) {
    LOG(INFO) << "Received register APDU with invalid P1 value: " << std::hex
              << adpu.P1();
    return base::nullopt;
  }

  reg_adpu.g2f_attestation_ = adpu.P1() & G2F_ATTEST;

  return reg_adpu;
}

bool U2fRegisterRequestAdpu::IsChromeDummyWinkRequest() const {
  return std::equal(app_id_.begin(), app_id_.end(), kChromeBogusAppId.begin(),
                    kChromeBogusAppId.end()) &&
         std::equal(challenge_.begin(), challenge_.end(),
                    kChromeBogusChallenge.begin(), kChromeBogusChallenge.end());
}

//
// U2fAuthenticateRequest Implementation.
//
//////////////////////////////////////////////////////////////////////

base::Optional<U2fAuthenticateRequestAdpu>
U2fAuthenticateRequestAdpu::FromCommandAdpu(const U2fCommandAdpu& adpu,
                                            uint16_t* u2f_status) {
  *u2f_status = 0;

  // The P1 field must be set to a value of 0x03 or 0x07, indicating
  // respectively a request to authenticate with user presence, or a request
  // merely trying to determine whether the key handle is owned by this U2F
  // device, in which case no user presence is required and authentication
  // should not be performed.
  if (adpu.P1() != U2F_AUTH_ENFORCE && adpu.P1() != U2F_AUTH_CHECK_ONLY) {
    LOG(INFO) << "Received authenticate APDU with invalid P1 value: "
              << std::hex << adpu.P1();
    return base::nullopt;
  }

  // Request body for U2F_AUTHENTICATE ADPUs are in the following format:
  //
  // Byte(s)  | Description
  // --------------------------
  //  0 - 31  | Challenge
  // 32 - 63  | App ID
  // 64       | Key Handle Length
  // 65 - end | Key Handle
  //
  constexpr int kAdpuFixedFieldsSize = 65;
  int body_size = adpu.Body().size();
  int kh_length = body_size - kAdpuFixedFieldsSize;

  U2fAuthenticateRequestAdpu auth_adpu;
  if (body_size < kAdpuFixedFieldsSize || kh_length != adpu.Body()[64] ||
      !ParseAdpuBody(adpu.Body(),
                     {{{0, 32}, &auth_adpu.challenge_},
                      {{32, 32}, &auth_adpu.app_id_},
                      {{65, kh_length}, &auth_adpu.key_handle_}})) {
    LOG(INFO) << "Received invalid U2F_AUTHENTICATE ADPU: "
              << base::HexEncode(adpu.Body().data(), adpu.Body().size());
    if (u2f_status) {
      *u2f_status = U2F_SW_WRONG_LENGTH;
    }
    return base::nullopt;
  }

  auth_adpu.auth_check_only_ = adpu.P1() == U2F_AUTH_CHECK_ONLY;

  return auth_adpu;
}

//
// U2fResponseAdpu Implementation.
//
//////////////////////////////////////////////////////////////////////

bool U2fResponseAdpu::ToString(std::string* out) const {
  out->append(data_.begin(), data_.end());
  out->push_back(sw1_);
  out->push_back(sw2_);
  return true;
}

}  // namespace u2f
