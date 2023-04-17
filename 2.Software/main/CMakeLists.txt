set(requires
    esp-sr
    hardware_driver
    player
    esp_http_client
    tinyusb
    nvs_flash
    esp_event
    esp_netif
    protocol_examples_common
    )

idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES ${requires})


