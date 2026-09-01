/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "display.h"
#include "zephyr/sys/atomic_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(display, CONFIG_DOOR_LOCK_APP_LOG_LEVEL);

static atomic_t nfc_mode = ATOMIC_INIT(false);

#define MIPI_DBI_NODE DT_NODELABEL(mipi_dbi)

#if DT_NODE_HAS_PROP(MIPI_DBI_NODE, dc_gpios)
static const struct gpio_dt_spec dc_gpio = GPIO_DT_SPEC_GET(MIPI_DBI_NODE, dc_gpios);
#endif

#define SPI_DEV_NODE DT_PHANDLE(MIPI_DBI_NODE, spi_dev)
static const struct device *spi_dev = DEVICE_DT_GET(SPI_DEV_NODE);

#if DT_NODE_HAS_PROP(SPI_DEV_NODE, cs_gpios)
static const struct gpio_dt_spec cs_gpio = GPIO_DT_SPEC_GET_BY_IDX(SPI_DEV_NODE, cs_gpios, 2);
#endif

struct k_event display_event;

K_THREAD_STACK_DEFINE(display_thread_stack, CONFIG_DISPLAY_THREAD_STACK_SIZE);

struct k_thread display_thread;

static dist_data_t dist_data;
static atomic_t disambiguation_is_front = ATOMIC_INIT(false);
static atomic_t disambiguation_visible = ATOMIC_INIT(false);
static atomic_t lock_shown_open = ATOMIC_INIT(false);

static void display_post_lock_event(bool isOpen)
{
	k_event_post(&display_event, BIT(isOpen ? DISPLAY_UNLOCK_ACTION : DISPLAY_LOCK_ACTION));
}

static bool distance_available;

static void display_update_disambiguation_side(display_ctx_t *ctx);
static void display_apply_clear_disambiguation_side(display_ctx_t *ctx);
static void display_realign_upper_labels(display_ctx_t *ctx);
static void display_realign_bottom_labels(display_ctx_t *ctx);

static void update_orientation(const struct device *dev, enum display_orientation orientation)
{
	lv_display_t *lv_disp = lv_display_get_default();
	if (lv_disp == NULL) {
		return;
	}

	display_set_orientation(dev, orientation);

	lv_display_rotation_t rotation;
	switch (orientation) {
	case DISPLAY_ORIENTATION_ROTATED_90:
		rotation = LV_DISPLAY_ROTATION_90;
		break;
	case DISPLAY_ORIENTATION_ROTATED_180:
		rotation = LV_DISPLAY_ROTATION_180;
		break;
	case DISPLAY_ORIENTATION_ROTATED_270:
		rotation = LV_DISPLAY_ROTATION_270;
		break;
	default:
		rotation = LV_DISPLAY_ROTATION_0;
		break;
	}

	lv_display_set_rotation(lv_disp, rotation);
	lv_obj_invalidate(lv_screen_active());
}

static void set_animation_x(void *object, int32_t x)
{
	lv_obj_set_x(static_cast<lv_obj_t *>(object), x);
}

int display_init(display_ctx_t *ctx)
{
	LOG_INF("Display init");

	if (!device_is_ready(spi_dev)) {
		LOG_ERR("SPI device not ready");
		return -ENODEV;
	}
	LOG_INF("SPI device ready: %s", spi_dev->name);

#if DT_NODE_HAS_PROP(MIPI_DBI_NODE, dc_gpios)
	if (!gpio_is_ready_dt(&dc_gpio)) {
		LOG_ERR("D/C GPIO not ready (port: %s, pin: %d)", dc_gpio.port->name, dc_gpio.pin);
		return -ENODEV;
	}
	LOG_INF("D/C GPIO ready: %s pin %d", dc_gpio.port->name, dc_gpio.pin);
#else
	LOG_WRN("D/C GPIO not defined in devicetree");
#endif

#if DT_NODE_HAS_PROP(SPI_DEV_NODE, cs_gpios)
	if (!gpio_is_ready_dt(&cs_gpio)) {
		LOG_ERR("CS GPIO not ready (port: %s, pin: %d)", cs_gpio.port->name, cs_gpio.pin);
		return -ENODEV;
	}
	LOG_INF("CS GPIO ready: %s pin %d", cs_gpio.port->name, cs_gpio.pin);
#else
	LOG_WRN("CS GPIO not defined for display");
#endif

	ctx->dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(ctx->dev)) {
		LOG_ERR("Display device not ready");
		return -ENODEV;
	}
	LOG_INF("Display device ready: %s", ctx->dev->name);

	update_orientation(ctx->dev, DISPLAY_ORIENTATION_NORMAL);

	static lv_style_t large_text_style;
	lv_style_init(&large_text_style);
	lv_style_set_text_font(&large_text_style, &lv_font_montserrat_28);
	lv_style_set_text_align(&large_text_style, LV_TEXT_ALIGN_CENTER);

	static lv_style_t small_text_style;
	lv_style_init(&small_text_style);
	lv_style_set_text_font(&small_text_style, &lv_font_montserrat_14);
	lv_style_set_text_align(&small_text_style, LV_TEXT_ALIGN_CENTER);

	LV_IMAGE_DECLARE(nordic_logo);
	ctx->nordic_logo = lv_image_create(lv_screen_active());
	lv_image_set_src(ctx->nordic_logo, &nordic_logo);
	lv_obj_align(ctx->nordic_logo, LV_ALIGN_TOP_MID, 0, 10);
	lv_obj_move_foreground(ctx->nordic_logo);

	ctx->status_label = lv_label_create(lv_screen_active());
	if (ctx->status_label == NULL) {
		LOG_ERR("Failed to create status label");
		return -ENOMEM;
	}
	lv_label_set_recolor(ctx->status_label, true);
	lv_label_set_text(ctx->status_label, "");
	lv_obj_add_style(ctx->status_label, &large_text_style, 0);
	lv_obj_set_style_text_align(ctx->status_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_pad_all(ctx->status_label, 0, 0);
	lv_obj_set_width(ctx->status_label, lv_pct(100));
	lv_obj_align_to(ctx->status_label, ctx->nordic_logo, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

#ifdef CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_FRONT_BACK_DETECTION
	LV_IMAGE_DECLARE(user_not_detected);
	ctx->disambiguation_icon = lv_image_create(lv_screen_active());
	if (ctx->disambiguation_icon == NULL) {
		LOG_ERR("Failed to create disambiguation icon");
		return -ENOMEM;
	}
	lv_image_set_src(ctx->disambiguation_icon, &user_not_detected);
	lv_image_set_scale(ctx->disambiguation_icon, 96);
	lv_obj_set_size(ctx->disambiguation_icon, 45, 45);
	lv_obj_set_style_pad_all(ctx->disambiguation_icon, 0, 0);
	lv_obj_align_to(ctx->disambiguation_icon, ctx->status_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
	lv_obj_add_flag(ctx->disambiguation_icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(ctx->disambiguation_icon);
#endif

	ctx->howto_close_label = lv_label_create(lv_screen_active());
	if (ctx->howto_close_label == NULL) {
		LOG_ERR("Failed to create howto_close label");
		return -ENOMEM;
	}
	lv_label_set_text(ctx->howto_close_label, "");
	lv_label_set_recolor(ctx->howto_close_label, true);
	lv_obj_add_style(ctx->howto_close_label, &small_text_style, 0);
	lv_obj_set_style_text_color(ctx->howto_close_label, lv_color_hex(0x000000), 0);
	lv_obj_set_style_pad_all(ctx->howto_close_label, 0, 0);
	lv_obj_align(ctx->howto_close_label, LV_ALIGN_BOTTOM_MID, 0, -20);
	lv_obj_move_foreground(ctx->howto_close_label);

	ctx->op_mode_label = lv_label_create(lv_screen_active());
	if (ctx->op_mode_label == NULL) {
		LOG_ERR("Failed to create op_mode label");
		return -ENOMEM;
	}
	lv_label_set_text(ctx->op_mode_label, "");
	lv_label_set_recolor(ctx->op_mode_label, true);
	lv_obj_add_style(ctx->op_mode_label, &large_text_style, 0);
	lv_obj_set_style_text_color(ctx->op_mode_label, lv_color_hex(0x000000), 0);
	lv_obj_set_style_pad_all(ctx->op_mode_label, 0, 0);
	lv_obj_align_to(ctx->op_mode_label, ctx->howto_close_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
	lv_obj_move_foreground(ctx->op_mode_label);

	ctx->op_mode_switch_label = lv_label_create(lv_screen_active());
	if (ctx->op_mode_switch_label == NULL) {
		LOG_ERR("Failed to create op_mode switch label");
		return -ENOMEM;
	}
	lv_label_set_text(ctx->op_mode_switch_label, "");
	lv_label_set_recolor(ctx->op_mode_switch_label, true);
	lv_obj_add_style(ctx->op_mode_switch_label, &small_text_style, 0);
	lv_obj_set_style_text_color(ctx->op_mode_switch_label, lv_color_hex(0x000000), 0);
	lv_obj_set_style_pad_all(ctx->op_mode_switch_label, 0, 0);
	lv_obj_align(ctx->op_mode_switch_label, LV_ALIGN_BOTTOM_MID, 0, -2);
	lv_obj_move_foreground(ctx->op_mode_switch_label);

	ctx->dist_label = lv_label_create(lv_screen_active());
	if (ctx->dist_label == NULL) {
		LOG_ERR("Failed to create distance label");
		return -ENOMEM;
	}
	lv_obj_add_style(ctx->dist_label, &large_text_style, 0);
	lv_obj_set_style_text_align(ctx->dist_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_pad_all(ctx->dist_label, 0, 0);
	lv_obj_align(ctx->dist_label, LV_ALIGN_BOTTOM_MID, 0, -52);
	(void)snprintf(ctx->count_str, sizeof(ctx->count_str), "Current distance:\nN/A");
	lv_label_set_text(ctx->dist_label, ctx->count_str);

	lv_task_handler();

	lv_label_set_text(ctx->status_label, "#ff0000 CLOSED" LV_SYMBOL_CLOSE "#");
	display_realign_bottom_labels(ctx);
	lv_task_handler();

	int ret = display_blanking_off(ctx->dev);
	if (ret != 0) {
		LOG_ERR("display_blanking_off failed: %d", ret);
	}

	struct display_capabilities caps;
	display_get_capabilities(ctx->dev, &caps);
	LOG_INF("Display: %ux%u, pixel_format=0x%x, supported_formats=0x%x", caps.x_resolution, caps.y_resolution,
		(uint32_t)caps.current_pixel_format, caps.supported_pixel_formats);

	LOG_INF("Display init complete");
	return 0;
}

static void animate_status_label(display_ctx_t *ctx)
{
	static lv_anim_t animation;
	lv_anim_init(&animation);
	lv_anim_set_var(&animation, ctx->status_label);
	lv_anim_set_values(&animation, 2 * lv_obj_get_x(ctx->status_label), 0);
	lv_anim_set_time(&animation, 500);
	lv_anim_set_exec_cb(&animation, set_animation_x);
	lv_anim_set_path_cb(&animation, lv_anim_path_bounce);
	lv_anim_start(&animation);
}

void open_animation(display_ctx_t *ctx)
{
	lv_label_set_text(ctx->status_label, "#00ff00 OPEN" LV_SYMBOL_OK "#");
	animate_status_label(ctx);
	display_realign_upper_labels(ctx);
}

void close_animation(display_ctx_t *ctx)
{
	animate_status_label(ctx);
	lv_label_set_text(ctx->status_label, "#ff0000 CLOSED" LV_SYMBOL_CLOSE "#");
	display_realign_upper_labels(ctx);
}

void howto_close_animation(display_ctx_t *ctx, bool show)
{
	if (show) {
		lv_label_set_text(ctx->howto_close_label, "Press BTN2 to close");
		lv_obj_set_style_text_color(ctx->howto_close_label, lv_color_hex(0x000000), 0);
	} else {
		lv_label_set_text(ctx->howto_close_label, "");
	}

	display_realign_upper_labels(ctx);
	lv_obj_move_foreground(ctx->howto_close_label);
}

void op_mode_animation(display_ctx_t *ctx, bool isNfc)
{
	if (isNfc) {
		lv_label_set_text(ctx->op_mode_label, "#0000ff NFC mode"
						      "#");
	} else {
		lv_label_set_text(ctx->op_mode_label, "#0000ff BLE/UWB mode"
						      "#");
	}

	lv_obj_set_style_text_color(ctx->op_mode_label, lv_color_hex(0x000000), 0);
	lv_label_set_text(ctx->op_mode_switch_label, "Press BTN3 to switch the mode");
	display_realign_upper_labels(ctx);
	lv_obj_move_foreground(ctx->op_mode_label);
	lv_obj_move_foreground(ctx->op_mode_switch_label);
	display_update_disambiguation_side(ctx);
}

void disconnected_animation(display_ctx_t *ctx)
{
	display_apply_clear_disambiguation_side(ctx);
	(void)snprintf(ctx->count_str, sizeof(ctx->count_str), "Current distance:\n%s", LV_SYMBOL_WARNING);
	lv_label_set_text(ctx->dist_label, ctx->count_str);
}

static void display_realign_upper_labels(display_ctx_t *ctx)
{
	constexpr int32_t kSmallGap = 4;
	constexpr int32_t kModeGap = 8;

	lv_obj_t *anchor;
#ifdef CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_FRONT_BACK_DETECTION
	if (!atomic_get(&nfc_mode) && atomic_get(&disambiguation_visible)) {
		lv_obj_align_to(ctx->disambiguation_icon, ctx->status_label, LV_ALIGN_OUT_BOTTOM_MID, 0, kSmallGap);
		anchor = ctx->disambiguation_icon;
	} else {
		anchor = ctx->status_label;
	}
#else
	anchor = ctx->status_label;
#endif

	lv_obj_align_to(ctx->op_mode_label, anchor, LV_ALIGN_OUT_BOTTOM_MID, 0, kModeGap);

	display_realign_bottom_labels(ctx);
}

static void display_realign_bottom_labels(display_ctx_t *ctx)
{
	constexpr int32_t kBottomMargin = 2;
	constexpr int32_t kBtnGap = 4;
	constexpr int32_t kDistGap = 8;

	lv_obj_align(ctx->op_mode_switch_label, LV_ALIGN_BOTTOM_MID, 0, -kBottomMargin);

	lv_obj_align_to(ctx->howto_close_label, ctx->op_mode_switch_label, LV_ALIGN_OUT_TOP_MID, 0, -kBtnGap);

	const char *howto_text = lv_label_get_text(ctx->howto_close_label);
	if (howto_text != nullptr && howto_text[0] != '\0') {
		lv_obj_align_to(ctx->dist_label, ctx->howto_close_label, LV_ALIGN_OUT_TOP_MID, 0, -kDistGap);
	} else {
		lv_obj_align_to(ctx->dist_label, ctx->op_mode_switch_label, LV_ALIGN_OUT_TOP_MID, 0, -kDistGap);
	}

	lv_obj_move_foreground(ctx->howto_close_label);
	lv_obj_move_foreground(ctx->op_mode_switch_label);
}

static void display_update_disambiguation_side(display_ctx_t *ctx)
{
#ifdef CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_FRONT_BACK_DETECTION
	LV_IMAGE_DECLARE(user_detected);
	LV_IMAGE_DECLARE(user_not_detected);

	if (atomic_get(&nfc_mode) || !atomic_get(&disambiguation_visible)) {
		lv_obj_add_flag(ctx->disambiguation_icon, LV_OBJ_FLAG_HIDDEN);
		lv_obj_remove_flag(ctx->dist_label, LV_OBJ_FLAG_HIDDEN);
	} else {
		bool is_front = atomic_get(&disambiguation_is_front);
		const void *new_src = is_front ? (const void *)&user_detected : (const void *)&user_not_detected;
		bool was_hidden = lv_obj_has_flag(ctx->disambiguation_icon, LV_OBJ_FLAG_HIDDEN);
		lv_image_set_src(ctx->disambiguation_icon, new_src);
		if (was_hidden) {
			lv_obj_remove_flag(ctx->disambiguation_icon, LV_OBJ_FLAG_HIDDEN);
			lv_obj_move_foreground(ctx->disambiguation_icon);
		}

		if (is_front) {
			lv_obj_remove_flag(ctx->dist_label, LV_OBJ_FLAG_HIDDEN);
		} else {
			lv_obj_add_flag(ctx->dist_label, LV_OBJ_FLAG_HIDDEN);
		}
	}

	display_realign_upper_labels(ctx);
#endif
}

static void display_apply_clear_disambiguation_side(display_ctx_t *ctx)
{
#ifdef CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_FRONT_BACK_DETECTION
	atomic_set(&disambiguation_visible, false);
	lv_obj_add_flag(ctx->disambiguation_icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(ctx->dist_label, LV_OBJ_FLAG_HIDDEN);
	display_realign_upper_labels(ctx);
#endif
}

int display_update(display_ctx_t *ctx, dist_data_t val)
{
	int ret = 0;

	if (atomic_get(&nfc_mode)) {
		ret = snprintf(ctx->count_str, sizeof(ctx->count_str), "Current distance:\nN/A");
		if (ret < 0 || ret >= (int)sizeof(ctx->count_str)) {
			LOG_ERR("Error formatting count string");
			return -EINVAL;
		}
		lv_label_set_text(ctx->dist_label, ctx->count_str);
		return 0;
	}

	if (!distance_available) {
		(void)snprintf(ctx->count_str, sizeof(ctx->count_str), "Current distance:\n%s", LV_SYMBOL_WARNING);
	} else {
		ret = snprintf(ctx->count_str, sizeof(ctx->count_str), "Current distance:\n%d cm", val.dist);
		if (ret < 0 || ret >= (int)sizeof(ctx->count_str)) {
			LOG_ERR("Error formatting count string");
			return -EINVAL;
		}
	}

	lv_label_set_text(ctx->dist_label, ctx->count_str);

	return 0;
}

void display_thread_main(void *ctx, void *, void *)
{
	LOG_INF("Display thread started");

	display_ctx_t display = {};

	int err = display_init(&display);
	if (err < 0) {
		LOG_ERR("Display init err: %d", err);
		return;
	}

	k_timeout_t waiting = K_MSEC(lv_task_handler());

	LOG_INF("Display thread entering main loop");

	while (true) {
		LOG_DBG("Display waiting for %u ms", k_ticks_to_ms_floor32(waiting.ticks));

		uint32_t event = k_event_wait(&display_event, DISPLAY_ALL_EVENTS_MASK, false, waiting);
		if (event) {
			LOG_DBG("Received display event %08x", event);
			if (event & BIT(DISPLAY_UPDATE_VALUES)) {
				LOG_DBG("Display received update value event");
				display_update(&display, dist_data);
				k_event_clear(&display_event, BIT(DISPLAY_UPDATE_VALUES));
			}
			if (event & BIT(DISPLAY_UNLOCK_ACTION)) {
				LOG_DBG("Display received open animation event");
				open_animation(&display);
				howto_close_animation(&display, true);
				atomic_set(&lock_shown_open, true);
				k_event_clear(&display_event, BIT(DISPLAY_UNLOCK_ACTION));
			}
			if (event & BIT(DISPLAY_LOCK_ACTION)) {
				LOG_DBG("Display received close animation event");
				close_animation(&display);
				howto_close_animation(&display, false);
				atomic_set(&lock_shown_open, false);
				k_event_clear(&display_event, BIT(DISPLAY_LOCK_ACTION));
			}
			if (event & BIT(DISPLAY_DISCONNECTED_ACTION)) {
				LOG_DBG("Display received disconnected animation event");
				distance_available = false;
				if (atomic_get(&nfc_mode)) {
					display_update(&display, dist_data);
				} else {
					disconnected_animation(&display);
				}
				k_event_clear(&display_event, BIT(DISPLAY_DISCONNECTED_ACTION));
			}
			if (event & BIT(DISPLAY_OP_MODE_CHANGE)) {
				LOG_DBG("Display received op mode change event");
				op_mode_animation(&display, atomic_get(&nfc_mode));
				display_update(&display, dist_data);
				k_event_clear(&display_event, BIT(DISPLAY_OP_MODE_CHANGE));
			}
			if (event & BIT(DISPLAY_UPDATE_DISAMBIGUATION)) {
				LOG_DBG("Display received disambiguation update event");
				display_update_disambiguation_side(&display);
				k_event_clear(&display_event, BIT(DISPLAY_UPDATE_DISAMBIGUATION));
			}
		}
		LOG_DBG("call handler");
		waiting = K_MSEC(lv_task_handler());
	}
}

void display_post_event(enum display_events event)
{
	LOG_DBG("Posting display event %d", event);
	k_event_post(&display_event, BIT(event));
}

void display_post_distance_update(dist_data_t val)
{
	constexpr display_events event = DISPLAY_UPDATE_VALUES;
	dist_data = val;
	distance_available = true;

	LOG_DBG("Posting display event %d", event);
	k_event_post(&display_event, BIT(event));
}

void display_post_op_mode_change(bool nfcEnabled)
{
	constexpr display_events event = DISPLAY_OP_MODE_CHANGE;
	LOG_DBG("Posting display event %d", event);
	atomic_set(&nfc_mode, nfcEnabled);
	k_event_post(&display_event, BIT(event));
}

void display_post_disambiguation_side(bool isFront)
{
#ifndef CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_FRONT_BACK_DETECTION
	return;
#else
	bool changed = (atomic_set(&disambiguation_is_front, isFront) != (atomic_val_t)isFront) ||
		       !atomic_get(&disambiguation_visible);
	atomic_set(&disambiguation_visible, true);
	if (changed) {
		k_event_post(&display_event, BIT(DISPLAY_UPDATE_DISAMBIGUATION));
	}
#endif
}

void display_refresh_disambiguation_side(bool isFront)
{
#ifndef CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_FRONT_BACK_DETECTION
	return;
#else
	atomic_set(&disambiguation_is_front, isFront);
	atomic_set(&disambiguation_visible, true);
	k_event_post(&display_event, BIT(DISPLAY_UPDATE_DISAMBIGUATION));
#endif
}

void display_clear_disambiguation_side()
{
#ifndef CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_FRONT_BACK_DETECTION
	return;
#else
	atomic_set(&disambiguation_visible, false);
	k_event_post(&display_event, BIT(DISPLAY_UPDATE_DISAMBIGUATION));
#endif
}

void display_sync_lock_state(bool isOpen)
{
	if (atomic_get(&lock_shown_open) == (atomic_val_t)isOpen) {
		return;
	}

	atomic_set(&lock_shown_open, isOpen);
	display_post_lock_event(isOpen);
}

void display_refresh_lock_state(bool isOpen)
{
	atomic_set(&lock_shown_open, isOpen);
	display_post_lock_event(isOpen);
}

void display_init()
{
	LOG_INF("Creating display thread");
	k_event_init(&display_event);

	k_thread_create(&display_thread, display_thread_stack, CONFIG_DISPLAY_THREAD_STACK_SIZE, display_thread_main,
			NULL, NULL, NULL, CONFIG_DISPLAY_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&display_thread, "display");
}
