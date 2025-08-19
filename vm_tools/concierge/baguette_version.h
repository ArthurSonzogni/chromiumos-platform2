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
constexpr char kBaguetteVersion[] = "2025-08-19-000102_17966d11328f4fc8e1c6bc8f6204fd33dd7eab7a";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "048ae7505cfe8e96ed71f03dc2ea96121307fc1ac699fec980901370afc649aa";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "1d1d6e2523438803876411c90ac7e12accbe3fe5f77472a41694e9397b44e592";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
