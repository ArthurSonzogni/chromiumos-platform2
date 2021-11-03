// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <gtest/gtest.h>

#include "ml/document_scanner_library.h"
#include "ml/util.h"

namespace ml {

namespace {

DocumentScannerLibraryParams GetTestingParams() {
  DocumentScannerLibraryParams params;
  params.root_dir = base::FilePath("/build/share/cros_camera/");
  return params;
}

}  // namespace

TEST(DocumentScannerLibraryTest, CanLoadLibrary) {
  auto* const library = DocumentScannerLibrary::GetInstance();
  if (IsAsan()) {
    EXPECT_FALSE(DocumentScannerLibrary::IsSupported());
    return;
  }

  if (DocumentScannerLibrary::IsSupported()) {
    EXPECT_EQ(library->Initialize(GetTestingParams()),
              DocumentScannerLibrary::InitializeResult::kOk);
  }
}

TEST(DocumentScannerLibraryTest, GetScanner) {
  if (!DocumentScannerLibrary::IsSupported()) {
    // No need to test the behavior on an unsupported device.
    return;
  }

  auto* const library = DocumentScannerLibrary::GetInstance();
  ASSERT_EQ(library->Initialize(GetTestingParams()),
            DocumentScannerLibrary::InitializeResult::kOk);

  auto scanner = library->CreateDocumentScanner();
  EXPECT_NE(scanner, nullptr);
}

}  // namespace ml
