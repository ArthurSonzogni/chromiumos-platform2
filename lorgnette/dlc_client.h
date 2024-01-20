// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_DLC_CLIENT_H_
#define LORGNETTE_DLC_CLIENT_H_

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/strings/strcat.h>

#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <dlcservice/dbus-proxies.h>  //NOLINT (build/include_alpha)

namespace lorgnette {

class DlcClient {
 public:
  DlcClient() = default;
  DlcClient(const DlcClient&) = delete;
  DlcClient& operator=(const DlcClient&) = delete;
  virtual ~DlcClient() = default;
  void Init(std::unique_ptr<org::chromium::DlcServiceInterfaceProxyInterface>
                dlcservice_client);
  virtual void SetCallbacks(
      base::OnceCallback<void(const base::FilePath&)> success_cb,
      base::OnceCallback<void(const std::string&)> failure_cb);
  virtual void InstallDlc();
  std::optional<std::string> GetRootPath(const std::string& in_id,
                                         std::string* out_error);

 private:
  void OnDlcInstall();
  void OnDlcInstallError(brillo::Error* error);
  void OnDlcStateChanged(const dlcservice::DlcState& dlc_state);
  void OnDlcStateChangedConnect(const std::string& interface,
                                const std::string& signal,
                                bool success);
  void InvokeSuccessCb(std::string dlc_root_path);
  void InvokeErrorCb(const std::string& error_msg);

  base::OnceCallback<void(const base::FilePath&)> success_cb_;
  base::OnceCallback<void(const std::string&)> failure_cb_;
  std::unique_ptr<org::chromium::DlcServiceInterfaceProxyInterface>
      dlcservice_client_;

  // Keep as last variable
  base::WeakPtrFactory<DlcClient> weak_factory_{this};
};
}  // namespace lorgnette

#endif  // LORGNETTE_DLC_CLIENT_H_