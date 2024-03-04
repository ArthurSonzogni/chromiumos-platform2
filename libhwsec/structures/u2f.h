// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_STRUCTURES_U2F_H_
#define LIBHWSEC_STRUCTURES_U2F_H_

#include <memory>

#include <base/containers/span.h>
#include <brillo/secure_blob.h>

#include "libhwsec/hwsec_export.h"
#include "libhwsec/structures/no_default_init.h"

namespace hwsec {
namespace u2f {

class PublicKey {
 public:
  virtual ~PublicKey() = default;

  virtual base::span<const uint8_t> x() const = 0;
  virtual base::span<const uint8_t> y() const = 0;
  virtual const brillo::Blob& raw() const = 0;
};

struct GenerateResult {
  std::unique_ptr<PublicKey> public_key;
  NoDefault<brillo::Blob> key_handle;
};

struct Signature {
  NoDefault<brillo::Blob> r;
  NoDefault<brillo::Blob> s;
};

enum class ConsumeMode : bool {
  kNoConsume,
  kConsume,
};

enum class UserPresenceMode : bool {
  kNotRequired,
  kRequired,
};

struct Config {
  size_t up_only_kh_size;
  size_t kh_size;
};

enum class FipsStatus : bool {
  kNotActive = false,
  kActive = true,
};

// FIPS 140-2 defines four levels of security, simply named "Level 1" to "Level
// 4".
enum class FipsCertificationStatus : uint8_t {
  kNotCertified = 0,
  kLevel1 = 1,
  kLevel2 = 2,
  kLevel3 = 3,
  kLevel4 = 4,
};

// Note that the description refers to "hardware" and "software" but in our
// case, both physical and logical certification status are associated with the
// GSC. For example, cr50's U2F library certification status is L1+L3 physical.
struct FipsCertificationLevel {
  // Hardware FIPS level.
  FipsCertificationStatus physical_certification_status;
  // Software FIPS level.
  FipsCertificationStatus logical_certification_status;
};

// Records whether FIPS mode is enabled on the device, and if enabled, the
// associated certification levels of it.
struct FipsInfo {
  FipsStatus activation_status;
  // Only present when |activation_status| is kActive.
  std::optional<FipsCertificationLevel> certification_level;
};

}  // namespace u2f
}  // namespace hwsec

#endif  // LIBHWSEC_STRUCTURES_U2F_H_
