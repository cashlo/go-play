idf_component_register(
    SRCS "main.c"  # List all source files in the main directory
    INCLUDE_DIRS "."
    REQUIRES esp_wifi nvs_flash driver esp_adc spiffs app_update
)