idf_build_get_property(idf_target IDF_TARGET)
if("${idf_target}" STREQUAL "esp32p4")
list(APPEND extra_requires esp-hosted-tanmatsu)
endif()

idf_component_register(
	SRCS
		"main.c"
		"radio.c"
		"spectrum.c"
		"capture.c"
		"decode.c"
		"meshtastic.c"
		"aes_ctr_esp.c"
		"lorawan_netid.c"
		"profiles.c"
		"ui.c"
		"view_spectrum.c"
		"view_listen.c"
		"view_devices.c"
		"view_radio.c"
		"telemetry.c"
	PRIV_REQUIRES
		esp_lcd
		esp_timer
		fatfs
		nvs_flash
		bt
		badge-bsp
		tanmatsu-wifi
		tanmatsu-lora
		mbedtls
		${extra_requires}
	INCLUDE_DIRS
		"."
)
