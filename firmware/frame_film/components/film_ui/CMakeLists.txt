set(srcs "src/ui_core.c"
         "src/ui_display.c"
         )

set(public_include "inc")

set(requires lvgl film_hal film_sys esp_timer)

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include}
                       REQUIRES ${requires}
                      )
