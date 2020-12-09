// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_PKCS11_CRYPTOKI_H_
#define CHAPS_PKCS11_CRYPTOKI_H_

#define EXPORT_SPEC __attribute__ ((visibility ("default")))

#ifndef NULL_PTR
#define NULL_PTR 0
#endif

// Note that this file is not the only entrypoint for including pkcs11.h.
// chaps.cc also includes pkcs11f.h.
#include <nss/pkcs11.h>

// Below are some workaround due to problems in the copy of pkcs11.h that we
// are including.

#ifndef CKK_INVALID_KEY_TYPE
#define CKK_INVALID_KEY_TYPE (CKK_VENDOR_DEFINED + 0)
#endif

// chaps is currently coded to PKCS#11 v2.20.
#define CRYPTOKI_VERSION_MAJOR 2
#define CRYPTOKI_VERSION_MINOR 20

#endif  // CHAPS_PKCS11_CRYPTOKI_H_
