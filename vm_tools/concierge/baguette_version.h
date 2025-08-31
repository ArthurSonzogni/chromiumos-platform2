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
constexpr char kBaguetteVersion[] = "2025-08-31-000135_68f8c4c381fa466f656b22cb517c9870644c6036";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "abc74fe2a4b46272ec45e5062a1d97916956f20d595ef8a1e6dbf74db798fbc4";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "55f2f06dd321cb49a5041a52eecea36de589bcf9d0a390af2e5515deb93e3305";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
