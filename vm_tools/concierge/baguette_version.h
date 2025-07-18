// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
#define VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

// This constant points to the image downloaded for new installations of
// Baguette.
// TODO(crbug.com/393151776): Point to luci recipe and builders that update this
// URL when new images are available.

// clang-format off
constexpr char kBaguetteVersion[] = "2025-07-18-000109_6c353cda82fe3c14ae1cf9d2799ba65d23c9a2ed";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "4b918ff54884c1dadf47fc01fe78bbdebaef0a64a3b5ecaf9f56b01e21c4fc66";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "48e82d843fd969def56f56ca3dcbe64c4fe3526f7b8b23059f5bf10727b8347d";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
