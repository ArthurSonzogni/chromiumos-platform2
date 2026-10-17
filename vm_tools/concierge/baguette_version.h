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
constexpr char kBaguetteVersion[] = "2026-10-17-000109_7bd30c58a3bdc35a138ffea0b1436bd2a2977791";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c9df31bd31f418f44db0144399a689470e41cc758c7338ad1c8345b8e6391ace";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "77a38273c4ada6cfe2f7a5bbec600c1c9c9eca049657c24b2fad9b42af3a8047";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
