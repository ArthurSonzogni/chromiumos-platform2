// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <base/files/file_path.h>
#include <brillo/cryptohome.h>

int main(int argc, char** argv) {
  if (argc != 3 || (strcmp(argv[1], "system") && strcmp(argv[1], "user"))) {
    printf("Usage: %s <system|user> <username>\n", argv[0]);
    return 1;
  }
  base::FilePath fp;
  if (!strcmp(argv[1], "system"))
    fp = brillo::cryptohome::home::GetRootPath(argv[2]);
  else
    fp = brillo::cryptohome::home::GetUserPath(argv[2]);
  printf("%s\n", fp.value().c_str());
  return 0;
}
