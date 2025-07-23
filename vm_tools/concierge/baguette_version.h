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
constexpr char kBaguetteVersion[] = "2025-07-23-000119_da06dac60be7a09b1321d2cb2e13b8f62dfc59a4";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "3dccc8756f484175fa48ad230fed8e09eb3f9ce8b65c0c55b96d861fb37f7fb1";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "dee633ffedf8a4cd93bf52e4169e9d4afa1d87850772c305fba83ffdc93f1a46";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
