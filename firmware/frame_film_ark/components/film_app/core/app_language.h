#ifndef __APP_LANGUAGE_H__
#define __APP_LANGUAGE_H__

#include "app_boot_cfg.h"

/* UI text only. User-authored profile fields and filenames never pass here. */
static inline const char *app_text(const char *zh, const char *en)
{
    return app_language_get() == APP_LANGUAGE_EN ? en : zh;
}

#endif
