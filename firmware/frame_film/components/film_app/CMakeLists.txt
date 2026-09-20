set(srcs "src/app_init.c"
         "src/app_manager.c"
         "src/app_render.c"
         "src/app_image.c"
         "src/app_template.c"
         "src/app_clock.c"
         "src/app_animation.c"
         "src/app_boot.c"
         "src/app_sleep.c"
         "src/app_menu.c"
         "src/app_settings.c"
         "src/app_shell.c"
         "src/font_clock_hero.c"
         )

set(public_include "inc")

set(requires film_service film_hal film_sys film_ui)

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${public_include}
                       REQUIRES ${requires}
                      )
