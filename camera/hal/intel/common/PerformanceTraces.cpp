/*
 * Copyright (C) 2012, 2014-2017 The Android Open Source Project
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

#define LOG_TAG "PerformanceTraces"

#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sstream>
#include "PerformanceTraces.h"

NAMESPACE_DECLARATION {
namespace PerformanceTraces {

int HalAtrace::mTraceLevel = 0;

HalAtrace::HalAtrace(const char* func, const char* tag, const char* note, int value)
{
    std::ostringstream stringStream;
    if (value < 0 || note == nullptr) {
        stringStream << "< " << func << "," << tag << ">";
        ia_trace_begin(mTraceLevel, stringStream.str().c_str());
    } else {
        stringStream << "< " << func << "," << tag << ">:" << note << "(" << value << ")";
        ia_trace_begin(mTraceLevel, stringStream.str().c_str());
    }
}

HalAtrace::~HalAtrace()
{
    ia_trace_end(mTraceLevel);
}

void HalAtrace::reset(void)
{
    if (LogHelper::isPerfDumpTypeEnable(CAMERA_DEBUG_LOG_ATRACE_LEVEL))
      mTraceLevel = 1;
    else
      mTraceLevel = 0;
}

/**
 * \class PerformanceTimer
 *
 * Private class for managing R&D traces used for performance
 * analysis and testing.
 *
 * This code should be disabled in product builds.
 */
class PerformanceTimer {

public:
    nsecs_t mStartAt;
    nsecs_t mLastRead;
    bool mFilled;            //!< timestamp has been taken
    bool mRequested;         //!< trace is requested/enabled

    PerformanceTimer(void) :
        mStartAt(0),
        mLastRead(0),
        mFilled(false),
        mRequested(false) {
    }

    bool isRunning(void) { return mFilled && mRequested; }

    bool isRequested(void) { return mRequested; }

    int64_t timeUs(void) {
        uint64_t now = systemTime();
        mLastRead = now;
        return (now - mStartAt) / 1000;
    }

    int64_t lastTimeUs(void) {
        uint64_t now = systemTime();
        return (now - mLastRead) / 1000;
    }

   /**
     * Enforce a standard format on timestamp traces parsed
     * by offline PnP tools.
     *
     * \see system/core/include/android/log.h
     */
    void formattedTrace(const char* p, const char *f) {
        LOGD("%s:%s, Time: %" PRId64 " us, Diff: %" PRId64 " us",
             p, f, timeUs(), mFilled ? lastTimeUs() : -1);
    }

    void start(void) {
        mStartAt = mLastRead = systemTime();
        mFilled = true;
    }

    void stop(void) { mFilled = false; }

};

// To allow disabling all tracing infrastructure for non-R&D builds,
// wrap everything in LIBCAMERA_RD_FEATURES (see Android.mk).
// -----------------------------------------------------------------

#ifdef LIBCAMERA_RD_FEATURES

static PerformanceTimer gLaunch2Preview;
static PerformanceTimer gLaunch2FocusLock;
static PerformanceTimer gShot2Shot;
static PerformanceTimer gShutterLag;
static PerformanceTimer gSwitchCameras;
static PerformanceTimer gAAAProfiler;
static PerformanceTimer gPnPBreakdown;
static PerformanceTimer gIOBreakdown;

static bool gSwitchCamerasCalled = false;
static bool gSwitchCamerasOriginalVideoMode = false;
static bool gSwitchCamerasVideoMode = false;
static int gSwitchCamerasOriginalCameraId = 0;

const int MEM_DATA_LEN = 192;
const char FLUSH_CTRL[2] = {0x0A, 0x0};
const char DBG_CTRL[3] = {0x34,0x0A,0x0};
const char* MEM_DBG ="/data/dbgopt";
const char* MEM_PIPE = "/sys/kernel/debug/tracing/trace_pipe";
const char* MEM_PIPE_FLUSH = "/sys/kernel/debug/tracing/trace";

bool IOBreakdown::mMemInfoEnabled = false;
int IOBreakdown::mPipeFD = -1;
int IOBreakdown::mDbgFD = -1;
int IOBreakdown::mPipeflushFD = -1;
std::mutex IOBreakdown::mMemMutex;

/**
 * Reset the flags that enable the different performance traces
 * This is needed during HAL open so that we can turn off the performance
 * traces from the system property
 */
void reset(void)
{
    gSwitchCamerasCalled = false;
    gSwitchCamerasVideoMode = false;
    gLaunch2Preview.mRequested = false;
    gShot2Shot.mRequested = false;
    gAAAProfiler.mRequested = false;
    gShutterLag.mRequested = false;
    gSwitchCameras.mRequested = false;
    gLaunch2FocusLock.mRequested = false;

    // bitmask of tracing categories
    if (LogHelper::isPerfDumpTypeEnable(CAMERA_DEBUG_LOG_PERF_TRACES)) {
        PerformanceTraces::Launch2Preview::enable(true);
        PerformanceTraces::Launch2FocusLock::enable(true);
        PerformanceTraces::Shot2Shot::enable(true);
        PerformanceTraces::ShutterLag::enable(true);
        PerformanceTraces::SwitchCameras::enable(true);
    }
    if (LogHelper::isPerfDumpTypeEnable(CAMERA_DEBUG_LOG_PERF_TRACES_BREAKDOWN)) {
        PerformanceTraces::PnPBreakdown::enable(true);
    }

    if (LogHelper::isPerfDumpTypeEnable(CAMERA_DEBUG_LOG_PERF_IOCTL_BREAKDOWN)) {
        PerformanceTraces::IOBreakdown::enableBD(true);
    }

    if (LogHelper::isPerfDumpTypeEnable(CAMERA_DEBUG_LOG_PERF_MEMORY)) {
        PerformanceTraces::IOBreakdown::enableMemInfo(true);
    }
}
/**
 * Controls trace state
 */
void Launch2Preview::enable(bool set)
{
    gLaunch2Preview.mRequested = set;
}

/**
 * Starts the launch2preview trace.
 */
void Launch2Preview::start(void)
{
    if (gLaunch2Preview.isRequested()) {
        if (gPnPBreakdown.isRunning())
            PnPBreakdown::step("Launch2Preview::start");
        gLaunch2Preview.start();
    }
}

/**
 * Stops the launch2preview trace and prints out results.
 */
void Launch2Preview::stop(int mFrameNum)
{
    if (gLaunch2Preview.isRunning()) {
        if (gPnPBreakdown.isRunning())
            PnPBreakdown::step("Launch2Preview::stop");
        if (mFrameNum == 1) {
            LOGD("LAUNCH time to the 1st preview frame show:\t%" PRId64 " ms\n",
                 gLaunch2Preview.timeUs() / 1000);
        } else {
            LOGD("LAUNCH: skip %d frame, time to the 1st preview frame show:\t%" PRId64 " ms\n",
                 (mFrameNum - 1), gLaunch2Preview.timeUs() / 1000);
        }

        gLaunch2Preview.stop();
    }
}

/**
 * Controls trace state
 */
void Launch2FocusLock::enable(bool set)
{
    gLaunch2FocusLock.mRequested = set;
}

/**
 * Starts the launch2FocusLock trace.
 */
void Launch2FocusLock::start(void)
{
    if (gLaunch2FocusLock.isRequested()) {
        gLaunch2FocusLock.start();
        gLaunch2FocusLock.formattedTrace("Launch2FocusLock", __FUNCTION__);
    }
}

/**
 * Stops the launch2FocusLock trace and prints out results.
 */
void Launch2FocusLock::stop(void)
{
    if (gLaunch2FocusLock.isRunning()) {
        if (gPnPBreakdown.isRunning())
            PnPBreakdown::step("Launch2FocusLock::stop");
        LOGD("LAUNCH time calculated from create instance to lock the focus frame:\t%" PRId64 " ms\n",
             gLaunch2FocusLock.timeUs() / 1000);
        gLaunch2FocusLock.stop();
    }
}

/**
 * Controls trace state
 */
void ShutterLag::enable(bool set)
{
    gShutterLag.mRequested = set;
}

/**
 * Starts the ShutterLag trace.
 */
void ShutterLag::takePictureCalled(void)
{
    if (gShutterLag.isRequested())
        gShutterLag.start();
}

/**
 * Prints ShutterLag trace results.
 */
void ShutterLag::snapshotTaken(struct timeval *ts)
{
    if (gShutterLag.isRunning()) {
        LOGD("ShutterLag from takePicture() to shot taken:\t%lldms\n",
             (((nsecs_t(ts->tv_sec)*1000000LL
             +  nsecs_t(ts->tv_usec))
             - gShutterLag.mStartAt/1000)/1000));
    }
}

/**
 * Controls trace state
 */
void Shot2Shot::enable(bool set)
{
    gShot2Shot.mRequested = set;
}

/**
 * Starts shot2shot trace
 */
void Shot2Shot::start(void)
{
    if (gShot2Shot.isRequested()) {
        gShot2Shot.start();
        if (gPnPBreakdown.isRunning())
            PnPBreakdown::step("Shot2Shot::start");
    }
}

/**
 * Marks that take picture call has been issued.
 *
 * This is needed to reliably detect start and end of shot2shot
 * sequences.
 */
void Shot2Shot::takePictureCalled(void)
{
    if (gShot2Shot.isRunning() == true)
        stop();

    start();
}

void Shot2Shot::stop(void)
{
    if (gShot2Shot.isRunning()) {
        if (gPnPBreakdown.isRunning())
            PnPBreakdown::step("Shot2Shot::stop");
            LOGD("shot2shot latency: %" PRId64 " us.", gShot2Shot.timeUs());
        gShot2Shot.stop();
    }
}

/**
 * Controls trace state
 */

void AAAProfiler::enable(bool set)
{
    gAAAProfiler.mRequested = set;
}

/**
 * Starts the AAAprofiler trace.
 */
void AAAProfiler::start(void)
{
    if (gAAAProfiler.isRequested()) {
        gAAAProfiler.start();
        gAAAProfiler.formattedTrace("gAAAProfiler", __FUNCTION__);
    }
}

/**
 * Stops the AAAprofiler trace and prints out results.
 */
void AAAProfiler::stop(void)
{
    if (gAAAProfiler.isRunning()) {
        LOGD("3A profiling time::\t%" PRId64 "ms\n",
             gAAAProfiler.timeUs() / 1000);
        gAAAProfiler.stop();
    }
}

/**
 * Controls trace state
 */
void SwitchCameras::enable(bool set)
{
    gSwitchCameras.mRequested = set;
}

/**
 * Starts the SwitchCameras trace.
 */
void SwitchCameras::start(int cameraid)
{
    if (gSwitchCameras.isRequested()) {
        if (gPnPBreakdown.isRunning())
            PnPBreakdown::step("Switch::start");
        gSwitchCamerasCalled = false;
        gSwitchCamerasOriginalVideoMode = false;
        gSwitchCamerasVideoMode = false;
        gSwitchCamerasOriginalCameraId = cameraid;
        gSwitchCameras.start();
    }
}

/**
 * Get the original mode
 */
void SwitchCameras::getOriginalMode(bool videomode)
{
    if (gSwitchCameras.isRequested())
        gSwitchCamerasOriginalVideoMode = videomode;
}

/**
 * This function will be called at the time of start preview.
 */
void SwitchCameras::called(bool videomode)
{
    if (gSwitchCameras.isRequested()) {
        gSwitchCamerasCalled = true;
        gSwitchCamerasVideoMode = videomode;
    }
}

/**
 * Stops the SwitchCameras trace and prints out results.
 */
void SwitchCameras::stop(void)
{
    if (gSwitchCameras.isRunning() && gSwitchCamerasCalled == true) {
        if (gPnPBreakdown.isRunning())
            PnPBreakdown::step("Switch::stop");
        if (gSwitchCamerasOriginalVideoMode == gSwitchCamerasVideoMode) {
            LOGD("Using %s mode, Switch from %s camera to %s camera, SWITCH time::\t%" PRId64 "ms\n",
                    (gSwitchCamerasVideoMode ? "video" : "camera"),
                    ((gSwitchCamerasOriginalCameraId == 0) ? "back" : "front"),
                    ((gSwitchCamerasOriginalCameraId == 1) ? "back" : "front"),
                    gSwitchCameras.timeUs() / 1000);
        } else {
            LOGD("Using %s camera, Switch from %s mode to %s mode, SWITCH time::\t%" PRId64 "ms\n",
                    ((gSwitchCamerasOriginalCameraId == 0) ? "back" : "front"),
                    (gSwitchCamerasOriginalVideoMode ? "video" : "camera"),
                    (gSwitchCamerasVideoMode ? "video" : "camera"),
                    gSwitchCameras.timeUs() / 1000);
        }
        gSwitchCamerasCalled = false;
        gSwitchCameras.stop();
    }
}

/**
 * Enable more detailed breakdown analysis that shows  how long
 * intermediate steps consumed
 */
void PnPBreakdown::enable(bool set)
{
    gPnPBreakdown.mRequested = set;
}

/**
 * Start the log breakdown performance tracer.
 */
void PnPBreakdown::start(void)
{
    if (gPnPBreakdown.isRequested()) {
        gPnPBreakdown.start();
        gPnPBreakdown.formattedTrace("PnPBreakdown", __FUNCTION__);
    }
}

/**
 * Mark an intermediate step in breakdown tracer.
 *
 * @arg func, the function name which called it.
 * @arg not, a string printed with the breakdown trace
 * @arg mFrameNum, the num of the frame got from ISP.
 */
void PnPBreakdown::step(const char *func, const char* note, const int mFrameNum)
{
    if (gPnPBreakdown.isRunning()) {
        if (!note)
            note = "";
        if (mFrameNum < 0)
            LOGD("PnPBreakdown-step %s:%s, Time: %" PRId64 " us, Diff: %" PRId64 " us",
                 func, note, gPnPBreakdown.timeUs(), gPnPBreakdown.lastTimeUs());
        else
            LOGD("PnPBreakdown-step %s:%s[%d], Time: %" PRId64 " us, Diff: %" PRId64 " us",
                 func, note, mFrameNum, gPnPBreakdown.timeUs(), gPnPBreakdown.lastTimeUs());
   }
}

/**
 * Stop the performance tracer.
 */
void PnPBreakdown::stop(void)
{
    if (gPnPBreakdown.isRunning()) {
        gPnPBreakdown.formattedTrace("PnPBreakdown", __FUNCTION__);
        gPnPBreakdown.stop();
    }
}

/**
 * To indicate the performance and memory for every IOCTL call.
 *
 * @arg func, the function name which called it.
 * @arg note, a string printed with IOCTL information.
 */
IOBreakdown::IOBreakdown(const char *func, const char *note):
 mFuncName(func)
,mNote(note)
{
    if (gIOBreakdown.isRunning()) {
        gIOBreakdown.timeUs();
        gIOBreakdown.lastTimeUs();
    }
}

IOBreakdown::~IOBreakdown()
{
    char memData[MEM_DATA_LEN]={0};
    if (gIOBreakdown.isRunning()) {
        if (!mNote)
            mNote = "";
        if (mMemInfoEnabled) {
            mMemMutex.lock();

            if (mDbgFD < 0) {
                LOGE("dgbopt isn't opened.");
            } else {
                unsigned int sizeToWrite = 3;
                unsigned int size = ::write(mDbgFD, DBG_CTRL, sizeToWrite);
                if (size != sizeToWrite)
                    LOGW("DbgFD write was not complete.");
                if (mPipeFD < 0) {
                    LOGE("trace_pipe isn't opened.");
                } else {
                    int n;
                    do {
                        n = ::read(mPipeFD, memData, MEM_DATA_LEN - 1);
                    }while (n<=0);
                    LOGD("memory <%s,%d>:%s", mNote, n, memData);
                }
            }
            mMemMutex.unlock();
        }

        LOGD("IOBreakdown-step %s:%s, Time: %" PRId64 " us, Diff: %" PRId64 " us",
                 mFuncName, mNote, gIOBreakdown.timeUs(), gIOBreakdown.lastTimeUs());
    }
}

/**
 * Enable more detailed breakdown analysis that shows  how long
 * intermediate steps consumed
 */
void IOBreakdown::enableBD(bool set)
{
    gIOBreakdown.mRequested = set;
}

/**
 * Enable more detailed memory analysis that shows how much memory
 * intermediate steps consumed
 */
void IOBreakdown::enableMemInfo(bool set)
{
    mMemInfoEnabled = set;
}

/**
 * Start the log breakdown performance tracer.
 */
void IOBreakdown::start(void)
{
    struct stat st;

    if (gIOBreakdown.isRequested()) {
        gIOBreakdown.start();
        gIOBreakdown.formattedTrace("IOBreakdown", __FUNCTION__);
    }

    if (mMemInfoEnabled) {
        mMemMutex.lock();
        if (stat (MEM_DBG, &st) == -1) {
            LOGE("Error stat MEM_DBG: %s", strerror(errno));
            return ;
        }

        if (stat (MEM_PIPE, &st) == -1) {
            LOGE("Error stat MEM_PIPE: %s", strerror(errno));
            return ;
        }

        if (stat (MEM_PIPE_FLUSH, &st) == -1) {
            LOGE("Error stat MEM_PIPE_FLUSH: %s", strerror(errno));
            return ;
        }

        if ((mDbgFD = ::open(MEM_DBG, O_WRONLY))<0) {
            LOGE("Fail to open dbgopt:%s", strerror(errno));
        } else if ((mPipeFD = ::open(MEM_PIPE, O_RDONLY))<0) {
            LOGE("Fail to open trace_pipe:%s", strerror(errno));
        } else if ((mPipeflushFD = ::open(MEM_PIPE_FLUSH, O_WRONLY))<0) {
            LOGE("Fail to open trace_pipe_flush:%s", strerror(errno));
        }

        if (mPipeflushFD >= 0) {
            unsigned int sizeToWrite = 2;
            unsigned int size = ::write(mPipeflushFD, FLUSH_CTRL, sizeToWrite);
            if (size != sizeToWrite)
                LOGW("Pipe flush was not complete.");
            if (::close(mPipeflushFD) < 0)
                LOGE("Close trace_pipe_flush error!");

            mPipeflushFD = -1;
        }
        mMemMutex.unlock();
    }
}

/**
 * Stop the performance tracer.
 */
void IOBreakdown::stop(void)
{
    if (gIOBreakdown.isRunning()) {
        gIOBreakdown.formattedTrace("IOBreakdown", __FUNCTION__);
        gIOBreakdown.stop();
    }

    mMemMutex.lock();
    if(mPipeFD >= 0)
        if (::close(mPipeFD) < 0)
            LOGE("Close trace_pipe error!");

    if(mDbgFD >= 0)
        if (::close(mDbgFD) < 0)
            LOGE("Close dbgopt error!");

    mPipeFD = -1;
    mDbgFD = -1;
    mMemInfoEnabled = false;
    mMemMutex.unlock();
}

#else // LIBCAMERA_RD_FEATURES
void reset(void) {}

#endif // LIBCAMERA_RD_FEATURES

} // namespace PerformanceTraces
} NAMESPACE_DECLARATION_END
