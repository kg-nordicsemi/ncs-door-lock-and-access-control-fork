/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#pragma once

#include <aliro/errors.h>

/**
 * @brief Initializes the Aliro stack.
 *
 * @return EXIT_SUCCESS if the Aliro stack was initialized successfully, EXIT_FAILURE otherwise.
 */
int AliroInit();

/**
 * @brief Starts the Aliro stack.
 *
 * @return EXIT_SUCCESS if the Aliro stack was started successfully, EXIT_FAILURE otherwise.
 */
int AliroStart();

/**
 * @brief Stops the Aliro stack.
 *
 * @return EXIT_SUCCESS if the Aliro stack was stopped successfully, EXIT_FAILURE otherwise.
 */
int AliroStop();

#ifdef CONFIG_DOOR_LOCK_BLE_UWB

enum class TransportMode {
	Nfc,
	BleUwb,
};

void AliroToggleTransportMode();

TransportMode GetTransportMode();

#endif // CONFIG_DOOR_LOCK_BLE_UWB

/**
 * @brief Check if Aliro stack is currently running.
 *
 * @return true if Aliro stack is running, false otherwise.
 */
bool IsAliroRunning();

/**
 * @brief Clears Aliro storage.
 *
 * @param reinitializeStorage Whether to reinitialize the storage after clearing.
 */
void ClearStorageAliro(bool reinitializeStorage);

#if defined(CONFIG_DOOR_LOCK_DISPLAY) && defined(CONFIG_DOOR_LOCK_BLE_UWB)
/**
 * @brief Refresh the display to reflect the current disambiguation side and lock state.
 *
 * Called after UWB session resume or other state transitions that may have been missed
 * by the normal per-ranging-data update path.
 */
void AliroDisplayRefreshState();
#endif // CONFIG_DOOR_LOCK_DISPLAY && CONFIG_DOOR_LOCK_BLE_UWB
