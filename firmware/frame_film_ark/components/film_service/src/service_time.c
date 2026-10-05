/*********************************************************************
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (c) 2026 kiritro
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 *
 * FileName : /film_service/src/service_time.c
 * Author: Kiritro  Version: v0.1  Date: 2026/9/20
 * Description: 时间服务：系统时间 + 时区（BLE 0x4D 时间同步 / 上电应用已存时区）
 * ChangeLog: Change Notes
 *
 *********************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "sys_log.h"
#include "service_param.h"
#include "service_time.h"

/*********************************************************************
 * MACROS
 */
#define TIME_TAG                "service_time"

/* 时间戳合理范围：2020-01-01 ~ 2100-01-01。挡掉 0 / 明显乱值 ——
   连接端取的浏览器时间一般没问题，但被改成荒唐值后 RTC 定时唤醒会跟着荒唐。 */
#define TIME_EPOCH_MIN          (1577836800LL)
#define TIME_EPOCH_MAX          (4102444800LL)

/* 时区上限 ±14h（现实中最东 +14、最西 -12） */
#define TIME_TZ_LIMIT           (14 * 60)

#define TIME_TZ_BUF_LEN         (24)

/*********************************************************************
 * LOCAL VARIABLES
 */


/*********************************************************************
 * LOCAL FUNCTIONS
 */

/**
 * @brief 把"距 UTC 分钟数"落成 POSIX TZ 串交给 libc
 *
 * POSIX 的 TZ 串里符号是**反的**：`UTC-8` 表示本地 = UTC+8。所以东八区（+480 分钟）
 * 要写成 `UTC-8`；整小时以外的（印度 +330）写成 `UTC-5:30`。
 *
 * 必须走 setenv + tzset：`localtime_r()` 认的是 libc 的 TZ 环境变量，
 * 不认我们自己存的 tz_min 字段 —— 漏了这步，时钟页永远按 UTC 显示。
 */
static void time_apply_tz(int16_t tz_min)
{
    char tz[TIME_TZ_BUF_LEN];
    int hour = tz_min / 60;             /* 东为正，C 的整除向零取整 */
    int min = tz_min % 60;

    if(min < 0)
    {
        min = -min;                     /* -5:30 这种，分钟取绝对值 */
    }

    snprintf(tz, sizeof(tz), "UTC%+d:%02d", -hour, min);
    setenv("TZ", tz, 1);
    tzset();

    sys_logi(TIME_TAG, "timezone applied: %s (tz_min=%d)", tz, (int)tz_min);
}

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

void service_time_init(void)
{
    time_apply_tz(g_service_param.sys.tz_min);
}

int service_time_sync(int64_t epoch_s, int16_t tz_min)
{
    struct timeval tv;
    struct tm tmv;
    time_t t;

    if(epoch_s < TIME_EPOCH_MIN || epoch_s > TIME_EPOCH_MAX)
    {
        sys_logw(TIME_TAG, "time sync rejected: epoch %lld out of range", (long long)epoch_s);
        return -1;
    }
    if(tz_min < -TIME_TZ_LIMIT || tz_min > TIME_TZ_LIMIT)
    {
        sys_logw(TIME_TAG, "time sync rejected: tz_min %d out of range", (int)tz_min);
        return -1;
    }

    /* 时区变了才落盘：NVS 擦写寿命有限，而正常连接端每次发的都是同一个值 */
    if(g_service_param.sys.tz_min != tz_min)
    {
        g_service_param.sys.tz_min = tz_min;
        service_param_save();
    }
    time_apply_tz(tz_min);

    tv.tv_sec = (time_t)epoch_s;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);

    /* 打一行人类可读的，方便对着串口/时钟页核 */
    t = (time_t)epoch_s;
    if(localtime_r(&t, &tmv) != NULL)
    {
        sys_logi(TIME_TAG, "time synced: %04d-%02d-%02d %02d:%02d:%02d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    }
    return 0;
}
