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
constexpr char kBaguetteVersion[] = "2026-06-18-000103_89d560df62692e9cb0d321d3bfa4c297b4effc46";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "96ddd001e00a326d2ff220f8ab6c4e17d628c230cfa33d2ce53f316913b933bb";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c2da101413a329462dfd977acf8aa7c3594c6e1da8cafe84b2247157e209242d";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
