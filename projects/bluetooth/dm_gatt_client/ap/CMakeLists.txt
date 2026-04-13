set(incs .)
set(srcs ap_main.c)



if (CONFIG_BT)
	list(APPEND incs
		app_bluetooth/storage
		app_bluetooth/gatt
		app_bluetooth
	)

	list(APPEND srcs
		app_bluetooth/storage/bluetooth_storage.c
		app_bluetooth/gatt/dm_gap_utils.c
		app_bluetooth/gatt/dm_gatt.c
		app_bluetooth/gatt/dm_gattc.c
		app_bluetooth/gatt/dm_gatts.c
		app_bluetooth/gatt/dm_gatt_cli.c
		app_bluetooth/gatt/dm_gatt_connection.c
	
		)
endif()


set(priv_req  media_service multimedia bk_network_provisioning lwip_intf_v2_1)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES "${priv_req}")
