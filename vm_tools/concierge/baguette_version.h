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
constexpr char kBaguetteVersion[] = "2025-07-08-000117_08626b773b633195162cecc1d5eaaf32ab6ea36c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "1162aa334faf73aef0586c72602e9c3de61c4d7cf256a5a853e44f06e57a8a08";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "f0ba8db80351cd251af5bd31e26560a9850747ca6c5d55cb38d2dec78cc7884c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
