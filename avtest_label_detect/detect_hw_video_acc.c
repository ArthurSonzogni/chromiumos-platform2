// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// hw_video_acc_* detectors
// For each detector, check both V4L2 and VAAPI capabilities.
#include <linux/videodev2.h>
#ifdef HAS_VAAPI
#include <va/va.h>
#endif

#include "label_detect.h"

static const char* kVideoDevicePattern = "/dev/video*";
static const char* kJpegDevicePattern = "/dev/jpeg*";
static const char* kDRMDevicePattern = "/dev/dri/renderD*";

/* Helper function for detect_video_acc_h264.
 * A V4L2 device supports H.264 decoding, if it's
 * a mem-to-mem V4L2 device, i.e. it provides V4L2_CAP_VIDEO_CAPTURE_*,
 * V4L2_CAP_VIDEO_OUTPUT_* and V4L2_CAP_STREAMING capabilities and it supports
 * V4L2_PIX_FMT_H264 as it's input, i.e. for its
 * V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE queue.
*/
static bool is_v4l2_dec_h264_device(int fd) {
  return is_hw_video_acc_device(fd) &&
    (is_v4l2_support_format(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
         V4L2_PIX_FMT_H264) ||
     is_v4l2_support_format(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
         V4L2_PIX_FMT_H264_SLICE));
}

/* Helper function for detect_video_acc_h264.
 * Determine given |fd| is a VAAPI device supports H.264 decoding, i.e.
 * it supports one of H.264 profile, has decoding entry point, and output
 * YUV420 formats.
 */
static bool is_vaapi_dec_h264_device(int fd) {
#ifdef HAS_VAAPI
  VAProfile va_profiles[] = {
    VAProfileH264Baseline,
    VAProfileH264Main,
    VAProfileH264High,
    VAProfileH264ConstrainedBaseline,
    VAProfileNone
  };
  if (is_vaapi_support_formats(
        fd, va_profiles, VAEntrypointVLD, VA_RT_FORMAT_YUV420))
    return true;
#endif
  return false;
}

/* Determines "hw_video_acc_h264" label. That is, either the VAAPI device
 * supports one of H.264 profile, has decoding entry point, and output
 * YUV420 formats. Or there is a /dev/video* device supporting H.264
 * decoding.
 */
bool detect_video_acc_h264(void) {
  return (is_any_device(kDRMDevicePattern, is_vaapi_dec_h264_device) ||
          is_any_device(kVideoDevicePattern, is_v4l2_dec_h264_device));
}

/* Helper function for detect_video_acc_vp8.
 * A V4L2 device supports VP8 decoding, if it's a mem-to-mem V4L2 device,
 * i.e. it provides V4L2_CAP_VIDEO_CAPTURE_*, V4L2_CAP_VIDEO_OUTPUT_* and
 * V4L2_CAP_STREAMING capabilities and it supports V4L2_PIX_FMT_VP8 as it's
 * input, i.e. for its V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE queue.
*/
static bool is_v4l2_dec_vp8_device(int fd) {
  return is_hw_video_acc_device(fd) &&
    (is_v4l2_support_format(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
         V4L2_PIX_FMT_VP8) ||
     is_v4l2_support_format(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
         V4L2_PIX_FMT_VP8_FRAME));
}

/* Helper function for detect_video_acc_vp8.
 * Dtermine given |fd| is a VAAPI device supports VP8 decoding, i.e. it
 * supports VP8 profile, has decoding entry point, and output YUV420
 * formats.
 */
static bool is_vaapi_dec_vp8_device(int fd) {
#ifdef HAS_VAAPI
#if VA_CHECK_VERSION(0, 35, 0)
  VAProfile va_profiles[] = {
    VAProfileVP8Version0_3,
    VAProfileNone
  };
  if (is_vaapi_support_formats(fd, va_profiles, VAEntrypointVLD,
        VA_RT_FORMAT_YUV420))
    return true;
#endif
#endif
  return false;
}

/* Determines "hw_video_acc_vp8" label. That is, either the VAAPI device
 * supports VP8 profile, has decoding entry point, and output YUV420
 * formats. Or there is a /dev/video* device supporting VP8 decoding.
 */
bool detect_video_acc_vp8(void) {
  if (is_any_device(kDRMDevicePattern, is_vaapi_dec_vp8_device))
    return true;

  if (is_any_device(kVideoDevicePattern, is_v4l2_dec_vp8_device))
    return true;
  return false;
}

/* Helper function for detect_video_acc_vp9.
 * Determine given |fd| is a VAAPI device supports VP9 decoding, i.e. it
 * supports VP9 profile, has decoding entry point, and output YUV420
 * formats.
 */
static bool is_vaapi_dec_vp9_device(int fd) {
#ifdef HAS_VAAPI
#if VA_CHECK_VERSION(0, 37, 1)
  VAProfile va_profiles[] = {
    VAProfileVP9Profile0,
    VAProfileNone
  };
  if (is_vaapi_support_formats(fd, va_profiles, VAEntrypointVLD,
        VA_RT_FORMAT_YUV420))
    return true;
#endif
#endif
  return false;
}

/* Helper function for detect_video_acc_vp9.
 * A V4L2 device supports VP9 decoding, if it's a mem-to-mem V4L2 device,
 * i.e. it provides V4L2_CAP_VIDEO_CAPTURE_*, V4L2_CAP_VIDEO_OUTPUT_* and
 * V4L2_CAP_STREAMING capabilities and it supports V4L2_PIX_FMT_VP9 as it's
 * input, i.e. for its V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE queue.
*/
static bool is_v4l2_dec_vp9_device(int fd) {
  return is_hw_video_acc_device(fd) &&
    (is_v4l2_support_format(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
         V4L2_PIX_FMT_VP9) ||
     is_v4l2_support_format(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
         V4L2_PIX_FMT_VP9_FRAME));
}

/* Determines "hw_video_acc_vp9" label. That is, either the VAAPI device
 * supports VP9 profile, has decoding entry point, and output YUV420
 * formats. Or there is a /dev/video* device supporting VP9 decoding.
 */
bool detect_video_acc_vp9(void) {
  if (is_any_device(kDRMDevicePattern, is_vaapi_dec_vp9_device))
    return true;

  if (is_any_device(kVideoDevicePattern, is_v4l2_dec_vp9_device))
    return true;
  return false;
}

/* Helper function for detect_video_acc_enc_h264.
 * A V4L2 device supports H.264 encoding, if it's a mem-to-mem V4L2 device,
 * i.e. it provides V4L2_CAP_VIDEO_CAPTURE_*, V4L2_CAP_VIDEO_OUTPUT_* and
 * V4L2_CAP_STREAMING capabilities and it supports V4L2_PIX_FMT_H264 as it's
 * output, i.e. for its V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE queue.
*/
static bool is_v4l2_enc_h264_device(int fd) {
  return is_hw_video_acc_device(fd) &&
    is_v4l2_support_format(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
        V4L2_PIX_FMT_H264);
}

/* Helper function for detect_video_acc_enc_h264.
 * Determine given |fd| is a VAAPI device supports H.264 encoding, i.e. it
 * support one of H.264 profile, has encoding entry point, and input YUV420
 * formats.
 */
static bool is_vaapi_enc_h264_device(int fd) {
#ifdef HAS_VAAPI
  VAProfile va_profiles[] = {
    VAProfileH264Baseline,
    VAProfileH264Main,
    VAProfileH264High,
    VAProfileH264ConstrainedBaseline,
    VAProfileNone
  };
  if (is_vaapi_support_formats(fd, va_profiles, VAEntrypointEncSlice,
        VA_RT_FORMAT_YUV420))
    return true;
#endif
  return false;
}

/* Determines "hw_video_acc_enc_h264" label. That is, either the VAAPI
 * device supports one of H.264 profile, has encoding entry point, and
 * input YUV420 formats. Or there is a /dev/video* device supporting H.264
 * encoding.
 */
bool detect_video_acc_enc_h264(void) {
  if (is_any_device(kDRMDevicePattern, is_vaapi_enc_h264_device))
    return true;

  if (is_any_device(kVideoDevicePattern, is_v4l2_enc_h264_device))
    return true;
  return false;
}

/* Helper function for detect_video_acc_enc_vp8.
 * A V4L2 device supports VP8 encoding, if it's a mem-to-mem V4L2 device,
 * i.e. it provides V4L2_CAP_VIDEO_CAPTURE_*, V4L2_CAP_VIDEO_OUTPUT_* and
 * V4L2_CAP_STREAMING capabilities and it supports V4L2_PIX_FMT_VP8 as it's
 * output, i.e. for its V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE queue.
*/
static bool is_v4l2_enc_vp8_device(int fd) {
  return is_hw_video_acc_device(fd) &&
    is_v4l2_support_format(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
        V4L2_PIX_FMT_VP8);
}

/* Helper function for detect_video_acc_enc_vp8.
 * Determine given |fd| is a VAAPI device supports VP8 encoding, i.e. it
 * supports one of VP8 profile, has encoding entry point, and input YUV420
 * formats.
 */
static bool is_vaapi_enc_vp8_device(int fd) {
#ifdef HAS_VAAPI
#if VA_CHECK_VERSION(0, 35, 0)
  VAProfile va_profiles[] = {
    VAProfileVP8Version0_3,
    VAProfileNone
  };
  if (is_vaapi_support_formats(fd, va_profiles, VAEntrypointEncSlice,
        VA_RT_FORMAT_YUV420))
    return true;
#endif
#endif
  return false;
}

/* Determines "hw_video_acc_enc_vp8" label. That is, either the VAAPI device
 * supports one of VP8 profile, has encoding entry point, and input YUV420
 * formats. Or there is a /dev/video* device supporting VP8 encoding.
 */
bool detect_video_acc_enc_vp8(void) {
  if (is_any_device(kDRMDevicePattern, is_vaapi_enc_vp8_device))
    return true;

  if (is_any_device(kVideoDevicePattern, is_v4l2_enc_vp8_device))
    return true;
  return false;
}

/* Helper function for detect_jpeg_acc_dec.
 * Determine given |fd| is a VAAPI device supports JPEG decoding, i.e. it
 * supports JPEG profile, has decoding entry point, and output YUV420
 * formats.
 */
static bool is_vaapi_dec_jpeg_device(int fd) {
#ifdef HAS_VAAPI
  VAProfile va_profiles[] = {
    VAProfileJPEGBaseline,
    VAProfileNone
  };
  if (is_vaapi_support_formats(fd, va_profiles, VAEntrypointVLD,
        VA_RT_FORMAT_YUV420))
    return true;
#endif
  return false;
}

/* Helper function for detect_jpeg_acc_dec.
 * A V4L2 device supports JPEG decoding, if it's a mem-to-mem V4L2 device,
 * i.e. it provides V4L2_CAP_VIDEO_CAPTURE_*, V4L2_CAP_VIDEO_OUTPUT_* and
 * V4L2_CAP_STREAMING capabilities and it supports V4L2_PIX_FMT_JPEG as it's
 * input, i.e. for its V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE queue.
*/
static bool is_v4l2_dec_jpeg_device(int fd) {
  return is_hw_jpeg_acc_device(fd) &&
    is_v4l2_support_format(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
         V4L2_PIX_FMT_JPEG);
}

/* Determines "hw_jpeg_acc_dec" label. That is, either the VAAPI device
 * supports jpeg profile, has decoding entry point, and output YUV420
 * formats. Or there is a /dev/jpeg* device supporting JPEG decoding.
 */
bool detect_jpeg_acc_dec(void) {
  if (is_any_device(kDRMDevicePattern, is_vaapi_dec_jpeg_device))
    return true;

  if (is_any_device(kJpegDevicePattern, is_v4l2_dec_jpeg_device))
    return true;
  return false;
}
