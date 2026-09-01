/*
 * Minimal Bluetooth LE SMP DFU sample for nRF54LM20A and nRF54LM20B.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/app_version.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(dfu_smp_ble, LOG_LEVEL_INF);

static struct k_work advertise_work;

static const struct bt_data advertising_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, SMP_BT_SVC_UUID_VAL),
};

static const struct bt_data scan_response_data[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void advertise(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, advertising_data,
			      ARRAY_SIZE(advertising_data), scan_response_data,
			      ARRAY_SIZE(scan_response_data));
	if (err != 0) {
		LOG_ERR("Cannot start advertising (%d)", err);
		return;
	}

	LOG_INF("Advertising as \"%s\"", CONFIG_BT_DEVICE_NAME);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);

	if (err != 0U) {
		LOG_ERR("Connection failed: 0x%02x %s", err,
			bt_hci_err_to_str(err));
		k_work_submit(&advertise_work);
		return;
	}

	LOG_INF("SMP client connected");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	LOG_INF("Disconnected: 0x%02x %s", reason,
		bt_hci_err_to_str(reason));
}

static void recycled(void)
{
	k_work_submit(&advertise_work);
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
};

int main(void)
{
	int err;

	k_work_init(&advertise_work, advertise);

	err = bt_enable(NULL);
	if (err != 0) {
		LOG_ERR("Bluetooth initialization failed (%d)", err);
		return err;
	}

	LOG_INF("Firmware version %s", APP_VERSION_STRING);
	k_work_submit(&advertise_work);

	return 0;
}
