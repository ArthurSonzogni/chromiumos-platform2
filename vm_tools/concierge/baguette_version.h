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
constexpr char kBaguetteVersion[] = "2025-12-04-000112_c451f7e7a752d410fed3e1e3379329af34e5b303";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "335fe9ae49726ecadc897ff6be4ed87af05507e5f2bea46fac551d0ee2f6dc87";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "55dc4c596998bf8ba7090d268b8600ffa738b79b8c5f1ca35b5addb747402232";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
