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
constexpr char kBaguetteVersion[] = "2026-01-28-000114_87f756cf6de8ea8265a1e223fb3c7bbf1d2e239e";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a951a99aad74fbe9df4e03fb4a9c5dc47fc223e4b78d4ae2c24d4056e9db6897";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "04a8653ea8618685fb877804e1f610766366fd886cc14546ff5a7b99d608e3c3";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
