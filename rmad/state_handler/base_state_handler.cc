// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/base_state_handler.h"

namespace rmad {

BaseStateHandler::BaseStateHandler(scoped_refptr<JsonStore> json_store)
    : json_store_(json_store) {}

}  // namespace rmad
