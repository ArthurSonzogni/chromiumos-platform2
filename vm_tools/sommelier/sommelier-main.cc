// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

int real_main(int argc, char** argv);

int main(int argc, char** argv) {
  return real_main(argc, argv);
}
