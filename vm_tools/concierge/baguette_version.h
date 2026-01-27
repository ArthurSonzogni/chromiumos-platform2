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
constexpr char kBaguetteVersion[] = "2026-01-27-000102_ccedc6f2d7615caaf5dea0c01db627668f443c13";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b059f1ae835a5ae1678c5401aa15a1df7834c7de67fad68a45684f0af668298b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a91fd51635987e2fc0862764df2b42cd6ec82bba7f11411910af828b10dc3d7b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
