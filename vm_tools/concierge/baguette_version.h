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
constexpr char kBaguetteVersion[] = "2026-08-09-000231_fc417024da8d50b74078d58ed39f63eb2d396599";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "766b204a56e911cac822f60fd8250da290aec4760a66f4f8323078d172e0bae0";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4fbc002c72abe766edef0dbc766cf49d1fdc59662f2a33616c81f1b772c5c27a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
