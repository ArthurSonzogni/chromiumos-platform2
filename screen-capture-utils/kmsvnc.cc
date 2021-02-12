// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <csignal>
#include <cstdint>
#include <memory>

#include <sys/time.h>

#include <base/command_line.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include <rfb/rfb.h>

#include "screen-capture-utils/bo_import_capture.h"
#include "screen-capture-utils/capture.h"
#include "screen-capture-utils/crtc.h"
#include "screen-capture-utils/egl_capture.h"
#include "screen-capture-utils/uinput.h"

namespace screenshot {
namespace {

constexpr const char kInternalSwitch[] = "internal";
constexpr const char kExternalSwitch[] = "external";
constexpr const char kCrtcIdSwitch[] = "crtc-id";
constexpr const char kMethodSwitch[] = "method";
constexpr int kBytesPerPixel = 4;

class ScopedPowerLock {
 public:
  ScopedPowerLock() {
    PCHECK(system("set_power_policy --screen_wake_lock=1") != -1)
        << "Invoking set_power_policy to avoid screen off";
  }

  ScopedPowerLock(const ScopedPowerLock&) = delete;
  ScopedPowerLock& operator=(const ScopedPowerLock&) = delete;

  ~ScopedPowerLock() {
    PCHECK(system("set_power_policy --screen_wake_lock=-1") != -1)
        << "Invoking set_power_policy to restore wake lock";
  }
};

class FpsTimer {
 public:
  FpsTimer() { gettimeofday(&start_time_, NULL); }

  FpsTimer(const FpsTimer&) = delete;
  FpsTimer& operator=(const FpsTimer&) = delete;

  ~FpsTimer() = default;

  void Frame() { frames_++; }

  void ModifiedFrame() { modified_frames_++; }

  // Print FPS stats once a second.
  void MaybePrint() {
    if (Elapsed() < 1.0)
      return;

    VLOG(1) << "fps: " << Get(frames_)
            << "  (modified frames: " << Get(modified_frames_) << ")";
    modified_frames_ = 0;
    frames_ = 0;
    PCHECK(gettimeofday(&start_time_, NULL) != -1);
  }

 private:
  struct timeval start_time_;
  size_t frames_{0};
  size_t modified_frames_{0};

  double Elapsed() const {
    struct timeval end_time;
    PCHECK(gettimeofday(&end_time, NULL) != -1);
    double seconds = (end_time.tv_sec - start_time_.tv_sec) +
                     (end_time.tv_usec - start_time_.tv_usec) / 1000.0 / 1000.0;
    return seconds;
  }
  double Get(size_t frames) const { return frames / Elapsed(); }
};

class ScopedSigaction {
 public:
  ScopedSigaction(int signum, void (*handler)(int)) : signum_(signum) {
    struct sigaction new_action;
    new_action.sa_handler = handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;

    sigaction(signum, &new_action, &old_action_);
  }

  ScopedSigaction(const ScopedSigaction&) = delete;
  ScopedSigaction& operator=(const ScopedSigaction&) = delete;

  ~ScopedSigaction() { sigaction(signum_, &old_action_, nullptr); }

 private:
  const int signum_;
  struct sigaction old_action_;
};

// Signal number received if shutdown requested.
volatile int g_shutdown_requested{0};

void SignalHandler(int signum) {
  g_shutdown_requested = signum;
}

void ConvertBuffer(const DisplayBuffer::Result& from, char* to) {
  for (int i = 0; i < from.height; i++) {
    memcpy(to + from.width * kBytesPerPixel * i,
           static_cast<char*>(from.buffer) + from.stride * i,
           from.width * kBytesPerPixel);
  }
}

enum class CaptureMethod {
  AUTODETECT,
  EGL,
  BO,
};

int VncMain() {
  ScopedPowerLock power_lock;
  auto* cmdline = base::CommandLine::ForCurrentProcess();

  if (cmdline->GetArgs().size() != 0) {
    LOG(ERROR) << "Wrong number of parameters";
    return 1;
  }

  int crtc_specs = (cmdline->HasSwitch(kInternalSwitch) ? 1 : 0) +
                   (cmdline->HasSwitch(kExternalSwitch) ? 1 : 0) +
                   (cmdline->HasSwitch(kCrtcIdSwitch) ? 1 : 0);
  if (crtc_specs > 1) {
    LOG(ERROR) << "--internal, --external and --crtc-id are exclusive";
    return 1;
  }

  std::unique_ptr<Crtc> crtc;
  if (cmdline->HasSwitch(kInternalSwitch)) {
    crtc = screenshot::CrtcFinder::FindInternalDisplay();
  } else if (cmdline->HasSwitch(kExternalSwitch)) {
    crtc = screenshot::CrtcFinder::FindExternalDisplay();
  } else if (cmdline->HasSwitch(kCrtcIdSwitch)) {
    uint32_t crtc_id;
    if (!base::StringToUint(cmdline->GetSwitchValueASCII(kCrtcIdSwitch),
                            &crtc_id)) {
      LOG(ERROR) << "Invalid --crtc-id specification";
      return 1;
    }
    crtc = screenshot::CrtcFinder::FindById(crtc_id);
  } else {
    crtc = screenshot::CrtcFinder::FindAnyDisplay();
  }

  if (!crtc) {
    LOG(ERROR) << "CRTC not found. Is the screen on?";
    return 1;
  }

  CaptureMethod method = CaptureMethod::AUTODETECT;
  if (cmdline->HasSwitch(kMethodSwitch)) {
    std::string method_str = cmdline->GetSwitchValueASCII(kMethodSwitch);
    if (method_str == "egl") {
      method = CaptureMethod::EGL;
    } else if (method_str == "bo") {
      method = CaptureMethod::BO;
    } else {
      LOG(ERROR) << "Invalid --method specification";
      return 1;
    }
  }

  uint32_t crtc_width = crtc->width();
  uint32_t crtc_height = crtc->height();

  LOG(INFO) << "Starting with CRTC size of: " << crtc_width << " "
            << crtc_height;

  if (method == CaptureMethod::AUTODETECT) {
    if (crtc->fb2())
      method = CaptureMethod::EGL;
    else
      method = CaptureMethod::BO;
  }

  const rfbScreenInfoPtr server =
      rfbGetScreen(0 /*argc*/, nullptr /*argv*/, crtc_width, crtc_height,
                   8 /*bitsPerSample*/, 3 /*samplesPerPixel*/, kBytesPerPixel);
  CHECK(server);

  std::unique_ptr<screenshot::DisplayBuffer> display_buffer;

  if (method == CaptureMethod::EGL) {
    display_buffer.reset(new screenshot::EglDisplayBuffer(
        crtc.get(), 0, 0, crtc_width, crtc_height));
  } else {
    display_buffer.reset(new screenshot::GbmBoDisplayBuffer(
        crtc.get(), 0, 0, crtc_width, crtc_height));
  }

  std::vector<char> buffer(crtc_width * crtc_height * kBytesPerPixel);

  // This is ARGB buffer.
  {
    auto capture_result = display_buffer->Capture();
    ConvertBuffer(capture_result, buffer.data());
    server->frameBuffer = buffer.data();
  }
  // http://libvncserver.sourceforge.net/doc/html/rfbproto_8h_source.html#l00150
  server->serverFormat.redMax = 255;
  server->serverFormat.greenMax = 255;
  server->serverFormat.blueMax = 255;
  server->serverFormat.redShift = 16;
  server->serverFormat.greenShift = 8;
  server->serverFormat.blueShift = 0;

  // Create uinput devices and hook up input events.
  const std::unique_ptr<Uinput> uinput = Uinput::Create(server);

  rfbInitServer(server);

  std::vector<char> prev(crtc_width * crtc_height * kBytesPerPixel);

  ScopedSigaction sa1(SIGINT, SignalHandler);
  ScopedSigaction sa2(SIGTERM, SignalHandler);
  FpsTimer timer;

  while (rfbIsActive(server)) {
    timer.Frame();
    timer.MaybePrint();

    auto capture_result = display_buffer->Capture();
    // Keep the previous framebuffer around for comparison.
    prev.swap(buffer);
    // Copy the current data to the buffer.
    ConvertBuffer(capture_result, buffer.data());
    // Update VNC server's view to the swapped current buffer.
    server->frameBuffer = buffer.data();

    // Find rectangle of modification.
    int min_x = crtc_width;
    int min_y = crtc_height;
    int max_x = 0;
    int max_y = 0;
    const char* current = buffer.data();
    for (int y = 0; y < crtc_height; y++) {
      for (int x = 0; x < crtc_width; x++) {
        if (*reinterpret_cast<const uint32_t*>(
                &current[(x + y * crtc_width) * kBytesPerPixel]) ==
            *reinterpret_cast<uint32_t*>(
                &prev[(x + y * crtc_width) * kBytesPerPixel])) {
          continue;
        }
        max_x = std::max(x, max_x);
        max_y = std::max(y, max_y);
        min_x = std::min(x, min_x);
        min_y = std::min(y, min_y);
      }
    }

    if ((min_x > max_x) || (min_y > max_y)) {
      // Skipping unchanged frame.
    } else {
      timer.ModifiedFrame();
      rfbMarkRectAsModified(server, min_x, min_y, max_x, max_y);
    }

    // deferUpdateTime (select timeout waiting for sockets) 60fps is 16ms if
    // everything else happened in an instant.
    rfbProcessEvents(server, 16000 /*deferUpdateTime*/);
    if (g_shutdown_requested) {
      LOG(INFO) << "Caught signal, shutting down";
      rfbShutdownServer(server, true /*disconnectClients*/);
    }
  }

  return 0;
}

}  // namespace
}  // namespace screenshot

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_STDERR;
  logging::InitLogging(settings);

  return screenshot::VncMain();
}
