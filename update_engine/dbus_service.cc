// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/dbus_service.h"

#include <string>

#include <base/logging.h>

#include "update_engine/marshal.glibmarshal.h"
#include "update_engine/omaha_request_params.h"
#include "update_engine/utils.h"

using std::string;

static const char kAUTestURLRequest[] = "autest";
// By default autest bypasses scattering. If we want to test scattering,
// we should use autest-scheduled. The Url used is same in both cases, but
// different params are passed to CheckForUpdate method.
static const char kScheduledAUTestURLRequest[] = "autest-scheduled";

static const char kAUTestURL[] =
    "https://omaha.sandbox.google.com/service/update2";

G_DEFINE_TYPE(UpdateEngineService, update_engine_service, G_TYPE_OBJECT)

static void update_engine_service_finalize(GObject* object) {
  G_OBJECT_CLASS(update_engine_service_parent_class)->finalize(object);
}

static guint status_update_signal = 0;

static void update_engine_service_class_init(UpdateEngineServiceClass* klass) {
  GObjectClass *object_class;
  object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = update_engine_service_finalize;

  status_update_signal = g_signal_new(
      "status_update",
      G_OBJECT_CLASS_TYPE(klass),
      G_SIGNAL_RUN_LAST,
      0,  // 0 == no class method associated
      NULL,  // Accumulator
      NULL,  // Accumulator data
      update_engine_VOID__INT64_DOUBLE_STRING_STRING_INT64,
      G_TYPE_NONE,  // Return type
      5,  // param count:
      G_TYPE_INT64,
      G_TYPE_DOUBLE,
      G_TYPE_STRING,
      G_TYPE_STRING,
      G_TYPE_INT64);
}

static void update_engine_service_init(UpdateEngineService* object) {
}

UpdateEngineService* update_engine_service_new(void) {
  return reinterpret_cast<UpdateEngineService*>(
      g_object_new(UPDATE_ENGINE_TYPE_SERVICE, NULL));
}

gboolean update_engine_service_attempt_update(UpdateEngineService* self,
                                              gchar* app_version,
                                              gchar* omaha_url,
                                              GError **error) {
  string update_app_version;
  string update_omaha_url;
  bool interactive = true;

  // Only non-official (e.g., dev and test) builds can override the current
  // version and update server URL over D-Bus. However, pointing to the
  // hardcoded test update server URL is always allowed.
  if (!chromeos_update_engine::utils::IsOfficialBuild()) {
    if (app_version) {
      update_app_version = app_version;
    }
    if (omaha_url) {
      update_omaha_url = omaha_url;
    }
  }
  if (omaha_url) {
    if (strcmp(omaha_url, kScheduledAUTestURLRequest) == 0) {
      update_omaha_url = kAUTestURL;
      // pretend that it's not user-initiated even though it is,
      // so as to test scattering logic, etc. which get kicked off
      // only in scheduled update checks.
      interactive = false;
    } else if (strcmp(omaha_url, kAUTestURLRequest) == 0) {
      update_omaha_url = kAUTestURL;
    }
  }
  LOG(INFO) << "Attempt update: app_version=\"" << update_app_version << "\" "
            << "omaha_url=\"" << update_omaha_url << "\" "
            << "interactive=" << (interactive? "yes" : "no");
  self->system_state_->update_attempter()->CheckForUpdate(update_app_version,
                                                          update_omaha_url,
                                                          interactive);
  return TRUE;
}

gboolean update_engine_service_reset_status(UpdateEngineService* self,
                                            GError **error) {
  *error = NULL;
  return self->system_state_->update_attempter()->ResetStatus();
}


gboolean update_engine_service_get_status(UpdateEngineService* self,
                                          int64_t* last_checked_time,
                                          double* progress,
                                          gchar** current_operation,
                                          gchar** new_version,
                                          int64_t* new_size,
                                          GError **error) {
  string current_op;
  string new_version_str;

  CHECK(self->system_state_->update_attempter()->GetStatus(last_checked_time,
                                                           progress,
                                                           &current_op,
                                                           &new_version_str,
                                                           new_size));

  *current_operation = g_strdup(current_op.c_str());
  *new_version = g_strdup(new_version_str.c_str());
  if (!(*current_operation && *new_version)) {
    *error = NULL;
    return FALSE;
  }
  return TRUE;
}

gboolean update_engine_service_reboot_if_needed(UpdateEngineService* self,
                                                GError **error) {
  if (!self->system_state_->update_attempter()->RebootIfNeeded()) {
    *error = NULL;
    return FALSE;
  }
  return TRUE;
}

gboolean update_engine_service_set_track(UpdateEngineService* self,
                                         gchar* track,
                                         GError **error) {
  // track == target channel.
  return update_engine_service_set_channel(self, track, false, error);
}

gboolean update_engine_service_get_track(UpdateEngineService* self,
                                         gchar** track,
                                         GError **error) {
  // track == target channel.
  return update_engine_service_get_channel(self, false, track, error);
}

gboolean update_engine_service_set_channel(UpdateEngineService* self,
                                           gchar* target_channel,
                                           bool is_powerwash_allowed,
                                           GError **error) {
  if (!target_channel)
    return FALSE;

  if (!self->system_state_->device_policy()) {
    LOG(INFO) << "Cannot set target channel until device policy/settings are "
                 "known";
    return FALSE;
  }

  bool delegated = false;
  self->system_state_->device_policy()->GetReleaseChannelDelegated(&delegated);
  if (!delegated) {
    // Note: This message will appear in UE logs with the current UI code
    // because UI hasn't been modified to call this method only if
    // delegated is set to true. chromium-os:219292 tracks this work item.
    LOG(INFO) << "Cannot set target channel explicitly when channel "
                 "policy/settings is not delegated";
    return FALSE;
  }

  LOG(INFO) << "Setting destination channel to: " << target_channel;
  if (!self->system_state_->request_params()->SetTargetChannel(
          target_channel, is_powerwash_allowed)) {
    *error = NULL;
    return FALSE;
  }

  return TRUE;
}

gboolean update_engine_service_get_channel(UpdateEngineService* self,
                                           bool get_current_channel,
                                           gchar** channel,
                                           GError **error) {
  chromeos_update_engine::OmahaRequestParams* rp =
      self->system_state_->request_params();

  string channel_str = get_current_channel ?
      rp->current_channel() : rp->target_channel();

  *channel = g_strdup(channel_str.c_str());
  return TRUE;
}

gboolean update_engine_service_emit_status_update(
    UpdateEngineService* self,
    gint64 last_checked_time,
    gdouble progress,
    const gchar* current_operation,
    const gchar* new_version,
    gint64 new_size) {
  g_signal_emit(self,
                status_update_signal,
                0,
                last_checked_time,
                progress,
                current_operation,
                new_version,
                new_size);
  return TRUE;
}
