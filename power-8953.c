/*
 * Copyright (c) 2016, 2018, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * *    * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_NIDEBUG 0

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define LOG_TAG "QTI PowerHAL"
#include <hardware/hardware.h>
#include <hardware/power.h>
#include <log/log.h>

#include "hint-data.h"
#include "metadata-defs.h"
#include "performance.h"
#include "power-common.h"
#include "utils.h"

#define MIN_VAL(X, Y) ((X > Y) ? (Y) : (X))

static int display_hint_sent;
static int video_encode_hint_sent;

static void process_video_encode_hint(void* metadata);

static bool is_target_SDM632() /* Returns value=632 if target is SDM632 else value 0 */
{
    int fd;
    bool is_target_SDM632 = false;
    char buf[10] = {0};
    fd = open("/sys/devices/soc0/soc_id", O_RDONLY);
    if (fd >= 0) {
        if (read(fd, buf, sizeof(buf) - 1) == -1) {
            ALOGW("Unable to read soc_id");
            is_target_SDM632 = false;
        } else {
            int soc_id = atoi(buf);
            if (soc_id == 349 || soc_id == 350) {
                is_target_SDM632 = true; /* Above SOCID for SDM632 */
            }
        }
    }
    close(fd);
    return is_target_SDM632;
}

int power_hint_override(power_hint_t hint, void* data) {
    switch (hint) {
        case POWER_HINT_VSYNC:
            break;
        case POWER_HINT_VIDEO_ENCODE: {
            process_video_encode_hint(data);
            return HINT_HANDLED;
        }
        default:
            break;
    }
    return HINT_NONE;
}

int set_interactive_override(int on) {
    char governor[80];
    static int set_i_count = 0;

    ALOGI("Got set_interactive hint");

    if (get_scaling_governor_check_cores(governor, sizeof(governor), CPU0) == -1) {
        if (get_scaling_governor_check_cores(governor, sizeof(governor), CPU1) == -1) {
            if (get_scaling_governor_check_cores(governor, sizeof(governor), CPU2) == -1) {
                if (get_scaling_governor_check_cores(governor, sizeof(governor), CPU3) == -1) {
                    ALOGE("Can't obtain scaling governor.");
                    return HINT_HANDLED;
                }
            }
        }
    }

    if (!on) {
        /* Display off. */
        if (is_interactive_governor(governor)) {
            /* timer rate - 40mS*/
            int resource_values[] = {
                    0x41424000,
                    0x28,
            };
            if (!display_hint_sent) {
                perform_hint_action(DISPLAY_STATE_HINT_ID, resource_values,
                                    sizeof(resource_values) / sizeof(resource_values[0]));
                display_hint_sent = 1;
            }
        } /* Perf time rate set for CORE0,CORE4 8952 target*/

    } else {
        /* Display on. */
        if (is_interactive_governor(governor)) {
            undo_hint_action(DISPLAY_STATE_HINT_ID);
            display_hint_sent = 0;
        }
    }

    set_i_count++;
    ALOGI("Got set_interactive hint on= %d, count= %d\n", on, set_i_count);

    return HINT_HANDLED;
}

/* Video Encode Hint */
static void process_video_encode_hint(void* metadata) {
    char governor[80] = {0};
    int resource_values[20] = {0};
    int num_resources = 0;
    struct video_encode_metadata_t video_encode_metadata;

    ALOGI("Got process_video_encode_hint");

    if (get_scaling_governor_check_cores(governor, sizeof(governor), CPU0) == -1) {
        if (get_scaling_governor_check_cores(governor, sizeof(governor), CPU1) == -1) {
            if (get_scaling_governor_check_cores(governor, sizeof(governor), CPU2) == -1) {
                if (get_scaling_governor_check_cores(governor, sizeof(governor), CPU3) == -1) {
                    ALOGE("Can't obtain scaling governor.");
                    // return HINT_HANDLED;
                }
            }
        }
    }

    /* Initialize encode metadata struct fields. */
    memset(&video_encode_metadata, 0, sizeof(struct video_encode_metadata_t));
    video_encode_metadata.state = -1;
    video_encode_metadata.hint_id = DEFAULT_VIDEO_ENCODE_HINT_ID;

    if (metadata) {
        if (parse_video_encode_metadata((char*)metadata, &video_encode_metadata) == -1) {
            ALOGE("Error occurred while parsing metadata.");
            return;
        }
    } else {
        return;
    }

    if (video_encode_metadata.state == 1) {
        if (is_schedutil_governor(governor)) {
            if (is_target_SDM632()) {
                /* sample_ms = 10mS
                 * SLB for Core0 = -6
                 * SLB for Core1 = -6
                 * SLB for Core2 = -6
                 * SLB for Core3 = -6
                 * hispeed load = 95
                 * hispeed freq = 1036 */
                int res[] = {
                        0x41820000, 0xa,        0x40c68100, 0xfffffffa, 0x40c68110,
                        0xfffffffa, 0x40c68120, 0xfffffffa, 0x40c68130, 0xfffffffa,
                        0x41440100, 0x5f,       0x4143c100, 0x40c,
                };
                memcpy(resource_values, res, MIN_VAL(sizeof(resource_values), sizeof(res)));
                num_resources = sizeof(res) / sizeof(res[0]);
                if (!video_encode_hint_sent) {
                    perform_hint_action(video_encode_metadata.hint_id, resource_values,
                                        num_resources);
                    video_encode_hint_sent = 1;
                }
            } else {
                /* sample_ms = 10mS */
                int res[] = {
                        0x41820000,
                        0xa,
                };
                memcpy(resource_values, res, MIN_VAL(sizeof(resource_values), sizeof(res)));
                num_resources = sizeof(res) / sizeof(res[0]);
                if (!video_encode_hint_sent) {
                    perform_hint_action(video_encode_metadata.hint_id, resource_values,
                                        num_resources);
                    video_encode_hint_sent = 1;
                }
            }
        } else if (is_interactive_governor(governor)) {
            /* Sched_load and migration_notification disable
             * timer rate - 40mS*/
            int res[] = {
                    0x41430000, 0x1, 0x41434000, 0x1, 0x41424000, 0x28,
            };
            memcpy(resource_values, res, MIN_VAL(sizeof(resource_values), sizeof(res)));
            num_resources = sizeof(res) / sizeof(res[0]);
            if (!video_encode_hint_sent) {
                perform_hint_action(video_encode_metadata.hint_id, resource_values, num_resources);
                video_encode_hint_sent = 1;
            }
        }
    } else if (video_encode_metadata.state == 0) {
        if (is_interactive_governor(governor) || is_schedutil_governor(governor)) {
            undo_hint_action(video_encode_metadata.hint_id);
            video_encode_hint_sent = 0;
        }
    }
    return;
}
