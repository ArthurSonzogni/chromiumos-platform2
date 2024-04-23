// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_BINARY_LOG_TOOL_H_
#define DEBUGD_SRC_BINARY_LOG_TOOL_H_

#include <map>
#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/memory/scoped_refptr.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/bus.h>
#include <dbus/debugd/dbus-constants.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>
#include <fbpreprocessor-client/fbpreprocessor/dbus-proxies.h>
#include <user_data_auth-client/user_data_auth/dbus-proxies.h>

namespace debugd {

class BinaryLogTool {
 public:
  explicit BinaryLogTool(scoped_refptr<dbus::Bus> bus);

  void GetBinaryLogs(
      const std::string& username,
      const std::map<FeedbackBinaryLogType, base::ScopedFD>& outfds);

  // Test-only.
  // Tests that want to mock the D-Bus interaction between debugd and
  // fbpreprocessord can provide a mock proxy.
  // BinaryLogTool will take ownership of the unique_ptr.
  void SetFbPreprocessorProxyForTesting(
      std::unique_ptr<org::chromium::FbPreprocessorProxyInterface> proxy);

  // Tests that want to mock the D-Bus interaction between debugd and
  // cryptohomed can provide a mock proxy.
  // BinaryLogTool will take ownership of the unique_ptr.
  void SetCryptohomeProxyForTesting(
      std::unique_ptr<org::chromium::CryptohomeMiscInterfaceProxyInterface>
          proxy);

  // Make it possible for tests to use a temporary directory instead of the
  // usual /run/daemon-store/fbpreprocessord/ base directory.
  void SetDaemonStoreBaseDirForTesting(const base::FilePath& base_dir) {
    daemon_store_base_dir_ = base_dir;
  }

  void DisableMinijailForTesting();

 private:
  std::unique_ptr<org::chromium::FbPreprocessorProxyInterface>
      fbpreprocessor_proxy_;

  std::unique_ptr<org::chromium::CryptohomeMiscInterfaceProxyInterface>
      cryptohome_proxy_;

  // Firmware dumps are normally stored under the usual daemon-store path
  // /run/daemon-store/fbpreprocessord/<user_hash>/. |daemon_store_base_dir_|
  // will typically be /run/daemon-store/fbpreprocessord/. However, unit tests
  // can specify a different base directory so they can use a temporary
  // directory instead.
  base::FilePath daemon_store_base_dir_;

  // Test-only.
  // Set to true by default. If set to false, the "tar" subprocess will not be
  // sanboxed with minijail when binary logs are compressed.
  bool use_minijail_ = true;
};

}  // namespace debugd

#endif  // DEBUGD_SRC_BINARY_LOG_TOOL_H_
