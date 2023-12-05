/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Merge:
 * This is Tool-3's heart.
 * This is where values from one model are copied into a different model.
 *
 * If the target model has access to kernel device files, then the parameters
 * will be applied to a real device as well.
 */

// NOLINTNEXTLINE(build/include)
#include "tools/mctk/merge.h"

#include <linux/media.h>
#include <linux/videodev2.h>

#include <optional>
#include <string>
#include <vector>

#include "tools/mctk/debug.h"
#include "tools/mctk/mcdev.h"

namespace {

bool MergeControl(V4lMcControl& tc, class V4lMcControl& sc) {
  MCTK_ASSERT(tc.desc_.id == sc.desc_.id);
  MCTK_ASSERT(tc.desc_.type == sc.desc_.type);
  MCTK_ASSERT(tc.desc_.flags == sc.desc_.flags);
  MCTK_ASSERT(tc.desc_.elem_size == sc.desc_.elem_size);
  MCTK_ASSERT(tc.desc_.nr_of_dims == sc.desc_.nr_of_dims);
  MCTK_ASSERT(tc.desc_.dims[0] == sc.desc_.dims[0]);
  MCTK_ASSERT(tc.desc_.dims[1] == sc.desc_.dims[1]);
  MCTK_ASSERT(tc.desc_.dims[2] == sc.desc_.dims[2]);
  MCTK_ASSERT(tc.desc_.dims[3] == sc.desc_.dims[3]);

  bool ok;

  switch (sc.desc_.type) {
    case V4L2_CTRL_TYPE_INTEGER:
    case V4L2_CTRL_TYPE_BOOLEAN:
    case V4L2_CTRL_TYPE_MENU:
    case V4L2_CTRL_TYPE_BUTTON:
    case V4L2_CTRL_TYPE_BITMASK:
    case V4L2_CTRL_TYPE_INTEGER_MENU:
      ok = tc.Set(sc.values_s32_);
      break;
    case V4L2_CTRL_TYPE_INTEGER64:
      ok = tc.Set(sc.values_s64_);
      break;
    case V4L2_CTRL_TYPE_STRING:
      ok = tc.Set(sc.values_string_);
      break;
    case V4L2_CTRL_TYPE_U8:
      ok = tc.Set(sc.values_u8_);
      break;
    case V4L2_CTRL_TYPE_U16:
      ok = tc.Set(sc.values_u16_);
      break;
    case V4L2_CTRL_TYPE_U32:
      ok = tc.Set(sc.values_u32_);
      break;
#ifdef V4L2_CTRL_TYPE_AREA
    case V4L2_CTRL_TYPE_AREA:
      ok = tc.Set(sc.values_area_);
      break;
#endif /* V4L2_CTRL_TYPE_AREA */
    case V4L2_CTRL_TYPE_CTRL_CLASS:
      MCTK_ASSERT(0);
      /* fall-through */
    default:
      MCTK_PANIC("Unmergeable control type encountered");
  }

  if (!ok) {
    MCTK_ERR("Setting control failed");
  }

  return true;
}

}  // namespace

bool V4lMcMergeMcDev(V4lMcDev& target, V4lMcDev& source, V4lMcRemap* remap) {
  /* Check: See if target contains all entities */
  for (auto& se : source.entities_) {
    if (!target.EntityById(se->desc_.id)) {
      MCTK_PANIC("Merge: target lacking an entity mentioned by source.");
    }
  }

  /* Check: See if target contains all pads */
  for (auto& se : source.entities_) {
    __u32 te_id = se->desc_.id;
    if (remap)
      te_id = remap->LookupEntityId(te_id, target);

    auto te = target.EntityById(te_id);

    for (auto& sp : se->pads_) {
      if (!te->PadByIndex(sp->desc_.index)) {
        MCTK_PANIC("Merge: target lacking a pad mentioned by source.");
      }
    }
  }

  /* Check: See if target contains all links */
  for (auto& se : source.entities_) {
    __u32 te_id = se->desc_.id;
    if (remap)
      te_id = remap->LookupEntityId(te_id, target);

    auto te = target.EntityById(te_id);

    for (auto& sp : se->pads_) {
      auto tp = te->PadByIndex(sp->desc_.index);

      for (auto* sl : sp->links_) {
        __u32 sink_entity_id = sl->desc_.sink.entity;
        if (remap)
          sink_entity_id = remap->LookupEntityId(sink_entity_id, target);

        auto tl = tp->LinkBySinkIds(sink_entity_id, sl->desc_.sink.index);
        if (!tl) {
          MCTK_PANIC("Merge: target lacking a link mentioned by source.");
        }
      }
    }
  }

  /* Check: See if target contains all maindev properties */
  for (auto& se : source.entities_) {
    __u32 te_id = se->desc_.id;
    if (remap)
      te_id = remap->LookupEntityId(te_id, target);

    auto te = target.EntityById(te_id);

    if ((!te->maindev_.audio && se->maindev_.audio) ||
        (!te->maindev_.audout && se->maindev_.audout) ||
        (!te->maindev_.crop_video_capture && se->maindev_.crop_video_capture) ||
        (!te->maindev_.crop_video_output && se->maindev_.crop_video_output) ||
        (!te->maindev_.crop_video_overlay && se->maindev_.crop_video_overlay) ||
        (!te->maindev_.crop_video_capture_mplane &&
         se->maindev_.crop_video_capture_mplane) ||
        (!te->maindev_.crop_video_output_mplane &&
         se->maindev_.crop_video_output_mplane) ||
        (!te->maindev_.dv_timings && se->maindev_.dv_timings) ||
        (!te->maindev_.subdev_dv_timings && se->maindev_.subdev_dv_timings) ||
        (!te->maindev_.fbuf && se->maindev_.fbuf) ||
        (!te->maindev_.fmt_video_capture && se->maindev_.fmt_video_capture) ||
        (!te->maindev_.fmt_video_output && se->maindev_.fmt_video_output) ||
        (!te->maindev_.fmt_video_overlay && se->maindev_.fmt_video_overlay) ||
        (!te->maindev_.fmt_vbi_capture && se->maindev_.fmt_vbi_capture) ||
        (!te->maindev_.fmt_vbi_output && se->maindev_.fmt_vbi_output) ||
        (!te->maindev_.fmt_sliced_vbi_capture &&
         se->maindev_.fmt_sliced_vbi_capture) ||
        (!te->maindev_.fmt_sliced_vbi_output &&
         se->maindev_.fmt_sliced_vbi_output) ||
        (!te->maindev_.fmt_video_output_overlay &&
         se->maindev_.fmt_video_output_overlay) ||
        (!te->maindev_.fmt_video_capture_mplane &&
         se->maindev_.fmt_video_capture_mplane) ||
        (!te->maindev_.fmt_video_output_mplane &&
         se->maindev_.fmt_video_output_mplane) ||
        (!te->maindev_.fmt_sdr_capture && se->maindev_.fmt_sdr_capture) ||
        (!te->maindev_.fmt_sdr_output && se->maindev_.fmt_sdr_output) ||
        (!te->maindev_.fmt_meta_capture && se->maindev_.fmt_meta_capture) ||
        (!te->maindev_.fmt_meta_output && se->maindev_.fmt_meta_output) ||
        (!te->maindev_.input && se->maindev_.input) ||
        (!te->maindev_.jpegcomp && se->maindev_.jpegcomp) ||
        (!te->maindev_.output && se->maindev_.output) ||
        (!te->maindev_.parm_video_capture && se->maindev_.parm_video_capture) ||
        (!te->maindev_.parm_video_output && se->maindev_.parm_video_output) ||
        (!te->maindev_.parm_video_overlay && se->maindev_.parm_video_overlay) ||
        (!te->maindev_.parm_vbi_capture && se->maindev_.parm_vbi_capture) ||
        (!te->maindev_.parm_vbi_output && se->maindev_.parm_vbi_output) ||
        (!te->maindev_.parm_sliced_vbi_capture &&
         se->maindev_.parm_sliced_vbi_capture) ||
        (!te->maindev_.parm_sliced_vbi_output &&
         se->maindev_.parm_sliced_vbi_output) ||
        (!te->maindev_.parm_video_output_overlay &&
         se->maindev_.parm_video_output_overlay) ||
        (!te->maindev_.parm_video_capture_mplane &&
         se->maindev_.parm_video_capture_mplane) ||
        (!te->maindev_.parm_video_output_mplane &&
         se->maindev_.parm_video_output_mplane) ||
        (!te->maindev_.parm_sdr_capture && se->maindev_.parm_sdr_capture) ||
        (!te->maindev_.parm_sdr_output && se->maindev_.parm_sdr_output) ||
        (!te->maindev_.parm_meta_capture && se->maindev_.parm_meta_capture) ||
        (!te->maindev_.parm_meta_output && se->maindev_.parm_meta_output) ||
        (!te->maindev_.priority && se->maindev_.priority) ||
        (!te->maindev_.std && se->maindev_.std) ||
        (!te->maindev_.subdev_std && se->maindev_.subdev_std)) {
      MCTK_PANIC("Merge: target lacking a maindev prop mentioned by source.");
    }

    for (__u32 type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
         type <= V4L2_BUF_TYPE_META_OUTPUT; type++) {
      if ((!te->maindev_.selection[type - 1].crop_ &&
           se->maindev_.selection[type - 1].crop_) ||
          (!te->maindev_.selection[type - 1].crop_default_ &&
           se->maindev_.selection[type - 1].crop_default_) ||
          (!te->maindev_.selection[type - 1].crop_bounds_ &&
           se->maindev_.selection[type - 1].crop_bounds_) ||
          (!te->maindev_.selection[type - 1].native_size_ &&
           se->maindev_.selection[type - 1].native_size_) ||
          (!te->maindev_.selection[type - 1].compose_ &&
           se->maindev_.selection[type - 1].compose_) ||
          (!te->maindev_.selection[type - 1].compose_default_ &&
           se->maindev_.selection[type - 1].compose_default_) ||
          (!te->maindev_.selection[type - 1].compose_bounds_ &&
           se->maindev_.selection[type - 1].compose_bounds_) ||
          (!te->maindev_.selection[type - 1].compose_padded_ &&
           se->maindev_.selection[type - 1].compose_padded_)) {
        MCTK_PANIC(
            "Merge: target lacking a maindev selection "
            "mentioned by source.");
      }
    }
  }

  /* Check: See if target contains all controls */
  for (auto& se : source.entities_) {
    __u32 te_id = se->desc_.id;
    if (remap)
      te_id = remap->LookupEntityId(te_id, target);

    auto te = target.EntityById(te_id);

    for (auto& sc : se->controls_) {
      auto tc = te->ControlById(sc->desc_.id);

      if (!tc) {
        MCTK_PANIC("Merge: target lacking a control mentioned by source.");
      }
    }
  }

  /* Check: See if target contains all subdev properties */
  for (auto& se : source.entities_) {
    __u32 te_id = se->desc_.id;
    if (remap)
      te_id = remap->LookupEntityId(te_id, target);

    auto te = target.EntityById(te_id);

    for (auto& sp : se->pads_) {
      auto tp = te->PadByIndex(sp->desc_.index);

      if ((!tp->subdev_.crop && sp->subdev_.crop) ||
          (!tp->subdev_.fmt && sp->subdev_.fmt) ||
          (!tp->subdev_.frame_interval && sp->subdev_.frame_interval)) {
        MCTK_PANIC("Merge: target lacking a subdev prop mentioned by source.");
      }

      if ((!tp->subdev_.selection.crop_ && sp->subdev_.selection.crop_) ||
          (!tp->subdev_.selection.crop_default_ &&
           sp->subdev_.selection.crop_default_) ||
          (!tp->subdev_.selection.crop_bounds_ &&
           sp->subdev_.selection.crop_bounds_) ||
          (!tp->subdev_.selection.native_size_ &&
           sp->subdev_.selection.native_size_) ||
          (!tp->subdev_.selection.compose_ && sp->subdev_.selection.compose_) ||
          (!tp->subdev_.selection.compose_default_ &&
           sp->subdev_.selection.compose_default_) ||
          (!tp->subdev_.selection.compose_bounds_ &&
           sp->subdev_.selection.compose_bounds_) ||
          (!tp->subdev_.selection.compose_padded_ &&
           sp->subdev_.selection.compose_padded_)) {
        MCTK_PANIC(
            "Merge: target lacking a subdev selection "
            "mentioned by source.");
      }
    }
  }

  /* Merge */
  for (auto& se : source.entities_) {
    __u32 te_id = se->desc_.id;
    if (remap)
      te_id = remap->LookupEntityId(te_id, target);

    auto te = target.EntityById(te_id);

    /* Merge maindev properties */
    if (se->maindev_.audio)
      te->SetAudio(*se->maindev_.audio);

    if (se->maindev_.audout)
      te->SetAudout(*se->maindev_.audout);

    if (se->maindev_.crop_video_capture)
      te->SetCrop(V4L2_BUF_TYPE_VIDEO_CAPTURE,
                  *se->maindev_.crop_video_capture);

    if (se->maindev_.crop_video_output)
      te->SetCrop(V4L2_BUF_TYPE_VIDEO_OUTPUT, *se->maindev_.crop_video_output);

    if (se->maindev_.crop_video_overlay)
      te->SetCrop(V4L2_BUF_TYPE_VIDEO_OVERLAY,
                  *se->maindev_.crop_video_overlay);

    if (se->maindev_.crop_video_capture_mplane)
      te->SetCrop(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                  *se->maindev_.crop_video_capture_mplane);

    if (se->maindev_.dv_timings)
      te->SetDvTimings(*se->maindev_.dv_timings);

    if (se->maindev_.subdev_dv_timings)
      te->SetSubdevDvTimings(*se->maindev_.subdev_dv_timings);

    /* Ignored: VIDIOC_S_EDID */
    /* Ignored: VIDIOC_SUBDEV_S_EDID */

    if (se->maindev_.fbuf)
      te->SetFbuf(*se->maindev_.fbuf);

    if (se->maindev_.fmt_video_capture)
      te->SetFmtVideoCapture(*se->maindev_.fmt_video_capture);

    if (se->maindev_.fmt_video_output)
      te->SetFmtVideoOutput(*se->maindev_.fmt_video_output);

    if (se->maindev_.fmt_video_overlay)
      te->SetFmtVideoOverlay(*se->maindev_.fmt_video_overlay);

    if (se->maindev_.fmt_vbi_capture)
      te->SetFmtVbiCapture(*se->maindev_.fmt_vbi_capture);

    if (se->maindev_.fmt_vbi_output)
      te->SetFmtVbiOutput(*se->maindev_.fmt_vbi_output);

    if (se->maindev_.fmt_sliced_vbi_capture)
      te->SetFmtSlicedVbiCapture(*se->maindev_.fmt_sliced_vbi_capture);

    if (se->maindev_.fmt_sliced_vbi_output)
      te->SetFmtSlicedVbiOutput(*se->maindev_.fmt_sliced_vbi_output);

    if (se->maindev_.fmt_video_output_overlay)
      te->SetFmtVideoOutputOverlay(*se->maindev_.fmt_video_output_overlay);

    if (se->maindev_.fmt_video_capture_mplane)
      te->SetFmtVideoCaptureMplane(*se->maindev_.fmt_video_capture_mplane);

    if (se->maindev_.fmt_video_output_mplane)
      te->SetFmtVideoOutputMplane(*se->maindev_.fmt_video_output_mplane);

    if (se->maindev_.fmt_sdr_capture)
      te->SetFmtSdrCapture(*se->maindev_.fmt_sdr_capture);

    if (se->maindev_.fmt_sdr_output)
      te->SetFmtSdrOutput(*se->maindev_.fmt_sdr_output);

    if (se->maindev_.fmt_meta_capture)
      te->SetFmtMetaCapture(*se->maindev_.fmt_meta_capture);

    if (se->maindev_.fmt_meta_output)
      te->SetFmtMetaOutput(*se->maindev_.fmt_meta_output);

    /* Ignored: VIDIOC_S_FREQUENCY */

    if (se->maindev_.input)
      te->SetInput(*se->maindev_.input);

    if (se->maindev_.jpegcomp)
      te->SetJpegcomp(*se->maindev_.jpegcomp);

    /* Ignored: VIDIOC_S_MODULATOR */

    if (se->maindev_.output)
      te->SetOutput(*se->maindev_.output);

    if (se->maindev_.parm_video_capture)
      te->SetParmVideoCapture(*se->maindev_.parm_video_capture);

    if (se->maindev_.parm_video_output)
      te->SetParmVideoOutput(*se->maindev_.parm_video_output);

    if (se->maindev_.parm_video_overlay)
      te->SetParmVideoOverlay(*se->maindev_.parm_video_overlay);

    if (se->maindev_.parm_vbi_capture)
      te->SetParmVbiCapture(*se->maindev_.parm_vbi_capture);

    if (se->maindev_.parm_vbi_output)
      te->SetParmVbiOutput(*se->maindev_.parm_vbi_output);

    if (se->maindev_.parm_sliced_vbi_capture)
      te->SetParmSlicedVbiCapture(*se->maindev_.parm_sliced_vbi_capture);

    if (se->maindev_.parm_sliced_vbi_output)
      te->SetParmSlicedVbiOutput(*se->maindev_.parm_sliced_vbi_output);

    if (se->maindev_.parm_video_output_overlay)
      te->SetParmVideoOutputOverlay(*se->maindev_.parm_video_output_overlay);

    if (se->maindev_.parm_video_capture_mplane)
      te->SetParmVideoCaptureMplane(*se->maindev_.parm_video_capture_mplane);

    if (se->maindev_.parm_video_output_mplane)
      te->SetParmVideoOutputMplane(*se->maindev_.parm_video_output_mplane);

    if (se->maindev_.parm_sdr_capture)
      te->SetParmSdrCapture(*se->maindev_.parm_sdr_capture);

    if (se->maindev_.parm_sdr_output)
      te->SetParmSdrOutput(*se->maindev_.parm_sdr_output);

    if (se->maindev_.parm_meta_capture)
      te->SetParmMetaCapture(*se->maindev_.parm_meta_capture);

    if (se->maindev_.parm_meta_output)
      te->SetParmMetaOutput(*se->maindev_.parm_meta_output);

    if (se->maindev_.priority)
      te->SetPriority(*se->maindev_.priority);

    for (__u32 type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
         type <= V4L2_BUF_TYPE_META_OUTPUT; type++) {
      if (se->maindev_.selection[type - 1].crop_)
        te->SetSelection(type, V4L2_SEL_TGT_CROP,
                         *se->maindev_.selection[type - 1].crop_);

      if (se->maindev_.selection[type - 1].crop_default_)
        te->SetSelection(type, V4L2_SEL_TGT_CROP_DEFAULT,
                         *se->maindev_.selection[type - 1].crop_default_);

      if (se->maindev_.selection[type - 1].crop_bounds_)
        te->SetSelection(type, V4L2_SEL_TGT_CROP_BOUNDS,
                         *se->maindev_.selection[type - 1].crop_bounds_);

      if (se->maindev_.selection[type - 1].native_size_)
        te->SetSelection(type, V4L2_SEL_TGT_NATIVE_SIZE,
                         *se->maindev_.selection[type - 1].native_size_);

      if (se->maindev_.selection[type - 1].compose_)
        te->SetSelection(type, V4L2_SEL_TGT_COMPOSE,
                         *se->maindev_.selection[type - 1].compose_);

      if (se->maindev_.selection[type - 1].compose_default_)
        te->SetSelection(type, V4L2_SEL_TGT_COMPOSE_DEFAULT,
                         *se->maindev_.selection[type - 1].compose_default_);

      if (se->maindev_.selection[type - 1].compose_bounds_)
        te->SetSelection(type, V4L2_SEL_TGT_COMPOSE_BOUNDS,
                         *se->maindev_.selection[type - 1].compose_bounds_);

      if (se->maindev_.selection[type - 1].compose_padded_)
        te->SetSelection(type, V4L2_SEL_TGT_COMPOSE_PADDED,
                         *se->maindev_.selection[type - 1].compose_padded_);
    }

    if (se->maindev_.std)
      te->SetStd(*se->maindev_.std);

    if (se->maindev_.subdev_std)
      te->SetSubdevStd(*se->maindev_.subdev_std);

    /* Ignored: VIDIOC_S_TUNER */

    /* Merge controls */
    for (auto& sc : se->controls_) {
      auto tc = te->ControlById(sc->desc_.id);
      if (!tc) {
        MCTK_ERR("Target control not found. Skipping control.");
        continue;
      }

      if (!MergeControl(*tc, *sc)) {
        MCTK_ERR("Control failed to merge. Continuing...");
        continue;
      }
    }

    /* Merge pad properties */
    for (auto& sp : se->pads_) {
      auto tp = te->PadByIndex(sp->desc_.index);

      /* Merge subdev properties */
      if (sp->subdev_.crop)
        tp->SetCrop(*sp->subdev_.crop);

      if (sp->subdev_.fmt)
        tp->SetFmt(*sp->subdev_.fmt);

      if (sp->subdev_.frame_interval)
        tp->SetFrameInterval(*sp->subdev_.frame_interval);

      if (sp->subdev_.selection.crop_)
        tp->SetSelection(V4L2_SEL_TGT_CROP, *sp->subdev_.selection.crop_);

      if (sp->subdev_.selection.crop_default_)
        tp->SetSelection(V4L2_SEL_TGT_CROP_DEFAULT,
                         *sp->subdev_.selection.crop_default_);

      if (sp->subdev_.selection.crop_bounds_)
        tp->SetSelection(V4L2_SEL_TGT_CROP_BOUNDS,
                         *sp->subdev_.selection.crop_bounds_);

      if (sp->subdev_.selection.native_size_)
        tp->SetSelection(V4L2_SEL_TGT_NATIVE_SIZE,
                         *sp->subdev_.selection.native_size_);

      if (sp->subdev_.selection.compose_)
        tp->SetSelection(V4L2_SEL_TGT_COMPOSE, *sp->subdev_.selection.compose_);

      if (sp->subdev_.selection.compose_default_)
        tp->SetSelection(V4L2_SEL_TGT_COMPOSE_DEFAULT,
                         *sp->subdev_.selection.compose_default_);

      if (sp->subdev_.selection.compose_bounds_)
        tp->SetSelection(V4L2_SEL_TGT_COMPOSE_BOUNDS,
                         *sp->subdev_.selection.compose_bounds_);

      if (sp->subdev_.selection.compose_padded_)
        tp->SetSelection(V4L2_SEL_TGT_COMPOSE_PADDED,
                         *sp->subdev_.selection.compose_padded_);

      /* Merge links */
      for (auto* sl : sp->links_) {
        __u32 sink_entity_id = sl->desc_.sink.entity;
        if (remap)
          sink_entity_id = remap->LookupEntityId(sink_entity_id, target);

        auto tl = tp->LinkBySinkIds(sink_entity_id, sl->desc_.sink.index);
        if (!tl) {
          MCTK_ERR("Target link not found. Skipping link.");
          continue;
        }

        tl->SetEnable(sl->IsEnabled());
      }
    }
  }

  return true;
}
