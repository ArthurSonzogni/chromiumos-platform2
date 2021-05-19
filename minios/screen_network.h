// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREEN_NETWORK_H_
#define MINIOS_SCREEN_NETWORK_H_

#include <memory>
#include <string>

#include "minios/screen_base.h"

namespace minios {

class ScreenNetwork : public ScreenBase {
 public:
  explicit ScreenNetwork(std::shared_ptr<DrawInterface> draw_utils,
                         ScreenControllerInterface* screen_controller);
  ~ScreenNetwork() = default;

  ScreenNetwork(const ScreenNetwork&) = delete;
  ScreenNetwork& operator=(const ScreenNetwork&) = delete;

  void Show() override;

  void Reset() override;

  void OnKeyPress(int key_changed) override;

  ScreenType GetType() override;

  std::string GetName() override;

 private:
  void ShowButtons();
};

}  // namespace minios

#endif  // MINIOS_SCREEN_NETWORK_H_
