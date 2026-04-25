idf_component_register(SRCS "main.c"
                            "ble_midi.c"
                            "config_store.c"
                            "wifi_sb1.c"
                            "http_sb1.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES nvs_flash bt esp_driver_gpio esp_driver_uart esp_wifi esp_netif esp_event esp_http_server json)
