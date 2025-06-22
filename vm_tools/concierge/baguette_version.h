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
constexpr char kBaguetteVersion[] = "2025-06-22-000130_d067c81ee4d8191e3d194ff8452e8f39eefb074f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "aef56db102f7e06df46b53f51d72c7d95ddb9ac6606bdc5f5e252a0fafd37a48";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "61efe277904623acf223cf6e885d9b7d7f37593542b94f4a4902cd2dff002d5c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
