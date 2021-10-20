// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/check.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <stddef.h>
#include <stdint.h>

#include "vpn-manager/daemon_mock.h"
#include "vpn-manager/ipsec_manager.h"

namespace vpn_manager {

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOGGING_FATAL); }
};

class IpsecManagerFuzzer {
 public:
  explicit IpsecManagerFuzzer(FuzzedDataProvider* const fuzz_provider)
      : fuzz_provider_(fuzz_provider) {}

  bool SetUpIpsecManager() {
    tmp_dir_ = std::make_unique<base::ScopedTempDir>();
    CHECK(tmp_dir_->CreateUniqueTempDir());
    base::FilePath temp_path =
        tmp_dir_->GetPath().Append("ipsec_manager_fuzzdir");
    base::CreateDirectory(temp_path);
    base::FilePath persistent_path = temp_path.Append("persistent");
    base::CreateDirectory(persistent_path);

    // Put all bytes in the file.
    base::FilePath file_path = tmp_dir_->GetPath().Append("cert_fuzz");
    cert_path_ = base::FilePath(file_path);
    base::File file(cert_path_,
                    base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
    if (!file.created()) {
      LOG(ERROR) << "Failed to create " << file_path.value();
      return false;
    }

    const std::string& file_contents =
        fuzz_provider_->ConsumeRemainingBytesAsString();
    file.Write(0, file_contents.c_str(), file_contents.length());

    starter_daemon_ = new DaemonMock;
    charon_daemon_ = new DaemonMock;
    ipsec_manager_ = std::make_unique<IpsecManager>(
        "aes128-sha1,3des-sha1,aes128-md5,3des-md5",  // esp
        "3des-sha1-modp1024",                         // ike
        30,                                           // ipsec_timeout
        "17/1701",                                    // left_protoport
        true,                                         // rekey
        "17/1701",                                    // right_protoport
        "",                                           // tunnel_group
        "transport",                                  // type
        temp_path,                                    // temp_path
        persistent_path);                             // persistent_path
    ipsec_manager_->starter_daemon_.reset(
        starter_daemon_);                                  // Passes ownership.
    ipsec_manager_->charon_daemon_.reset(charon_daemon_);  // Passes ownership.

    return true;
  }

  void Fuzz() {
    std::string ignored;

    ipsec_manager_->ReadCertificateSubject(cert_path_, &ignored);
  }

 private:
  std::unique_ptr<IpsecManager> ipsec_manager_;
  DaemonMock* starter_daemon_;
  DaemonMock* charon_daemon_;

  std::unique_ptr<base::ScopedTempDir> tmp_dir_;
  base::FilePath cert_path_;

  FuzzedDataProvider* const fuzz_provider_;
};

namespace {

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  FuzzedDataProvider data_provider(data, size);
  IpsecManagerFuzzer ipsec_fuzzer(&data_provider);

  if (ipsec_fuzzer.SetUpIpsecManager())
    ipsec_fuzzer.Fuzz();

  return 0;
}
}  // namespace
}  // namespace vpn_manager
