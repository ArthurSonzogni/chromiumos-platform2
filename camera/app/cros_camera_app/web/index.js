// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { AsyncJobQueue } from "./async_job_queue.js";

/**
 * @param {MediaStream} stream
 */
function stopAllTracks(stream) {
  for (const track of stream.getTracks()) {
    track.stop();
  }
}

class FpsCounter {
  /**
   * @param {HTMLVideoElement} video
   * @param {number} len
   */
  constructor(video, len) {
    this.video = video;
    this.len = len;

    /** @type {number[]} */
    this.timestamps = [];

    /** @type {HTMLDivElement} */
    this.el = document.getElementById("fps-counter");

    /** @type {number|null} */
    this.callbackId = null;
  }

  /**
   * @param {DOMHighResTimeStamp} now
   */
  onVideoFrame = (now) => {
    this.timestamps.push(now);
    if (this.timestamps.length >= this.len * 2) {
      this.timestamps = this.timestamps.slice(-this.len);
    }

    const count = Math.min(this.len, this.timestamps.length);
    const start = this.timestamps.at(-count);
    const fps = count === 0 ? 0 : ((count - 1) * 1000) / (now - start);
    this.el.textContent = `FPS: ${fps.toFixed(0)}`;

    this.callbackId = this.video.requestVideoFrameCallback(this.onVideoFrame);
  };

  start() {
    this.callbackId = this.video.requestVideoFrameCallback(this.onVideoFrame);
  }

  stop() {
    if (this.callbackId !== null) {
      this.video.cancelVideoFrameCallback(this.callbackId);
      this.callbackId = null;
    }

    this.timestamps = [];
    this.el.textContent = "FPS: ?";
  }
}

class ResolutionStat {
  constructor() {
    this.el = document.getElementById("resolution-stat");
  }

  /**
   * @param {MediaStream} stream
   */
  update(stream) {
    const track = stream.getVideoTracks()[0];
    const { width = 0, height = 0 } = track.getSettings();
    this.el.textContent = `Resolution: ${width}x${height}`;
  }

  reset() {
    this.el.textContent = `Resolution: ?`;
  }
}

/**
 * @typedef {Object} TurboStatData
 * @property {number=} PkgWatt
 * @property {number=} CorWatt
 * @property {number=} GFXWatt
 */

class TurboStat {
  constructor() {
    /** @type {HTMLDivElement} */
    this.pkgWattEl = document.getElementById("pkg-watt");

    /** @type {HTMLDivElement} */
    this.corWattEl = document.getElementById("cor-watt");

    /** @type {HTMLDivElement} */
    this.gfxWattEl = document.getElementById("gfx-watt");

    /** @type {EventSource|null} */
    this.source = null;
  }

  start() {
    this.source = new EventSource("/turbostat");
    this.source.addEventListener("message", this.onMessage);

    // TODO(shik): Move this check to server-side rendering to avoid flickering.
    this.source.addEventListener("unsupported", this.onUnsupported);
  }

  stop() {
    // TODO(shik): Expose this on UI.
    if (this.source !== null) {
      this.source.close();
      this.source = null;
    }
  }

  onUnsupported = () => {
    document.body.classList.toggle("no-turbostat", true);
  };

  /**
   * @param {MessageEvent<string>} event
   */
  onMessage = (event) => {
    /**
     * @param {HTMLDivElement} el
     * @param {string} prefix
     * @param {number|undefined} value
     */
    const update = (el, prefix, value) => {
      const valueStr = value === undefined ? "?" : value.toFixed(2);
      el.textContent = prefix + valueStr;
    };

    /** @type {TurboStatData} */
    const data = JSON.parse(event.data);
    update(this.pkgWattEl, "PkgWatt: ", data.PkgWatt);
    update(this.corWattEl, "CorWatt: ", data.CorWatt);
    update(this.gfxWattEl, "GFXWatt: ", data.GFXWatt);
  };
}

class CameraDeviceSelect {
  /**
   * @param {(deviceId: string) => Awaitble<void>} onChange
   */
  constructor(onChange) {
    /** @type {typeof onChange} */
    this.onChange = onChange;

    /** @type {HTMLSelectElement} */
    this.el = document.getElementById("camera-device");

    this.el.addEventListener("change", this.handleChange);
  }

  async updateOptions() {
    let devs = await navigator.mediaDevices.enumerateDevices();
    if (devs.some((d) => d.kind === "videoinput" && d.deviceId === "")) {
      // The deviceId and label could be empty string before granting the
      // permission for privacy reasons. Call getUserMedia() to prompt the
      // permission dialog proactively.
      const stream = await navigator.mediaDevices.getUserMedia({
        video: true,
      });
      stopAllTracks(stream);

      devs = await navigator.mediaDevices.enumerateDevices();
    }

    const options = [];
    for (const d of devs) {
      if (d.kind !== "videoinput") {
        continue;
      }
      const option = document.createElement("option");
      option.textContent = d.label;
      option.value = d.deviceId;
      options.push(option);
    }
    this.el.replaceChildren(...options);

    // TODO(shik): Prefer the previously selected value.
    if (this.el.value !== null) {
      await this.handleChange();
    }
  }

  handleChange = async () => {
    this.el.disabled = true;
    await this.onChange(this.el.value);
    this.el.disabled = false;
  };
}

class PlayPauseButton {
  /**
   * @param {(clickedPlay: boolean) => Awaitable<void>} onClick A callback for
   *     the button click event with a boolean parameter to indicate whether
   *     this click is for "Play" or "Pause".
   */
  constructor(onClick) {
    /** @type {typeof onClick} */
    this.onClick = onClick;

    /** @type {boolean} */
    this.isPlaying = false;

    /** @type {HTMLButtonElement} */
    this.el = document.getElementById("play-pause");

    this.el.addEventListener("click", this.handleClick);
  }

  /**
   * @param {boolean} value
   */
  setPlaying(value) {
    this.isPlaying = value;
    this.el.textContent = value ? "Pause" : "Play";
  }

  handleClick = async () => {
    this.el.disabled = true;
    const clickedPlay = !this.isPlaying;
    await this.onClick(clickedPlay);
    this.el.disabled = false;
  };
}

class CameraApp {
  constructor() {
    this.deviceId = null;
    this.video = document.querySelector("video");
    this.fpsCounter = new FpsCounter(this.video, 100);
    this.resolutionStat = new ResolutionStat();
    this.playPauseButton = new PlayPauseButton(this.onPlayPauseClick);
    this.cameraDeviceSelect = new CameraDeviceSelect(this.onDeviceChange);
  }

  async init() {
    await this.cameraDeviceSelect.updateOptions();
  }

  async start() {
    this.playPauseButton.setPlaying(true);
    const stream = await navigator.mediaDevices.getUserMedia({
      video: {
        width: { ideal: 1280 },
        frameRate: { ideal: 30 },
        ...(this.deviceId === null ? {} : { deviceId: this.deviceId }),
      },
    });
    this.video.srcObject = stream;
    this.fpsCounter.start();
    this.resolutionStat.update(stream);
  }

  stop() {
    /** @type {MediaStream} stream */
    const stream = this.video.srcObject;
    if (stream === null) {
      return;
    }

    this.resolutionStat.reset();
    this.fpsCounter.stop();
    stopAllTracks(stream);
    this.video.srcObject = null;
    this.playPauseButton.setPlaying(false);
  }

  /**
   * @return {boolean}
   */
  isPlaying() {
    return this.video.srcObject !== null;
  }

  /**
   * @param {boolean} clickedPlay
   */
  onPlayPauseClick = async (clickedPlay) => {
    if (clickedPlay) {
      await this.start();
    } else {
      this.stop();
    }
  };

  /**
   * @param {string} deviceId
   */
  onDeviceChange = async (deviceId) => {
    this.deviceId = deviceId;
    if (this.isPlaying()) {
      this.stop();
      await this.start();
    }
  };
}

/**
 * @param {CameraApp} app
 */
function registerHotkeys(app) {
  /**
   * @param {string} key
   */
  const handleHotkey = async (key) => {
    if (key === "p") {
      if (app.isPlaying()) {
        app.stop();
      } else {
        await app.start();
      }
    }
  };

  // Only process the next hotkey event if the previous one is finished to avoid
  // race conditions.
  const queue = new AsyncJobQueue("drop");
  document.addEventListener("keydown", (event) => {
    queue.push(() => handleHotkey(event.key));
  });
}

async function init() {
  const turboStat = new TurboStat();
  turboStat.start();

  const app = new CameraApp();
  await app.init();
  await app.start();

  registerHotkeys(app);
}

init();
