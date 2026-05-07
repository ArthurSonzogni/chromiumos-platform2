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
constexpr char kBaguetteVersion[] = "2026-05-07-000106_8d0dfba44f3f5bb47a5c23fc99e55b0df9efc6c3";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "bbe2b1214f0c6749c690a79fd0ca08e6241e0fe97bc14c681979531186b45bc2";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b913e8617c3c181fbe2bb393ca191668a2b129759901859ee6d9213305aa6410";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
