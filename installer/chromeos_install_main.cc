// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

int main(int argc, char* argv[]) {
  int ret = execvp("chromeos-install.sh", argv);
  return ret;
}
