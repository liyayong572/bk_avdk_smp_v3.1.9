set(incs .)
set(srcs ap_main.c)



if (CONFIG_BLE)
	list(APPEND incs
		gatt_ble
		gatt_ble/fa00
		gatt_ble/f618
	)

	list(APPEND srcs


		gatt_ble/gatts.c
		gatt_ble/fa00/fa00_server.c
		gatt_ble/f618/f618_server.c

		)
endif()


set(priv_req  media_service multimedia bk_network_provisioning lwip_intf_v2_1 bt)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")
