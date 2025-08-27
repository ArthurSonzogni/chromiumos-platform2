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
constexpr char kBaguetteVersion[] = "2025-08-27-000104_db03bbb4bfac44ed7e071363a6714dffed8ccc63";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "7b3fbff8beda9304a8822b2cb643c68c44ca2e202e020aaf2ba25a6abf460dfa";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "88dcf77a17624363a1cee2fbbb79e6c3f85fc00b7b797e1cf8098d5716defac3";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
