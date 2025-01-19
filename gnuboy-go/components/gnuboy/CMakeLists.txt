idf_component_register(
    SRCS
    "cpu.c"
    "debug.c"
    "emu.c"
    "events.c"
    "exports.c"
    "fastmem.c"
    "hw.c"
    "inflate.c"
    "keytable.c"
    "lcd.c"
    "lcdc.c"
    "loader.c"
    "main.c"
    "mem.c"
    "newsound.c"
    "palette.c"
    "path.c"
    "rccmds.c"
    "rcfile.c"
    "rckeys.c"
    "rcvars.c"
    "refresh.c"
    "rtc.c"
    "save.c"
    "serial.c"
    "sound.c"
    "split.c"
    INCLUDE_DIRS "."
    REQUIRES spi_flash esp_partition esp_event driver fatfs
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    GNUBOY_NO_MINIZIP
    GNUBOY_NO_SCREENSHOT
    IS_LITTLE_ENDIAN
)

