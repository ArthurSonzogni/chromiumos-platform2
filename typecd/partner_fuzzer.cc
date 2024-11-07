// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>

#include "fuzzer/FuzzedDataProvider.h"
#include "typecd/alt_mode.h"
#include "typecd/partner.h"
#include "typecd/test_utils.h"

namespace typecd {

class PartnerFuzzer {
 public:
  PartnerFuzzer() {
    // Set up the temporary directory where we create the partner sysfs
    // directory.
    CHECK(scoped_temp_dir_.CreateUniqueTempDir());
    temp_dir_ = scoped_temp_dir_.GetPath();
  }

  base::FilePath temp_dir_;
  base::ScopedTempDir scoped_temp_dir_;
};

}  // namespace typecd

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOGGING_ERROR); }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  typecd::PartnerFuzzer fuzzer;
  FuzzedDataProvider data_provider(data, size);

  // If the input corpus doesn't have sufficient bytes to fill out the VDOs, we
  // should return immediately.
  if (size < 1024) {
    return 0;
  }

  // Set up fake sysfs paths.
  auto partner_path = fuzzer.temp_dir_.Append(std::string("port0-partner"));
  CHECK(base::CreateDirectory(partner_path));

  auto identity_path = partner_path.Append(std::string("identity"));
  CHECK(base::CreateDirectory(identity_path));

  // Fill identity with random strings.
  // We pick length 10 since the output is of the form "0xdeadbeef", but this
  // can easily be higher.
  auto val = data_provider.ConsumeRandomLengthString(10);
  CHECK(base::WriteFile(identity_path.Append("cert_stat"), val));
  val = data_provider.ConsumeRandomLengthString(10);
  CHECK(base::WriteFile(identity_path.Append("id_header"), val));
  val = data_provider.ConsumeRandomLengthString(10);
  CHECK(base::WriteFile(identity_path.Append("product"), val));
  val = data_provider.ConsumeRandomLengthString(10);
  CHECK(base::WriteFile(identity_path.Append("product_type_vdo1"), val));
  val = data_provider.ConsumeRandomLengthString(10);
  CHECK(base::WriteFile(identity_path.Append("product_type_vdo2"), val));
  val = data_provider.ConsumeRandomLengthString(10);
  CHECK(base::WriteFile(identity_path.Append("product_type_vdo3"), val));

  // Fill other sysfs fields with data.
  val = data_provider.ConsumeRandomLengthString(10);
  CHECK(
      base::WriteFile(partner_path.Append("supports_usb_power_delivery"), val));

  typecd::Partner p(partner_path);
  return 0;
}
