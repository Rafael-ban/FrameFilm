#ifndef __APP_PASS_H__
#define __APP_PASS_H__

#include "app_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pass 页面私有消息，避开 app_interface.h 的公共消息。 */
#define APP_UI_REQ_PASS_RELOAD  (0x30)
#define APP_UI_MSG_PASS_RELOAD  (0x31)

extern const app_entry_t g_app_pass_entry;

#ifdef __cplusplus
}
#endif

#endif /* __APP_PASS_H__ */
