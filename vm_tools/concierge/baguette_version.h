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
constexpr char kBaguetteVersion[] = "2026-10-23-000119_23b5535ed41e57223d5a887a61cfec6471839402";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "63182727be5761b8b82be3101a1fa3e4cb5969560d9fe6d099c42e2f79f5a556";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "bf741243055c10907306b6dcd263423c4e4f5eb687ac2f925910a7abd9207e78";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
