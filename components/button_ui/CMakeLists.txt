idf_component_register(
    SRCS "button_ui.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/include"
    REQUIRES esp_driver_gpio nvs_flash freertos log esp_netif
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os)
