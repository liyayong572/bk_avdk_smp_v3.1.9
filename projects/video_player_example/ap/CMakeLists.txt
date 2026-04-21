set(incs . sport_dv/include video_player_cli/src)
set(srcs ap_main.c
        sport_dv/src/sport_dv_app.c
        sport_dv/src/sport_dv_common.c
        sport_dv/src/sport_dv_display.c
        sport_dv/src/sport_dv_keys.c
        sport_dv/src/sport_dv_preview.c
        sport_dv/src/sport_dv_video_rec.c
        sport_dv/src/blend_assets/bk_img.c
        sport_dv/src/blend_assets/blend_dsc.c
        video_player_cli/src/audio_recorder_device.c
        )

# LVGL widgets demo
if (CONFIG_LV_USE_DEMO_WIDGETS)
    list(APPEND srcs
        lv_demo_widgets.c
        assets/img_clothes.c
        assets/img_demo_widgets_avatar.c
        assets/img_lvgl_logo.c
    )
endif()	
	
set(GLOBAL_FUNCTION_SYMBOLS "")

set(priv_req  media_service multimedia bk_camera bk_cli bk_display bk_voice_service bk_video_recorder key)

# bk_video_player is built as an independent component. Add it explicitly so the example can
# compile even if other dependency chains change.
if (CONFIG_AVI_PLAYER)
    list(APPEND priv_req bk_video_player)
endif()

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")

foreach(symbol ${GLOBAL_FUNCTION_SYMBOLS})
	target_link_libraries(${COMPONENT_LIB} INTERFACE "-u ${symbol}")
endforeach()

# Copy .it.csv file to build directory
file(COPY ${CMAKE_CURRENT_LIST_DIR}/../.it.csv DESTINATION ${CMAKE_BINARY_DIR}/)
