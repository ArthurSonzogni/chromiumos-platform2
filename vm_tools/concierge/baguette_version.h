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
constexpr char kBaguetteVersion[] = "2026-02-19-000121_c454f73d002b69ad1b555eb1063a8303b57832f3";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "9c1dd3f04e6bfd67e52ccada2c80e7994bfecf49631be0e1d492b273910819fe";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "32e4d1d529dbefd8cdc738e32fb83100c60b99eac563e88e2f2d824cf596502a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
