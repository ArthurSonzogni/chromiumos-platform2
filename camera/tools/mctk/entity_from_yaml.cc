/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Factory for an abstract model of a V4L2 media-ctl entity.
 * It will be populated with data from a YAML tree.
 *
 * The YAML tree is no longer needed once this function returns.
 *
 * Returns:
 *  - on success: A pointer to an abstract V4L2 entity.
 *  - on failure: nullptr.
 */

#include "tools/mctk/entity.h"

#include <linux/media.h>
#include <linux/types.h>
#include <linux/v4l2-mediabus.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <stddef.h> /* size_t */

#include <memory>
#include <optional>
#include <string>
#include <utility> /* std::move */
#include <vector>

#include "tools/mctk/debug.h"
#include "tools/mctk/link.h"
#include "tools/mctk/pad.h"
#include "tools/mctk/yaml_tree.h"

std::optional<struct v4l2_audio> ParseAudio(YamlNode& map) {
  struct v4l2_audio audio = {};
  bool ok = true;

  audio.index = map["index"].ReadInt<__u32>(ok);
  map["name"].ReadCString(reinterpret_cast<char*>(audio.name), 32, ok);
  audio.capability = map["capability"].ReadInt<__u32>(ok);
  audio.mode = map["mode"].ReadInt<__u32>(ok);
  if (!ok)
    return std::nullopt;

  return std::optional<struct v4l2_audio>(audio);
}

std::optional<struct v4l2_audioout> ParseAudout(YamlNode& map) {
  struct v4l2_audioout audout = {};
  bool ok = true;

  audout.index = map["index"].ReadInt<__u32>(ok);
  map["name"].ReadCString(reinterpret_cast<char*>(audout.name), 32, ok);
  audout.capability = map["capability"].ReadInt<__u32>(ok);
  audout.mode = map["mode"].ReadInt<__u32>(ok);
  if (!ok)
    return std::nullopt;

  return std::optional<struct v4l2_audioout>(audout);
}

std::optional<struct v4l2_dv_timings> ParseDvTimings(YamlNode& map) {
  struct v4l2_dv_timings dv_timings = {};
  bool ok = true;

  dv_timings.type = map["type"].ReadInt<__u32>(ok);
  dv_timings.bt.width = map["bt"]["width"].ReadInt<__u32>(ok);
  dv_timings.bt.height = map["bt"]["height"].ReadInt<__u32>(ok);
  dv_timings.bt.interlaced = map["bt"]["interlaced"].ReadInt<__u32>(ok);
  dv_timings.bt.polarities = map["bt"]["polarities"].ReadInt<__u32>(ok);
  dv_timings.bt.pixelclock = map["bt"]["pixelclock"].ReadInt<__u64>(ok);
  dv_timings.bt.hfrontporch = map["bt"]["hfrontporch"].ReadInt<__u32>(ok);
  dv_timings.bt.hsync = map["bt"]["hsync"].ReadInt<__u32>(ok);
  dv_timings.bt.hbackporch = map["bt"]["hbackporch"].ReadInt<__u32>(ok);
  dv_timings.bt.vfrontporch = map["bt"]["vfrontporch"].ReadInt<__u32>(ok);
  dv_timings.bt.vsync = map["bt"]["vsync"].ReadInt<__u32>(ok);
  dv_timings.bt.vbackporch = map["bt"]["vbackporch"].ReadInt<__u32>(ok);
  dv_timings.bt.il_vfrontporch = map["bt"]["il_vfrontporch"].ReadInt<__u32>(ok);
  dv_timings.bt.il_vsync = map["bt"]["il_vsync"].ReadInt<__u32>(ok);
  dv_timings.bt.il_vbackporch = map["bt"]["il_vbackporch"].ReadInt<__u32>(ok);
  dv_timings.bt.standards = map["bt"]["standards"].ReadInt<__u32>(ok);
  dv_timings.bt.flags = map["bt"]["flags"].ReadInt<__u32>(ok);
  dv_timings.bt.picture_aspect.numerator =
      map["bt"]["picture_aspect"]["numerator"].ReadInt<__u32>(ok);
  dv_timings.bt.picture_aspect.denominator =
      map["bt"]["picture_aspect"]["denominator"].ReadInt<__u32>(ok);
  dv_timings.bt.cea861_vic = map["bt"]["cea861_vic"].ReadInt<__u8>(ok);
  dv_timings.bt.hdmi_vic = map["bt"]["hdmi_vic"].ReadInt<__u8>(ok);
  if (!ok)
    return std::nullopt;

  return std::optional<struct v4l2_dv_timings>(dv_timings);
}

std::optional<struct v4l2_pix_format> ParsePixFormat(YamlNode& map) {
  struct v4l2_pix_format fmt_pix = {};
  bool ok = true;

  fmt_pix.width = map["width"].ReadInt<__u32>(ok);
  fmt_pix.height = map["height"].ReadInt<__u32>(ok);
  fmt_pix.pixelformat = map["pixelformat"].ReadInt<__u32>(ok);
  fmt_pix.field = map["field"].ReadInt<__u32>(ok);
  fmt_pix.bytesperline = map["bytesperline"].ReadInt<__u32>(ok);
  fmt_pix.sizeimage = map["sizeimage"].ReadInt<__u32>(ok);
  fmt_pix.colorspace = map["colorspace"].ReadInt<__u32>(ok);
  fmt_pix.priv = map["priv"].ReadInt<__u32>(ok);
  fmt_pix.flags = map["flags"].ReadInt<__u32>(ok);
  fmt_pix.ycbcr_enc = map["ycbcr_enc"].ReadInt<__u32>(ok);
  fmt_pix.quantization = map["quantization"].ReadInt<__u32>(ok);
  fmt_pix.xfer_func = map["xfer_func"].ReadInt<__u32>(ok);
  if (!ok)
    return std::nullopt;

  return std::optional<struct v4l2_pix_format>(fmt_pix);
}

std::optional<struct v4l2_vbi_format> ParseVbiFormat(YamlNode& map) {
  struct v4l2_vbi_format fmt_vbi = {};
  bool ok = true;

  fmt_vbi.sampling_rate = map["sampling_rate"].ReadInt<__u32>(ok);
  fmt_vbi.offset = map["offset"].ReadInt<__u32>(ok);
  fmt_vbi.samples_per_line = map["samples_per_line"].ReadInt<__u32>(ok);
  fmt_vbi.sample_format = map["sample_format"].ReadInt<__u32>(ok);
  fmt_vbi.start[0] = map["start"][0].ReadInt<__s32>(ok);
  fmt_vbi.start[1] = map["start"][1].ReadInt<__s32>(ok);
  fmt_vbi.start[0] = map["count"][0].ReadInt<__u32>(ok);
  fmt_vbi.start[1] = map["count"][1].ReadInt<__u32>(ok);
  fmt_vbi.flags = map["flags"].ReadInt<__u32>(ok);
  if (!ok)
    return std::nullopt;

  return std::optional<struct v4l2_vbi_format>(fmt_vbi);
}

std::optional<struct v4l2_pix_format_mplane> ParsePixFormatMplane(
    YamlNode& map) {
  struct v4l2_pix_format_mplane fmt_pix_mplane = {};
  bool ok = true;

  fmt_pix_mplane.width = map["width"].ReadInt<__u32>(ok);
  fmt_pix_mplane.height = map["height"].ReadInt<__u32>(ok);
  fmt_pix_mplane.pixelformat = map["pixelformat"].ReadInt<__u32>(ok);
  fmt_pix_mplane.field = map["field"].ReadInt<__u32>(ok);
  fmt_pix_mplane.colorspace = map["colorspace"].ReadInt<__u32>(ok);

  for (size_t i = 0; i < VIDEO_MAX_PLANES; i++) {
    fmt_pix_mplane.plane_fmt[i].sizeimage =
        map["plane_fmt"][i]["sizeimage"].ReadInt<__u32>(ok);
    fmt_pix_mplane.plane_fmt[i].bytesperline =
        map["plane_fmt"][i]["bytesperline"].ReadInt<__u32>(ok);
  }

  fmt_pix_mplane.num_planes = map["num_planes"].ReadInt<__u8>(ok);
  fmt_pix_mplane.flags = map["flags"].ReadInt<__u8>(ok);
  fmt_pix_mplane.ycbcr_enc = map["ycbcr_enc"].ReadInt<__u8>(ok);
  fmt_pix_mplane.quantization = map["quantization"].ReadInt<__u8>(ok);
  fmt_pix_mplane.xfer_func = map["xfer_func"].ReadInt<__u8>(ok);
  if (!ok)
    return std::nullopt;

  return std::optional<struct v4l2_pix_format_mplane>(fmt_pix_mplane);
}

std::optional<struct v4l2_sdr_format> ParseSdrFormat(YamlNode& map) {
  struct v4l2_sdr_format fmt_sdr = {};
  bool ok = true;

  fmt_sdr.pixelformat = map["pixelformat"].ReadInt<__u32>(ok);
  fmt_sdr.buffersize = map["buffersize"].ReadInt<__u32>(ok);
  if (!ok)
    return std::nullopt;

  return std::optional<struct v4l2_sdr_format>(fmt_sdr);
}

std::optional<struct v4l2_meta_format> ParseMetaFormat(YamlNode& map) {
  struct v4l2_meta_format fmt_meta = {};
  bool ok = true;

  fmt_meta.dataformat = map["dataformat"].ReadInt<__u32>(ok);
  fmt_meta.buffersize = map["buffersize"].ReadInt<__u32>(ok);
  if (!ok)
    return std::nullopt;

  return std::optional<struct v4l2_meta_format>(fmt_meta);
}

std::optional<struct v4l2_jpegcompression> ParseJpegcomp(YamlNode& map) {
  struct v4l2_jpegcompression jpegcomp = {};
  bool ok = true;

  jpegcomp.quality = map["quality"].ReadInt<__s32>(ok);
  jpegcomp.APPn = map["APPn"].ReadInt<__s32>(ok);
  jpegcomp.APP_len = map["APP_len"].ReadInt<__s32>(ok);
  map["APP_len"].ReadCArray<__u8>(reinterpret_cast<__u8*>(jpegcomp.APP_data),
                                  60, ok);
  jpegcomp.COM_len = map["COM_len"].ReadInt<__s32>(ok);
  map["COM_data"].ReadCArray<__u8>(reinterpret_cast<__u8*>(jpegcomp.COM_data),
                                   60, ok);
  jpegcomp.jpeg_markers = map["jpeg_markers"].ReadInt<__u32>(ok);
  if (!ok)
    return std::nullopt;

  return std::optional<struct v4l2_jpegcompression>(jpegcomp);
}

std::optional<struct v4l2_captureparm> ParseCaptureParm(YamlNode& map) {
  struct v4l2_captureparm parm = {};
  bool ok = true;

  parm.capability = map["capability"].ReadInt<__u32>(ok);
  parm.capturemode = map["capturemode"].ReadInt<__u32>(ok);
  parm.timeperframe.numerator =
      map["timeperframe"]["numerator"].ReadInt<__u32>(ok);
  parm.timeperframe.denominator =
      map["timeperframe"]["denominator"].ReadInt<__u32>(ok);
  parm.extendedmode = map["extendedmode"].ReadInt<__u32>(ok);
  parm.readbuffers = map["readbuffers"].ReadInt<__u32>(ok);
  if (!ok)
    return std::nullopt;

  return std::optional<struct v4l2_captureparm>(parm);
}

std::optional<struct v4l2_outputparm> ParseOutputParm(YamlNode& map) {
  struct v4l2_outputparm parm = {};
  bool ok = true;

  parm.capability = map["capability"].ReadInt<__u32>(ok);
  parm.outputmode = map["outputmode"].ReadInt<__u32>(ok);
  parm.timeperframe.numerator =
      map["timeperframe"]["numerator"].ReadInt<__u32>(ok);
  parm.timeperframe.denominator =
      map["timeperframe"]["denominator"].ReadInt<__u32>(ok);
  parm.extendedmode = map["extendedmode"].ReadInt<__u32>(ok);
  parm.writebuffers = map["writebuffers"].ReadInt<__u32>(ok);
  if (!ok)
    return std::nullopt;

  return std::optional<struct v4l2_outputparm>(parm);
}

std::optional<enum v4l2_priority> ParsePriority(YamlNode& scalar) {
  std::optional<__u32> temp_priority = scalar.Read<__u32>();

  if (temp_priority) {
    enum v4l2_priority retval = (enum v4l2_priority)(*temp_priority);
    return std::optional<enum v4l2_priority>(retval);
  }

  return std::nullopt;
}

std::unique_ptr<V4lMcEntity> V4lMcEntity::CreateFromYamlNode(
    YamlNode& node_ent) {
  auto entity = std::make_unique<V4lMcEntity>();

  /* Parse desc */
  bool ok = true;
  entity->desc_.id = node_ent["desc"]["id"].ReadInt<__u32>(ok);
  node_ent["desc"]["name"].ReadCString(entity->desc_.name, 32, ok);
  entity->desc_.type = node_ent["desc"]["type"].ReadInt<__u32>(ok);
  entity->desc_.revision = node_ent["desc"]["revision"].ReadInt<__u32>(ok);
  entity->desc_.flags = node_ent["desc"]["flags"].ReadInt<__u32>(ok);
  entity->desc_.group_id = node_ent["desc"]["group_id"].ReadInt<__u32>(ok);
  entity->desc_.pads = node_ent["desc"]["pads"].ReadInt<__u16>(ok);
  entity->desc_.links = node_ent["desc"]["links"].ReadInt<__u16>(ok);
  if (!ok) {
    MCTK_ERR("Entity description doesn't look right, aborting.");
    return nullptr;
  }

  /* Parse V4L properties */
  entity->maindev_.audio = ParseAudio(node_ent["v4l_properties"]["audio"]);
  entity->maindev_.audout = ParseAudout(node_ent["v4l_properties"]["audout"]);

  entity->maindev_.crop_video_capture =
      node_ent["v4l_properties"]["crop_video_capture"].ReadRect();
  entity->maindev_.crop_video_output =
      node_ent["v4l_properties"]["crop_video_output"].ReadRect();
  entity->maindev_.crop_video_overlay =
      node_ent["v4l_properties"]["crop_video_overlay"].ReadRect();
  entity->maindev_.crop_video_capture_mplane =
      node_ent["v4l_properties"]["crop_video_capture_mplane"].ReadRect();
  entity->maindev_.crop_video_output_mplane =
      node_ent["v4l_properties"]["crop_video_output_mplane"].ReadRect();

  entity->maindev_.dv_timings =
      ParseDvTimings(node_ent["v4l_properties"]["dv_timings"]);
  entity->maindev_.subdev_dv_timings =
      ParseDvTimings(node_ent["v4l_properties"]["subdev_dv_timings"]);

  /* Ignored: EDID */

  /* struct v4l2_framebuffer is not (de)serialisable */

  entity->maindev_.fmt_video_capture =
      ParsePixFormat(node_ent["v4l_properties"]["fmt_video_capture"]);
  entity->maindev_.fmt_video_output =
      ParsePixFormat(node_ent["v4l_properties"]["fmt_video_output"]);
  /* struct v4l2_window is not (de)serialisable */
  MCTK_ASSERT(node_ent["v4l_properties"]["fmt_video_overlay"].IsEmpty());
  entity->maindev_.fmt_vbi_capture =
      ParseVbiFormat(node_ent["v4l_properties"]["fmt_vbi_capture"]);
  entity->maindev_.fmt_vbi_output =
      ParseVbiFormat(node_ent["v4l_properties"]["fmt_vbi_output"]);
  /* Sliced VBI does not have a stable UAPI, so don't try to handle it */
  MCTK_ASSERT(node_ent["v4l_properties"]["fmt_sliced_vbi_capture"].IsEmpty());
  MCTK_ASSERT(node_ent["v4l_properties"]["fmt_sliced_vbi_output"].IsEmpty());
  /* struct v4l2_window is not (de)serialisable */
  MCTK_ASSERT(node_ent["v4l_properties"]["fmt_video_output_overlay"].IsEmpty());
  entity->maindev_.fmt_video_capture_mplane = ParsePixFormatMplane(
      node_ent["v4l_properties"]["fmt_video_capture_mplane"]);
  entity->maindev_.fmt_video_output_mplane = ParsePixFormatMplane(
      node_ent["v4l_properties"]["fmt_video_output_mplane"]);
  entity->maindev_.fmt_sdr_capture =
      ParseSdrFormat(node_ent["v4l_properties"]["fmt_sdr_capture"]);
  entity->maindev_.fmt_sdr_output =
      ParseSdrFormat(node_ent["v4l_properties"]["fmt_sdr_output"]);
  entity->maindev_.fmt_meta_capture =
      ParseMetaFormat(node_ent["v4l_properties"]["fmt_meta_capture"]);
  entity->maindev_.fmt_meta_output =
      ParseMetaFormat(node_ent["v4l_properties"]["fmt_meta_output"]);

  /* Ignored: Frequency */

  entity->maindev_.input = node_ent["v4l_properties"]["input"].Read<__s32>();
  entity->maindev_.jpegcomp =
      ParseJpegcomp(node_ent["v4l_properties"]["jpegcomp"]);

  /* Ignored: modulator */

  entity->maindev_.output = node_ent["v4l_properties"]["output"].Read<__s32>();

  entity->maindev_.parm_video_capture =
      ParseCaptureParm(node_ent["v4l_properties"]["parm_video_capture"]);
  entity->maindev_.parm_video_output =
      ParseOutputParm(node_ent["v4l_properties"]["parm_video_output"]);
  entity->maindev_.parm_video_overlay =
      ParseOutputParm(node_ent["v4l_properties"]["parm_video_overlay"]);
  entity->maindev_.parm_vbi_capture =
      ParseCaptureParm(node_ent["v4l_properties"]["parm_vbi_capture"]);
  entity->maindev_.parm_vbi_output =
      ParseOutputParm(node_ent["v4l_properties"]["parm_vbi_output"]);
  entity->maindev_.parm_sliced_vbi_capture =
      ParseCaptureParm(node_ent["v4l_properties"]["parm_sliced_vbi_capture"]);
  entity->maindev_.parm_sliced_vbi_output =
      ParseOutputParm(node_ent["v4l_properties"]["parm_sliced_vbi_output"]);
  entity->maindev_.parm_video_output_overlay =
      ParseOutputParm(node_ent["v4l_properties"]["parm_video_output_overlay"]);
  entity->maindev_.parm_video_capture_mplane =
      ParseCaptureParm(node_ent["v4l_properties"]["parm_video_capture_mplane"]);
  entity->maindev_.parm_video_output_mplane =
      ParseOutputParm(node_ent["v4l_properties"]["parm_video_output_mplane"]);
  entity->maindev_.parm_sdr_capture =
      ParseCaptureParm(node_ent["v4l_properties"]["parm_sdr_capture"]);
  entity->maindev_.parm_sdr_output =
      ParseOutputParm(node_ent["v4l_properties"]["parm_sdr_output"]);
  entity->maindev_.parm_meta_capture =
      ParseCaptureParm(node_ent["v4l_properties"]["parm_meta_capture"]);
  entity->maindev_.parm_meta_output =
      ParseOutputParm(node_ent["v4l_properties"]["parm_meta_output"]);

  entity->maindev_.priority =
      ParsePriority(node_ent["v4l_properties"]["priority"]);

  for (size_t i = 1; i <= 14; i++) {
    YamlNode& node = node_ent["v4l_properties"]["selection"][std::to_string(i)];

    node.ReadSelection(entity->maindev_.selection[i - 1]);
  }

  entity->maindev_.std = node_ent["v4l_properties"]["std"].Read<__u64>();
  entity->maindev_.subdev_std =
      node_ent["v4l_properties"]["subdev_std"].Read<__u64>();

  /* Ignored: tuner */

  /* Parse controls */
  for (YamlNode* node_control : node_ent["controls"].ReadSequence()) {
    std::unique_ptr<V4lMcControl> new_control =
        V4lMcControl::CreateFromYamlNode(*node_control);

    if (!new_control) {
      MCTK_ASSERT(0);
      return nullptr;
    }

    entity->controls_.push_back(std::move(new_control));
  }

  /* Parse pads */
  for (YamlNode* node_pad : node_ent["pads"].ReadSequence()) {
    std::unique_ptr<V4lMcPad> new_pad =
        V4lMcPad::CreateFromYamlNode(*node_pad, *entity);

    if (!new_pad) {
      /* NOTE:
       * Since CreateFromYamlNode() manipulates entity->links_,
       * we MUST abort if CreateFromYamlNode() fails,
       * otherwise we are left with stale links.
       */
      MCTK_ERR("Failed to create pad from YAML node.");
      return nullptr;
    }

    entity->pads_.push_back(std::move(new_pad));
  }

  return entity;
}
