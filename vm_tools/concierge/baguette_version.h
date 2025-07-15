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
constexpr char kBaguetteVersion[] = "2025-07-15-000130_9be1a74f5aec69657392dafb88e1194451344b6d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "93af2bdda7f436dcd04b66284d7340507751212783a1ec4b13f2b17730f39274";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "172661c7684fda825a0a0159be8336a7fd9fea67b31ee0184ddd86e30e14b5b5";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
