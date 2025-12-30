idf_component_register(
    SRCS "src/etherlink_uart.c"
    INCLUDE_DIRS "include"
    REQUIRES etherlink driver esp_driver_uart
)
