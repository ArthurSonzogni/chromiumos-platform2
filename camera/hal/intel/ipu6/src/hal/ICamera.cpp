/*
 * Copyright (C) 2015-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "ICamera"

#include "iutils/CameraDump.h"
#include "iutils/CameraLog.h"

#include "ICamera.h"
#include "CameraHal.h"
#include "PlatformData.h"

/**
 * This is the wrapper to the CameraHal Class to provide the HAL interface
 * Main job of this file
 * 1. Check the argument from user
 * 2. Transfer HAL API to CameraHal class
 * 3. Implement the HAL static function: get_number_of_cameras and get_camera_info
 */
namespace icamera {

static CameraHal * gCameraHal = nullptr;

#define CheckCameraId(camera_id, err_code) \
    do { \
        int max_cam = PlatformData::numberOfCameras(); \
        if (((camera_id) < 0) || (camera_id) >= max_cam) { \
            LOGE("camera index(%d) is invaild., max_cam:%d", camera_id, max_cam); \
            return err_code; \
        } \
    } while (0)

/**
 * Return the numbers of camera
 * This should be called before any other calls
 *
 * \return > 0  return camera numbers
 * \return == 0 failed to get camera numbers
 **/
int get_number_of_cameras()
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(1);

    return PlatformData::numberOfCameras();
}

/**
 * Get capability related camera info.
 * Should be called after get_number_of_cameras
 *
 * \return error code
 */
int get_camera_info(int camera_id, camera_info_t& info)
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(1);
    CheckCameraId(camera_id, BAD_VALUE);

    int ret = PlatformData::getCameraInfo(camera_id, info);

    return ret;
}

/**
 * Initialize camera hal
 *
 * \return error code
 **/
int camera_hal_init()
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(1);

    if (gCameraHal) {
        LOGW("camera hal is initialized multiple times.");
        return 0;
    }
    gCameraHal = new CameraHal;

    CheckError(!gCameraHal, INVALID_OPERATION, "camera hal is NULL.");
    return gCameraHal->init();
}

/**
 * De-initialize camera hal
 *
 * \return error code
 **/
int camera_hal_deinit()
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(1);

    CheckError(!gCameraHal, INVALID_OPERATION, "camera hal is NULL.");
    int ret = gCameraHal->deinit();

    delete gCameraHal;
    gCameraHal = nullptr;

    return ret;
}

/**
 * Register callback function
 **/
void camera_callback_register(int camera_id, const camera_callback_ops_t* callback)
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(1);

    CheckError(!gCameraHal, VOID_VALUE, "camera hal is NULL.");
    gCameraHal->deviceCallbackRegister(camera_id, callback);
}

/**
 * Open one camera device
 *
 * \param camera_id camera index
 *
 * \return error code
 **/
int camera_device_open(int camera_id)
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(1);

    CheckError(!gCameraHal, INVALID_OPERATION, "camera hal is NULL.");
    CheckCameraId(camera_id, BAD_VALUE);

    return gCameraHal->deviceOpen(camera_id);
}

/**
 * Close camera device
 *
 * \param camera_id The ID that opened before
 **/
void camera_device_close(int camera_id)
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(1);

    CheckError(!gCameraHal, VOID_VALUE, "camera hal is NULL.");
    CheckCameraId(camera_id,);

    gCameraHal->deviceClose(camera_id);
}

/**
 * Configure the sensor input of the device
 *
 * \param camera_id The camera ID that was opened
 * \param input_config  sensor input configuration
 *
 * \return 0 succeed <0 error
 **/
int camera_device_config_sensor_input(int camera_id, const stream_t *input_config)
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(1);

    CheckError(!gCameraHal, INVALID_OPERATION, "camera hal is NULL.");
    CheckError(!input_config, BAD_VALUE, "camera input_config is NULL.");
    CheckCameraId(camera_id, BAD_VALUE);

    return gCameraHal->deviceConfigInput(camera_id, input_config);
}

/**
 * Add stream to device
 *
 * \param camera_id The camera ID that was opened
 * \param stream_id
 * \param stream_conf stream configuration
 *
 * \return 0 succeed <0 error
 **/
int camera_device_config_streams(int camera_id, stream_config_t *stream_list)
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(1);

    CheckError(!gCameraHal, INVALID_OPERATION, "camera hal is NULL.");
    CheckError(!stream_list, BAD_VALUE, "camera stream is NULL.");
    CheckCameraId(camera_id, BAD_VALUE);

    if (stream_list->operation_mode == CAMERA_STREAM_CONFIGURATION_MODE_STILL_CAPTURE) {
        for (int i = 0; i < stream_list->num_streams; i++) {
            stream_list->streams[i].usage = CAMERA_STREAM_STILL_CAPTURE;
        }
    }

    return gCameraHal->deviceConfigStreams(camera_id, stream_list);
}

/**
 * Start device
 *
 * Start all streams in device.
 *
 * \param camera_id The Caemra ID that opened before
 *
 * \return error code
 **/
int camera_device_start(int camera_id)
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(1);
    CheckError(!gCameraHal, INVALID_OPERATION ,"camera hal is NULL.");
    CheckCameraId(camera_id, BAD_VALUE);

    return gCameraHal->deviceStart(camera_id);
}

/**
 * Stop device
 *
 * Stop all streams in device.
 *
 * \param camera_id The Caemra ID that opened before
 *
 * \return error code
 **/
int camera_device_stop(int camera_id)
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(1);
    CheckError(!gCameraHal, INVALID_OPERATION, "camera hal is NULL.");
    CheckCameraId(camera_id, BAD_VALUE);

    return gCameraHal->deviceStop(camera_id);
}

/**
 * Allocate memory for mmap & dma export io-mode
 *
 * \param camera_id The camera ID that opened before
 * \param camera_buff stream buff
 *
 * \return error code
 **/
int camera_device_allocate_memory(int camera_id, camera_buffer_t *buffer)
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(2);
    CheckError(!gCameraHal, INVALID_OPERATION, "camera hal is NULL.");
    CheckCameraId(camera_id, BAD_VALUE);
    CheckError(!buffer, BAD_VALUE, "buffer is NULL.");
    CheckError(buffer->s.memType != V4L2_MEMORY_MMAP, BAD_VALUE, "memory type %d is not supported.", buffer->s.memType);

    return gCameraHal->deviceAllocateMemory(camera_id, buffer);
}

/**
 * Queue a buffer to a stream (deprecated)
 *
 * \param camera_id The camera ID that opened before
 * \param stream_id the stream ID that add to device before
 * \param camera_buff stream buff
 *
 * \return error code
 **/
int camera_stream_qbuf(int camera_id, int stream_id, camera_buffer_t *buffer,
                       int num_buffers, const Parameters* settings)
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(2);
    CheckError(!gCameraHal, INVALID_OPERATION, "camera hal is NULL.");
    CheckCameraId(camera_id, BAD_VALUE);

    LOGW("camera_stream_qbuf(cam_id, stream_id, *buffer, num_buffers, *settings) is deprecated and will be removed soon.");
    LOGW("Please start to use camera_stream_qbuf(cam_id, **buffer, num_buffers, *settings)");

    return gCameraHal->streamQbuf(camera_id, &buffer, num_buffers, settings);
}

/**
 * Queue a buffer(or more buffers) to a stream
 *
 * \param camera_id The camera ID that opened before
 * \param buffer The array of pointers to the camera_buffer_t
 * \param num_buffers The number of buffers in the array
 *
 * \return error code
 **/
int camera_stream_qbuf(int camera_id, camera_buffer_t **buffer,
                       int num_buffers, const Parameters* settings)
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(2);
    CheckError(!gCameraHal, INVALID_OPERATION, "camera hal is NULL.");
    CheckCameraId(camera_id, BAD_VALUE);

    return gCameraHal->streamQbuf(camera_id, buffer, num_buffers, settings);
}

/**
 * Dequeue a buffer from a stream
 *
 * \param camera_id The camera ID that opened before
 * \param stream_id the stream ID that add to device before
 * \param camera_buff stream buff
 *
 * \return error code
 **/
int camera_stream_dqbuf(int camera_id, int stream_id, camera_buffer_t **buffer,
                        Parameters* settings)
{
    PERF_CAMERA_ATRACE();
    HAL_TRACE_CALL(2);
    CheckError(!gCameraHal, INVALID_OPERATION, "camera hal is NULL.");
    CheckCameraId(camera_id, BAD_VALUE);
    CheckError(!buffer, BAD_VALUE, "camera stream buffer is null.");

    return gCameraHal->streamDqbuf(camera_id, stream_id, buffer, settings);
}

int camera_set_parameters(int camera_id, const Parameters& param)
{
    HAL_TRACE_CALL(2);
    CheckCameraId(camera_id, BAD_VALUE);
    CheckError(!gCameraHal, INVALID_OPERATION, "camera device is not open before setting parameters.");

    return gCameraHal->setParameters(camera_id, param);
}

int camera_get_parameters(int camera_id, Parameters& param, long sequence)
{
    HAL_TRACE_CALL(2);
    CheckCameraId(camera_id, BAD_VALUE);
    CheckError(!gCameraHal, INVALID_OPERATION, "camera device is not open before getting parameters.");

    return gCameraHal->getParameters(camera_id, param, sequence);
}

int get_frame_size(int camera_id, int format, int width, int height, int field, int *bpp)
{
    CheckError(width <= 0, BAD_VALUE, "width <=0");
    CheckError(height <= 0, BAD_VALUE, "height <=0");
    CheckError(field < 0, BAD_VALUE, "field <0");

    int frameSize = 0;
    bool isOFSCompression = PlatformData::getOFSCompression(camera_id);

   *bpp = CameraUtils::getBpp(format);
    if(isOFSCompression) {
        frameSize = CameraUtils::getFrameSize(format, width, height, false, true, true);
    } else {
        frameSize = CameraUtils::getFrameSize(format, width, height);
    }
    LOG2("@%s: output compression frame: %d, frame size from HAL:%d\n",
               __func__, isOFSCompression, frameSize);

    return frameSize;
}

} // namespace icamera
