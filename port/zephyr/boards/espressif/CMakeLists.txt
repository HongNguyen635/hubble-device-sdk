zephyr_include_directories(
	../
	../../../
)

if(CONFIG_SOC_ESP32C6)
    zephyr_library_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../esp-idf/hubblenetwork-sdk/boards/espressif/)
    zephyr_library_sources(radio.c)
else()
    message(FATAL_ERROR "SOC is not supported.")
endif()
