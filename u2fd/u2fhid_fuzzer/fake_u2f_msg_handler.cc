// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/u2fhid_fuzzer/fake_u2f_msg_handler.h"

namespace u2f {

U2fResponseApdu FakeU2fMessageHandler::ProcessMsg(const std::string& request) {
  return U2fResponseApdu();
}

}  // namespace u2f
