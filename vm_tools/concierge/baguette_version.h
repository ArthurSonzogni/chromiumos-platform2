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
constexpr char kBaguetteVersion[] = "2025-07-27-000153_06f9b4505103eeb7704661f772ae0699dcd04812";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "d4b90ab5d3e26e57d6e7ad0d080b92b1c2290ce2ca38528250796e84716af7e1";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "638abba64479ba2c75de271f38228a700867feed6a5a4ba1b9b65002bdd50a3a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
