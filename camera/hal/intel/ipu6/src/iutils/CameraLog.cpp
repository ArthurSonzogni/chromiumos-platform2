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

#define LOG_TAG "CameraLog"

#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <base/logging.h>

#include "iutils/Utils.h"

#include "CameraLog.h"
#include "Trace.h"

namespace icamera {
int gLogLevel = 0;
char *gLogModules = nullptr;
int gPerfLevel = 0;
int gEnforceDvs = 0;
int gSlowlyRunRatio = 0;
bool gIsDumpMediaTopo = false;
bool gIsDumpMediaInfo = false;

static const char* cameraDebugLogToString(int level) {
    switch (level) {
        case CAMERA_DEBUG_LOG_LEVEL1:
            return "LV1";
        case CAMERA_DEBUG_LOG_LEVEL2:
            return "LV2";
        case CAMERA_DEBUG_LOG_REQ_STATE:
            return "REQ";
        case CAMERA_DEBUG_LOG_AIQ:
            return "AIQ";
        case CAMERA_DEBUG_LOG_XML:
            return "XML";
        case CAMERA_DEBUG_LOG_DBG:
            return "DBG";
        case CAMERA_DEBUG_LOG_INFO:
            return "INF";
        case CAMERA_DEBUG_LOG_ERR:
            return "ERR";
        case CAMERA_DEBUG_LOG_WARNING:
            return "WAR";
        case CAMERA_DEBUG_LOG_VERBOSE:
            return "VER";
        case CAMERA_DEBUG_LOG_VC_SYNC:
            return "VCSYNC";
        case CAMERA_DEBUG_LOG_GRAPH:
            return "GRAPH";
        default:
            return "UKN";
    }
}

__attribute__((__format__ (__printf__, 3, 0)))
static void printLog(const char *module, int level, const char *fmt, va_list ap)
{
    char prefix[64] = {};
    snprintf(prefix, sizeof(prefix), "[%s]: CamHAL_%s:", cameraDebugLogToString(level), module);

    char message[256] = {};
    vsnprintf(message, sizeof(message), fmt, ap);

    switch (level) {
        case CAMERA_DEBUG_LOG_ERR:
            LOG(ERROR) << prefix << message;
            break;
        case CAMERA_DEBUG_LOG_WARNING:
            LOG(WARNING) << prefix << message;
            break;
        default:
            LOG(INFO) << prefix << message;
            break;
    }
}

namespace Log {

void setDebugLevel(void)
{
    const char* PROP_CAMERA_HAL_DEBUG = "cameraDebug";
    const char* PROP_CAMERA_HAL_MODULES = "cameraModules";
    const char* PROP_CAMERA_HAL_PERF  = "cameraPerf";
    const char* PROP_CAMERA_HAL_DVS = "cameraDvs";
    const char* PROP_CAMERA_RUN_RATIO = "cameraRunRatio";

    // debug
    char *dbgLevel = getenv(PROP_CAMERA_HAL_DEBUG);
    if (dbgLevel) {
        gLogLevel = strtoul(dbgLevel, nullptr, 0);
        LOG1("Debug level is 0x%x", gLogLevel);

        // to enable both LOG1 and LOG2 traces
        if (gLogLevel & CAMERA_DEBUG_LOG_LEVEL2)
            gLogLevel |= CAMERA_DEBUG_LOG_LEVEL1;
    }

    char *slowlyRunRatio = getenv(PROP_CAMERA_RUN_RATIO);
    if (slowlyRunRatio) {
        gSlowlyRunRatio = strtoul(slowlyRunRatio, nullptr, 0);
        LOG1("Slow run ratio is 0x%x", gSlowlyRunRatio);
    }

    //modules
    gLogModules = getenv(PROP_CAMERA_HAL_MODULES);

    // performance
    char *perfLevel = getenv(PROP_CAMERA_HAL_PERF);
    if (perfLevel) {
        gPerfLevel = strtoul(perfLevel, nullptr, 0);
        LOGD("Performance level is 0x%x", gPerfLevel);

        // bitmask of tracing categories
        if (gPerfLevel & CAMERA_DEBUG_LOG_PERF_TRACES) {
            LOGD("Perf KPI start/end trace is not yet supported");
        }
        if (gPerfLevel & CAMERA_DEBUG_LOG_PERF_TRACES_BREAKDOWN) {
            LOGD("Perf KPI breakdown trace is not yet supported");
        }
        if (gPerfLevel & CAMERA_DEBUG_LOG_PERF_IOCTL_BREAKDOWN) {
            LOGD("Perf IOCTL breakdown trace is not yet supported");
        }
        if (gPerfLevel & CAMERA_DEBUG_LOG_PERF_MEMORY) {
            LOGD("Perf memory breakdown trace is not yet supported");
        }
        if (gPerfLevel & CAMERA_DEBUG_LOG_MEDIA_TOPO_LEVEL) {
            gIsDumpMediaTopo = true;
        }
        if (gPerfLevel & CAMERA_DEBUG_LOG_MEDIA_CONTROLLER_LEVEL) {
            gIsDumpMediaInfo = true;
        }
        ScopedAtrace::setTraceLevel(gPerfLevel);
    }

    // Enforce DVS for debugging
    char *dvs = getenv(PROP_CAMERA_HAL_DVS);
    if (dvs) {
        gEnforceDvs = strtoul(dvs, nullptr, 0);
        LOGD("EnforceDvs level is 0x%x", gEnforceDvs);
    }
}

bool isDebugLevelEnable(int level)
{
    return gLogLevel & level;
}

bool isModulePrintable(const char *module)
{
    if (gLogModules == nullptr) {
        return true;
    } else if (strstr(gLogModules, module) != nullptr) {
        return true;
    } else {
        return false;
    }
}

bool isDumpMediaTopo(void)
{
    return gIsDumpMediaTopo;
}

bool isDumpMediaInfo(void)
{
    return gIsDumpMediaInfo;
}

__attribute__((__format__ (__printf__, 4, 0)))
void print_log(bool enable, const char *module, const int level, const char *format, ...)
{
    if (!enable && (level != CAMERA_DEBUG_LOG_ERR))
        return;

    if (!isModulePrintable(module)) {
        return;
    }

    va_list arg;
    va_start(arg, format);

    printLog(module, level, format, arg);

    va_end(arg);
}

__attribute__((__format__ (__printf__, 1, 0)))
void ccaPrintError(const char *fmt, va_list ap)
{
    if (gLogLevel & CAMERA_DEBUG_LOG_AIQ) {
        printLog("CCA_DEBUG", CAMERA_DEBUG_LOG_ERR, fmt, ap);
    }
}

__attribute__((__format__ (__printf__, 1, 0)))
void ccaPrintInfo(const char *fmt, va_list ap)
{
    if (gLogLevel & CAMERA_DEBUG_LOG_AIQ) {
        printLog("CCA_DEBUG", CAMERA_DEBUG_LOG_INFO, fmt, ap);
    }
}

__attribute__((__format__ (__printf__, 1, 0)))
void ccaPrintDebug(const char *fmt, va_list ap)
{
    if (gLogLevel & CAMERA_DEBUG_LOG_AIQ) {
        printLog("CCA_DEBUG", CAMERA_DEBUG_LOG_DBG, fmt, ap);
    }
}

} // namespace Log

#ifdef HAVE_ANDROID_OS

void __camera_hal_log(bool condition, int prio, const char *tag,
                      const char *fmt, ...)
{
    if (condition) {
        va_list ap;
        va_start(ap, fmt);
        if (gLogLevel & CAMERA_DEBUG_LOG_PERSISTENT) {
            int errnoCopy;
            unsigned int maxTries = 20;
            do {
                errno = 0;
                __android_log_vprint(prio, tag, fmt, ap);
                errnoCopy = errno;
                if (errnoCopy == EAGAIN)
                    usleep(2000); /* sleep 2ms */
            } while(errnoCopy == EAGAIN && maxTries--);
        } else {
            __android_log_vprint(prio, tag, fmt, ap);
        }
    }
}

#endif //HAVE_ANDROID_OS
} //namespace icamera
