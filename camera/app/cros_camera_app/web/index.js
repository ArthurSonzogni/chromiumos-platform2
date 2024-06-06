// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

async function init() {
  const videoEl = document.querySelector("video");

  const stream = await navigator.mediaDevices.getUserMedia({
    video: {
      width: { ideal: 1280 },
      frameRate: { ideal: 30 },
    },
  });
  videoEl.srcObject = stream;
}

init();
