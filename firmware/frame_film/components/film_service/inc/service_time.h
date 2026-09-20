#ifndef __SERVICE_TIME_H__
#define __SERVICE_TIME_H__

/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>

/*********************************************************************
 * CPPMIX
 */
#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/**
 * @brief 初始化时间服务：把已保存的时区应用到 libc
 *
 * 必须在 service_param_init() **之后**调用（要读 g_service_param.tz_min）。
 *
 * 为什么需要这一步：`localtime_r()` 认的是 libc 的 TZ 环境变量，不认我们自己存的
 * 字段 —— 不做这一步，重启后时钟页会退回 UTC。
 */
extern void service_time_init(void);

/**
 * @brief 同步时间 + 时区（连接端下发，BLE 0x4D）
 *
 * 时间本身**不落盘**：deep sleep 期间 RTC 继续走、系统时间跨休眠保持，只有完全断电
 * 才丢（丢了本来也只能靠重新校时，写 NVS 反而白耗擦写）。时区**要落盘**，否则重启即失效。
 *
 * @param epoch_s Unix 秒（UTC）
 * @param tz_min  距 UTC 的分钟数，东为正（东八区 = +480）
 * @return 0 已应用；-1 取值不合理（时间戳越界 / 时区超出 ±14h），已忽略
 */
extern int service_time_sync(int64_t epoch_s, int16_t tz_min);

/**
 * @brief 取当前生效的时区（分钟，东为正）
 */
extern int16_t service_time_get_tz(void);

#ifdef __cplusplus
}
#endif

#endif /* __SERVICE_TIME_H__ */
