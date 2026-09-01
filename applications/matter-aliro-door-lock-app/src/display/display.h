/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include <core/lv_obj.h>
#include <lvgl.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

#define DISPLAY_STR_MAX_LEN (50)
#define DISPLAY_ALL_EVENTS_MASK (UINT32_MAX)

typedef struct {
	int32_t dist;
	int32_t threshold;
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
	char count_str[DISPLAY_STR_MAX_LEN];
} display_ctx_t;

enum display_events {
	DISPLAY_UPDATE_VALUES = 0,
	DISPLAY_LOCK_ACTION,
	DISPLAY_UNLOCK_ACTION,
	DISPLAY_DISCONNECTED_ACTION,
	DISPLAY_OP_MODE_CHANGE,
	DISPLAY_UPDATE_DISAMBIGUATION
};

/** Initialize the display thread. */
void display_init();

/** Post a display event. */
void display_post_event(enum display_events event);

/** Update the displayed distance. */
void display_post_distance_update(dist_data_t val);

/** Update the displayed transport mode. */
void display_post_op_mode_change(bool nfcEnabled);

/** Update the displayed front/back result if it changed. */
void display_post_disambiguation_side(bool isFront);

/** Force a front/back redraw, for example after session resume. */
void display_refresh_disambiguation_side(bool isFront);

/** Hide the front/back icon after disconnect. */
void display_clear_disambiguation_side();

/** Update the lock state only when the displayed state differs. */
void display_sync_lock_state(bool isOpen);

/** Force a lock-state redraw. */
void display_refresh_lock_state(bool isOpen);

/** Render a distance update. */
int display_update(display_ctx_t *ctx, dist_data_t val);

/** Render the unlocked state. */
void open_animation(display_ctx_t *ctx);

/** Render the locked state. */
void close_animation(display_ctx_t *ctx);

/** Render the disconnected state. */
void disconnected_animation(display_ctx_t *ctx);
