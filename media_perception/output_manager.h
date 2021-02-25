// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_PERCEPTION_OUTPUT_MANAGER_H_
#define MEDIA_PERCEPTION_OUTPUT_MANAGER_H_

#include <memory>
#include <string>
#include <vector>
#include <brillo/dbus/dbus_connection.h>
#include <dbus/message.h>

#include "base/memory/weak_ptr.h"
#include "media_perception/media_perception_mojom.pb.h"
#include "media_perception/perception_interface.pb.h"
#include "media_perception/rtanalytics.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojom/media_perception.mojom.h"

namespace mri {

// Manages and handles many types of graph outputs. Class represents an
// abstraction so that the MediaPerceptionImpl does not need to care what the
// output types for a particular pipeline are.
class OutputManager {
 public:
  OutputManager() {}

  OutputManager(const std::string& configuration_name,
                std::shared_ptr<Rtanalytics> rtanalytics,
                const PerceptionInterfaces& interfaces,
                chromeos::media_perception::mojom::PerceptionInterfacesPtr*
                    interfaces_ptr);

  void HandleFramePerception(const std::vector<uint8_t>& bytes);

  void HandleHotwordDetection(const std::vector<uint8_t>& bytes);

  void HandlePresencePerception(const std::vector<uint8_t>& bytes);

  void HandleOccupancyTrigger(const std::vector<uint8_t>& bytes);

  void HandleAppearances(const std::vector<uint8_t>& bytes);

  void HandleSmartFraming(const std::vector<uint8_t>& bytes);

  // Empty bytes indicates a PTZ reset command.
  void HandleIndexedTransitions(const std::vector<uint8_t>& bytes);

 private:
  void HandleFalconPtzTransitionResponse(dbus::Response* response);

  std::string configuration_name_;

  std::shared_ptr<Rtanalytics> rtanalytics_;

  // D-Bus objects for sending messages to the Falcon camera.
  brillo::DBusConnection dbus_connection_;
  scoped_refptr<::dbus::Bus> bus_;
  dbus::ObjectProxy* dbus_proxy_;

  mojo::Remote<chromeos::media_perception::mojom::FramePerceptionHandler>
      frame_perception_handler_;

  mojo::Remote<chromeos::media_perception::mojom::HotwordDetectionHandler>
      hotword_detection_handler_;

  mojo::Remote<chromeos::media_perception::mojom::PresencePerceptionHandler>
      presence_perception_handler_;

  mojo::Remote<chromeos::media_perception::mojom::OccupancyTriggerHandler>
      occupancy_trigger_handler_;

  mojo::Remote<chromeos::media_perception::mojom::AppearancesHandler>
      appearances_handler_;

  mojo::Remote<chromeos::media_perception::mojom::OneTouchAutozoomHandler>
      one_touch_autozoom_handler_;

  mojo::Remote<chromeos::media_perception::mojom::SoftwareAutozoomHandler>
      software_autozoom_handler_;

  base::WeakPtrFactory<OutputManager> weak_ptr_factory_{this};
};

}  // namespace mri

#endif  // MEDIA_PERCEPTION_OUTPUT_MANAGER_H_
