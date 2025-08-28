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
constexpr char kBaguetteVersion[] = "2025-08-28-000100_e10fe893357935faf6467610481af06bfec7aab7";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "30d73a5e189e19c293c4b086c3fcdb3b026ef0dd569dc19066c68e42dcd751a2";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "328cc47168241dd7ae408e10b0ae365e4eda333a13ed153245256a39dc726306";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
