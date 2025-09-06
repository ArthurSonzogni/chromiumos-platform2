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
constexpr char kBaguetteVersion[] = "2025-09-06-000104_b5b9ffa52c0e81364168fc7206199dfb2fe2209b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "3960e5b3d8884623975ebceeac89a0250381cd6f57f6ff5a43f951a2fcf4c365";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "344675c452a7c9014a3987c283fa5ad9b19ad10e94d7e377649d95318a7a2dbc";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
