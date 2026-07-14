/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include <core/lv_obj.h>
#include <lvgl.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

// TODO move to sample Kconfig
#define DISPLAY_STR_MAX_LEN (50)
#define DISPLAY_CHART_LEN (10)
#define DISPLAY_ALL_EVENTS_MASK (UINT32_MAX)

typedef struct {
	int32_t dist;
	int32_t treshold;
} dist_data_t;

typedef struct {
	const struct device *dev;
	lv_obj_t *nordic_logo;
	lv_obj_t *status_label;
	lv_obj_t *howto_close_label;
	lv_obj_t *op_mode_label;
	lv_obj_t *op_mode_switch_label;
	lv_obj_t *disambiguation_icon;
	lv_obj_t *dist_label;
	lv_obj_t *dist_limit_label;
	lv_obj_t *chart;
	lv_chart_series_t *ser1;
	lv_chart_series_t *ser2;
	int32_t value_history[DISPLAY_CHART_LEN];
	char count_str[DISPLAY_STR_MAX_LEN];
	char count_str2[DISPLAY_STR_MAX_LEN];
	bool is_opened;
} display_ctx_t;

enum display_events {
	DISPLAY_UPDATE_VALUES = 0,
	DISPLAY_BOOT_ANIMATION,
	DISPLAY_LOCK_ACTION,
	DISPLAY_UNLOCK_ACTION,
	DISPLAY_DISCONNECTED_ACTION,
	DISPLAY_OP_MODE_CHANGE,
	DISPLAY_UPDATE_DISAMBIGUATION
};

/**
 * @brief Initialize tft display
 * *
 * @return int 0 on success, negative error code otherwise
 */
void display_init();

/**
 * @brief Post an event to the display
 *
 * @param event Event to post
 */
void display_post_event(enum display_events event);

/**
 * @brief Post an event to the display to update the distance
 *
 * @param val Distance value
 */
void display_post_distance_update(dist_data_t val);

/**
 * @brief Post an event to the display to change the op mode
 *
 * @param isNfc True if NFC is active, false if BLE/UWB is active
 */
void display_post_op_mode_change(bool isNfc);

/**
 * @brief Post an event to the display to update the disambiguation side (FRONT/BACK).
 *
 * @param isFront True when any session is classified as front.
 */
void display_post_disambiguation_side(bool isFront);

/**
 * @brief Force the display to redraw the disambiguation side (FRONT/BACK).
 *
 * Unlike display_post_disambiguation_side(), always posts an update event so the UI
 * can resync after suspend or other paths that bypass the disambiguation callback.
 *
 * @param isFront True when any session is classified as front.
 */
void display_refresh_disambiguation_side(bool isFront);

/**
 * @brief Hide the disambiguation icon and show the distance readout.
 *
 * Use on session disconnect. For ranging suspend use
 * display_refresh_disambiguation_side(false) instead (user not detected, distance hidden).
 */
void display_clear_disambiguation_side();

/**
 * @brief Sync OPEN/CLOSED on the display when it differs from the actual lock state.
 *
 * Posts a lock/unlock event only when the UI is out of date (no animation when already correct).
 *
 * @param isOpen True when the lock is physically open (unsecured).
 */
void display_sync_lock_state(bool isOpen);

/**
 * @brief Force the display to redraw OPEN/CLOSED from the actual lock state.
 *
 * Always posts a lock/unlock event so the UI can resync after resume or other missed transitions.
 *
 * @param isOpen True when the lock is physically open (unsecured).
 */
void display_refresh_lock_state(bool isOpen);

/**
 * @brief Update tft display
 * @note This function should be called in main loop
 *
 * @param ctx Display context
 *
 * @return int 0 on success, negative error code otherwise
 */
int display_update(display_ctx_t *ctx, dist_data_t val);

/**
 * @brief Animation on start
 *
 * @param ctx Display context
 */
void boot_animation(display_ctx_t *ctx);

/**
 * @brief Animation on open
 *
 * @param ctx Display context
 */
void open_animation(display_ctx_t *ctx);

/**
 * @brief Animation on close
 *
 * @param ctx Display context
 */
void close_animation(display_ctx_t *ctx);

/**
 * @brief Animation on disconnected
 *
 * @param ctx Display context
 */
void disconnected_animation(display_ctx_t *ctx);
