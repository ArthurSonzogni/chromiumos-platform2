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
constexpr char kBaguetteVersion[] = "2025-11-04-000105_6a0d865ee0f9704645961f9664a731a26c075561";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "ca271d3590b71f2c9b2728485f26878b31f98cf5672c7a0361f0d3a836b30a38";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "64e7ba960852ee69901aedcc28f7a2ab1518f9a8610c72871430665e6b9f4c52";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
