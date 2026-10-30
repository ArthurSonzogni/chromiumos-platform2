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
constexpr char kBaguetteVersion[] = "2026-10-30-000107_2faf5e146077b4d84413ed65145f719ad54c3922";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "bbf62ea7959e826c85c960315303a3308e9b8bbe97413d4bc9c1e4ed59ed5514";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "12900042fd9d52a90b971d222b55171b17f4f4ab6f43cd52c3ef27167bbf2c51";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
