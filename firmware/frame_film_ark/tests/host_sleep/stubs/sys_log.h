#ifndef HOST_SYS_LOG_H
#define HOST_SYS_LOG_H
#include <assert.h>
#define sys_logi(...) ((void)0)
#define sys_loge(...) ((void)0)
#define sys_logw(...) ((void)0)
#define SYS_ERROR_CHECK(error) assert(!(error))
#endif
