idf_component_register(
    SRCS "odroid_system.c" "odroid_settings.c" "odroid_sdcard.c" "odroid_input.c" "odroid_audio.c" "odroid_display.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_adc esp_event fatfs nvs_flash app_update spiffs
)
