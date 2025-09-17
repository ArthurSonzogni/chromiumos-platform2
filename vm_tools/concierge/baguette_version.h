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
constexpr char kBaguetteVersion[] = "2025-09-17-000103_36b720cebdbdb982fb46b50cb889890a7edd8cf2";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6f3d032123d9eb8e9863cbabfb62dc9fd5b61b0211b2cbf534a1e2f55623de56";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "467fc1da076bdfec37052e521638824e38ed723ba790e9ec0bc43177e3fea1fb";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
