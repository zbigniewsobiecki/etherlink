idf_component_register(
    SRCS "src/etherlink_ble.c"
    INCLUDE_DIRS "include"
    REQUIRES etherlink bt
    PRIV_REQUIRES nvs_flash
)
