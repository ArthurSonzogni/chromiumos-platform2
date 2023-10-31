/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Wrapper class capturing a snapshot of the description and properties of
 * a media-ctl "entity" and its twinned V4L2 device.
 *
 * Setter functions primarily update the state in the class.
 * If fd_ is set to an fd to the V4L2 (sub)device, the matching ioctl()s
 * are sent to the kernel, programming the updated values into the driver.
 *
 * If fd_ is set, this class owns it and will close it upon destruction.
 */

#ifndef CAMERA_TOOLS_MCTK_ENTITY_H_
#define CAMERA_TOOLS_MCTK_ENTITY_H_

#include <linux/media.h>
#include <linux/v4l2-subdev.h> /* VIDIOC_SUBDEV_S_STD */
#include <linux/videodev2.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tools/mctk/control.h"
#include "tools/mctk/link.h"
#include "tools/mctk/pad.h"
#include "tools/mctk/selection.h"
#include "tools/mctk/yaml_tree.h"

class V4lMcEntity {
 public:
  /* Public functions */

  /* This constructor should be private, but that forces hacks to make
   * unique_ptr work. So let's keep it public, but please use the
   * factory functions instead of the constructor directly!
   */
  V4lMcEntity() : desc_({}) {}

  ~V4lMcEntity();

  /* Lookup functions for child nodes */
  V4lMcControl* ControlById(__u32 id);
  V4lMcPad* PadByIndex(__u16 index);

  /* Factory functions */
  static std::unique_ptr<V4lMcEntity> CreateFromKernel(
      struct media_entity_desc& desc, int fd_mc);
  static std::unique_ptr<V4lMcEntity> CreateFromYamlNode(YamlNode& node_ent);

  /* Setters for classic V4L2 properties */
  bool SetAudio(struct v4l2_audio& audio);
  bool SetAudout(struct v4l2_audioout& audout);
  bool SetCrop(__u32 type, struct v4l2_rect& c);
  bool SetDvTimings(struct v4l2_dv_timings& dv_timings);
  bool SetSubdevDvTimings(struct v4l2_dv_timings& dv_timings);
  bool SetFbuf(struct v4l2_framebuffer& fbuf);
  bool SetFmtVideoCapture(struct v4l2_pix_format& pix);
  bool SetFmtVideoOutput(struct v4l2_pix_format& pix);
  bool SetFmtVideoOverlay(struct v4l2_window& win);
  bool SetFmtVbiCapture(struct v4l2_vbi_format& vbi);
  bool SetFmtVbiOutput(struct v4l2_vbi_format& vbi);
  bool SetFmtSlicedVbiCapture(struct v4l2_sliced_vbi_format& sliced);
  bool SetFmtSlicedVbiOutput(struct v4l2_sliced_vbi_format& sliced);
  bool SetFmtVideoOutputOverlay(struct v4l2_window& win);
  bool SetFmtVideoCaptureMplane(struct v4l2_pix_format_mplane& pix_mp);
  bool SetFmtVideoOutputMplane(struct v4l2_pix_format_mplane& pix_mp);
  bool SetFmtSdrCapture(struct v4l2_sdr_format& sdr);
  bool SetFmtSdrOutput(struct v4l2_sdr_format& sdr);
  bool SetFmtMetaCapture(struct v4l2_meta_format& meta);
  bool SetFmtMetaOutput(struct v4l2_meta_format& meta);
  bool SetInput(int input);
  bool SetJpegcomp(struct v4l2_jpegcompression& jpegcomp);
  bool SetOutput(int output);
  bool SetParmVideoCapture(struct v4l2_captureparm& capture);
  bool SetParmVideoOutput(struct v4l2_outputparm& output);
  bool SetParmVideoOverlay(struct v4l2_outputparm& output);
  bool SetParmVbiCapture(struct v4l2_captureparm& capture);
  bool SetParmVbiOutput(struct v4l2_outputparm& output);
  bool SetParmSlicedVbiCapture(struct v4l2_captureparm& capture);
  bool SetParmSlicedVbiOutput(struct v4l2_outputparm& output);
  bool SetParmVideoOutputOverlay(struct v4l2_outputparm& output);
  bool SetParmVideoCaptureMplane(struct v4l2_captureparm& capture);
  bool SetParmVideoOutputMplane(struct v4l2_outputparm& output);
  bool SetParmSdrCapture(struct v4l2_captureparm& capture);
  bool SetParmSdrOutput(struct v4l2_outputparm& output);
  bool SetParmMetaCapture(struct v4l2_captureparm& capture);
  bool SetParmMetaOutput(struct v4l2_outputparm& output);
  bool SetPriority(enum v4l2_priority priority);
  bool SetSelection(__u32 type, __u32 target, struct v4l2_rect& r);
  bool SetStd(v4l2_std_id std);
  bool SetSubdevStd(v4l2_std_id subdev_std);

  /* Public variables */

  /* Entity description, as per MEDIA_IOC_ENUM_ENTITIES */
  struct media_entity_desc desc_;

  /* Classic V4L2 properties */
  struct {
    /* VIDIOC_G_AUDIO */
    std::optional<struct v4l2_audio> audio;

    /* VIDIOC_G_AUDOUT */
    std::optional<struct v4l2_audioout> audout;

    /* VIDIOC_G_CROP */
    std::optional<struct v4l2_rect> crop_video_capture;
    std::optional<struct v4l2_rect> crop_video_output;
    std::optional<struct v4l2_rect> crop_video_overlay;
    std::optional<struct v4l2_rect> crop_video_capture_mplane;
    std::optional<struct v4l2_rect> crop_video_output_mplane;

    /* Ignored: VIDIOC_G_CTRL */
    /* We do VIDIOC_G_EXT_CTRLS instead */

    /* VIDIOC_G_DV_TIMINGS */
    std::optional<struct v4l2_dv_timings> dv_timings;

    /* VIDIOC_SUBDEV_G_DV_TIMINGS */
    std::optional<struct v4l2_dv_timings> subdev_dv_timings;

    /* Ignored: VIDIOC_G_EDID */
    /* Ignored: VIDIOC_SUBDEV_G_EDID */

    /* Ignored: VIDIOC_G_ENC_INDEX */
    /* Irrelevant: Outdated and not a device configuration */

    /* Separate: VIDIOC_G_EXT_CTRLS */
    /* See elsewhere in this class */

    /* VIDIOC_G_FBUF */
    std::optional<struct v4l2_framebuffer> fbuf;

    /* VIDIOC_G_FMT */
    /* This ioctl() handles a different struct for each v4l2_buf_type,
     * hence the seemingly duplicated code - it is not.
     */
    std::optional<struct v4l2_pix_format> fmt_video_capture;
    std::optional<struct v4l2_pix_format> fmt_video_output;
    std::optional<struct v4l2_window> fmt_video_overlay;
    std::optional<struct v4l2_vbi_format> fmt_vbi_capture;
    std::optional<struct v4l2_vbi_format> fmt_vbi_output;
    std::optional<struct v4l2_sliced_vbi_format> fmt_sliced_vbi_capture;
    std::optional<struct v4l2_sliced_vbi_format> fmt_sliced_vbi_output;
    std::optional<struct v4l2_window> fmt_video_output_overlay;
    std::optional<struct v4l2_pix_format_mplane> fmt_video_capture_mplane;
    std::optional<struct v4l2_pix_format_mplane> fmt_video_output_mplane;
    std::optional<struct v4l2_sdr_format> fmt_sdr_capture;
    std::optional<struct v4l2_sdr_format> fmt_sdr_output;
    std::optional<struct v4l2_meta_format> fmt_meta_capture;
    std::optional<struct v4l2_meta_format> fmt_meta_output;

    /* Ignored: VIDIOC_G_FREQUENCY */

    /* VIDIOC_G_INPUT */
    std::optional<int> input;

    /* VIDIOC_G_JPEGCOMP */
    std::optional<struct v4l2_jpegcompression> jpegcomp;

    /* Ignored: VIDIOC_G_MODULATOR */
    std::vector<struct v4l2_modulator> modulators;
    std::vector<__u32> modulator_freq;

    /* VIDIOC_G_OUTPUT */
    std::optional<int> output;

    /* VIDIOC_G_PARM */
    /* This ioctl() handles a different struct for each v4l2_buf_type,
     * hence the seemingly duplicated code - it is not.
     */
    std::optional<struct v4l2_captureparm> parm_video_capture;
    std::optional<struct v4l2_outputparm> parm_video_output;
    std::optional<struct v4l2_outputparm> parm_video_overlay;
    std::optional<struct v4l2_captureparm> parm_vbi_capture;
    std::optional<struct v4l2_outputparm> parm_vbi_output;
    std::optional<struct v4l2_captureparm> parm_sliced_vbi_capture;
    std::optional<struct v4l2_outputparm> parm_sliced_vbi_output;
    std::optional<struct v4l2_outputparm> parm_video_output_overlay;
    std::optional<struct v4l2_captureparm> parm_video_capture_mplane;
    std::optional<struct v4l2_outputparm> parm_video_output_mplane;
    std::optional<struct v4l2_captureparm> parm_sdr_capture;
    std::optional<struct v4l2_outputparm> parm_sdr_output;
    std::optional<struct v4l2_captureparm> parm_meta_capture;
    std::optional<struct v4l2_outputparm> parm_meta_output;

    /* VIDIOC_G_PRIORITY */
    std::optional<enum v4l2_priority> priority;

    /* VIDIOC_G_SELECTION */
    /* enum v4l2_buf_type counts 1..V4L2_BUF_TYPE_META_OUTPUT,
     * but in memory we store this as 0.. V4L2_BUF_TYPE_META_OUTPUT-1
     */
    V4lMcSelection selection[V4L2_BUF_TYPE_META_OUTPUT];

    /* VIDIOC_G_STD */
    std::optional<v4l2_std_id> std;

    /* VIDIOC_SUBDEV_G_STD */
    /* Note: This ioctl() does not take a pad index.*/
    std::optional<v4l2_std_id> subdev_std;

    /* Ignored: VIDIOC_G_TUNER */
    std::vector<struct v4l2_tuner> tuners;
    std::vector<__u32> tuner_freq;
  } maindev_;

  /* Controls are complex enough to have their own class */
  std::vector<std::unique_ptr<V4lMcControl>> controls_;

  /* media-ctl child nodes */
  std::vector<std::unique_ptr<V4lMcPad>> pads_;
  /* Only outgoing links */
  std::vector<std::unique_ptr<V4lMcLink>> links_;

  std::string devpath_;

 private:
  /* Private variables */

  /* Optional fd to V4L2 device described by this entity.
   * If this is set, setters will additionally call ioctl() on this fd.
   */
  std::optional<int> fd_;
};

#endif /* CAMERA_TOOLS_MCTK_ENTITY_H_ */
