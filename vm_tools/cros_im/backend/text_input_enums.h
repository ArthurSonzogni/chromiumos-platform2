// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CROS_IM_BACKEND_TEXT_INPUT_ENUMS_H_
#define VM_TOOLS_CROS_IM_BACKEND_TEXT_INPUT_ENUMS_H_

// Enum definitions from the text-input protocol. These should remain in sync
// with the definitions in text-input-unstable-v1.xml and thus those generated
// in text-input-unstable-v1-client-protocol.h.

// Having these enums separated from the generated Wayland headers allows
// frontends to not need a dependency on the Wayland headers, which the backend
// otherwise abstracts away.

// TODO(timloh): Add some sort of checks to ensure these are kept in sync.

#ifndef ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_ENUM
#define ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_ENUM
enum zwp_text_input_v1_preedit_style {
  ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_DEFAULT = 0,
  ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_NONE = 1,
  ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_ACTIVE = 2,
  ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_INACTIVE = 3,
  ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_HIGHLIGHT = 4,
  ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_UNDERLINE = 5,
  ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_SELECTION = 6,
  ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_INCORRECT = 7,
};
#endif  // ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_ENUM

#endif  // VM_TOOLS_CROS_IM_BACKEND_TEXT_INPUT_ENUMS_H_
