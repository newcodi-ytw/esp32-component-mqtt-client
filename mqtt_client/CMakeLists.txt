set(srcs    "src/mqtt_task.c")

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS inc
                       REQUIRES esp_http_client
                       REQUIRES esp_ringbuf
                       REQUIRES mqtt)

