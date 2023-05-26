// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_DLC_CLIENT_FAKE_H_
#define LORGNETTE_DLC_CLIENT_FAKE_H_

#include <string>

#include "lorgnette/dlc_client.h"

namespace lorgnette {

class DlcClientFake : public DlcClient {
 public:
  void InstallDlc() override;
  void OnDlcSuccess();
  void SetCallbacks(
      base::OnceCallback<void(const base::FilePath&)> success_cb,
      base::OnceCallback<void(const std::string&)> failure_cb) override;

 private:
  base::OnceCallback<void(const base::FilePath&)> success_cb_;
  base::OnceCallback<void(const std::string&)> failure_cb_;
  const base::FilePath path_ = base::FilePath("/test/path/to/dlc");
};

}  // namespace lorgnette

#endif  // LORGNETTE_DLC_CLIENT_FAKE_H_
