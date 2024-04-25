// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_DLC_CLIENT_FAKE_H_
#define LORGNETTE_DLC_CLIENT_FAKE_H_

#include <set>
#include <string>

#include "lorgnette/dlc_client.h"

namespace lorgnette {

class DlcClientFake : public DlcClient {
 public:
  void InstallDlc(const std::set<std::string>& dlc_ids) override;
  void OnDlcSuccess(const std::string& dlc_id);
  void SetCallbacks(
      base::RepeatingCallback<void(const std::string&, const base::FilePath&)>
          success_cb,
      base::RepeatingCallback<void(const std::string&, const std::string&)>
          failure_cb) override;

 private:
  base::RepeatingCallback<void(const std::string&, const base::FilePath&)>
      success_cb_;
  base::RepeatingCallback<void(const std::string&, const std::string&)>
      failure_cb_;
  const base::FilePath path_ = base::FilePath("/test/path/to/dlc");
};

}  // namespace lorgnette

#endif  // LORGNETTE_DLC_CLIENT_FAKE_H_
