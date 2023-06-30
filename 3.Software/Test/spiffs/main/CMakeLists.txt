idf_component_register(SRCS "spiffs_example_main.c"
                    INCLUDE_DIRS "."
                    )

spiffs_create_partition_image(storage ../audio FLASH_IN_PROJECT)