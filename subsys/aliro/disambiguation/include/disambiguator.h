/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <zephyr/kernel.h>

#include <aliro_disambiguation.h>

namespace Aliro::Uwb::Disambiguation {

/** @brief Output of @ref Disambiguator::Process: door decision, phone side, and ranging diagnostics. */
struct Result {
	bool mSideIsFront{ false };
	bool mUnlockAllowed{ false };
	uint16_t mDistanceCm{ 0 };
	float mMeanPdoaDeg{ 0.0f };
	float mPRatio{ 0.0f };
	int mCir{ 0 };
	int mNoiseBlocks{ 0 };

	/** @return True if the peer is on the front side (@ref mSideIsFront). */
	bool IsFront() const { return mSideIsFront; }
	/** @return True if unlock is allowed (front side and within secure bubble). */
	bool IsUnlockAllowed() const { return mUnlockAllowed; }
};

/** @brief Singleton UWB front/back and door disambiguation over buffered CIR, distance, and PDOA/RSSI. */
class Disambiguator {
public:
	/** @brief Returns the process-wide disambiguator instance. */
	static Disambiguator &Instance()
	{
		static Disambiguator sInstance;
		return sInstance;
	}

	/** @brief Enables the module.
	 *  @param params Disambiguation parameters.
	 *  @retval 0 on success, or a negative error code on failure.
	 */
	int Init(const disambiguation_parameters &params);

	/** @brief Clears measurement state and processing state for a single session.
	 *  @param sessionIdx Session index in @c [0, CONFIG_DOOR_LOCK_BLE_UWB_MAX_SESSIONS).
	 */
	void ResetSession(uint8_t sessionIdx);

	/** @brief Resets the global CIR counter, blocking Process() until the 40-sample window refills.
	 *  Call after removing the last active session.
	 */
	void FlushCir();

	/** @brief Feeds one distance sample for a session (internally calibrated when not in error).
	 *  @param distanceCm Measured distance in centimeters.
	 *  @param sessionIdx Session index in @c [0, CONFIG_DOOR_LOCK_BLE_UWB_MAX_SESSIONS).
	 *  @param error When true, the sample is passed through as an error to processing.
	 */
	void AddDistanceMeasurement(uint16_t distanceCm, uint8_t sessionIdx, bool error);

	/** @brief Feeds one PDOA and RSSI sample pair for a session.
	 *  @param pdoaQ411 Phase difference of arrival in Q4.11 fixed-point units.
	 *  @param rssiQ88 RSSI in Q8.8 fixed-point units (passed as @c uint16_t to processing).
	 *  @param sessionIdx Session index in @c [0, CONFIG_DOOR_LOCK_BLE_UWB_MAX_SESSIONS).
	 *  @param pdoaError When true, PDOA is treated as invalid for processing.
	 *  @param rssiError When true, RSSI is treated as invalid for processing.
	 */
	void AddPdoaMeasurement(int16_t pdoaQ411, int16_t rssiQ88, uint8_t sessionIdx, bool pdoaError, bool rssiError);

	/** @brief Feeds a raw CIR buffer (global to all sessions in the current implementation).
	 *  @param data CIR bytes from the UWB stack.
	 *  @param size Length of @p data in bytes.
	 */
	void AddCirMeasurement(uint8_t *data, uint16_t size);

	/** @brief Feeds a BLE RSSI sample for a session.
	 *
	 *  BLE signals are attenuated significantly more by doors than UWB (~15-25 dB extra loss).
	 *  When the smoothed RSSI drops more than BLE_RSSI_DROP_DB below the reference captured
	 *  at first FRONT confirmation, the disambiguator caps the score to prevent body-motion
	 *  radar spikes from triggering a false FRONT re-detection.
	 *
	 *  Call this ~every 500ms while a BLE ranging session is active.
	 *
	 *  @param rssiDbm Measured BLE connection RSSI in dBm (typically -30 to -100).
	 *  @param sessionIdx Session index in @c [0, CONFIG_DOOR_LOCK_BLE_UWB_MAX_SESSIONS).
	 */
	void AddBleRssiMeasurement(int8_t rssiDbm, uint8_t sessionIdx);

	/** @brief Runs disambiguation when enough CIR, distance, and PDOA samples are buffered.
	 *  @param[out] out Filled with door/side decision and algorithm metrics on success.
	 *  @param sessionIdx Session index in @c [0, CONFIG_DOOR_LOCK_BLE_UWB_MAX_SESSIONS).
	 *  @retval 0 Success; @p out is stored as the last result for @p sessionIdx.
	 *  @retval -EINVAL @p sessionIdx is out of range.
	 *  @retval -EBUSY Not initialized, or CIR/distance/PDOA counts below the internal ready threshold.
	 */
	int Process(Result &out, uint8_t sessionIdx);

	/** @brief Returns a snapshot of the last successful @ref Process output for a session.
	 *  @param sessionIdx Session index.
	 *  @return Copy of the last result, or empty if unavailable.
	 */
	std::optional<Result> TryGetLastResult(uint8_t sessionIdx);

	/** @brief Returns true if any active session currently allows unlock.
	 *
	 *  Unlock is allowed when the disambiguator reports front side AND the
	 *  user is within @c secure_bubble_radius for that session.
	 */
	bool IsAnyUnlockAllowed() const;

private:
	Disambiguator() = default;
	~Disambiguator() = default;
	Disambiguator(const Disambiguator &) = delete;
	Disambiguator &operator=(const Disambiguator &) = delete;
	Disambiguator(Disambiguator &&) = delete;
	Disambiguator &operator=(Disambiguator &&) = delete;

	void ResetAllSessions();

	constexpr static uint8_t kMaxSessions{ CONFIG_DOOR_LOCK_BLE_UWB_MAX_SESSIONS };

	mutable k_mutex mMutex{};
	bool mInitialized{ false };
	uint32_t mCirCount{ 0 };
	disambiguation_parameters mDisambiguationParams{};

	struct SessionState {
		bool hasResult{ false };
		uint32_t distanceCount{ 0 };
		uint32_t pdoaCount{ 0 };
		Result lastResult{};
		/* Soft EWMA confidence score: 0.0 = full BACK, 1.0 = full FRONT.
		 * Updated each Process() call with adaptive alpha and PDOA veto weighting.
		 * Hysteresis thresholds on this score replace the old consecutive-count filter. */
		float mFrontScore{ 0.0f };
		/* Set to true the first time the score crosses FRONT threshold within a session.
		 * Once set, re-entry into FRONT from BACK uses the slower RE_FRONT_CLIMB_FACTOR
		 * instead of FRONT_CLIMB_FACTOR, making body-motion false re-detections harder. */
		bool mHasBeenFront{ false };
		/* Previous p_ratio for jump-rate detection.
		 * Body-motion spikes cause p_ratio to jump >2x in a single tick (86 ms).
		 * Genuine phone approaches ramp up gradually (<1.5x per tick). */
		float mPrevPRatio{ 0.0f };
		/* Cooldown ticks remaining after a suspicious p_ratio jump was detected.
		 * During cooldown the BACK→FRONT climb is reduced to RE_FRONT_CLIMB_FACTOR
		 * regardless of mHasBeenFront, blocking false FRONT from sudden body-motion spikes. */
		uint8_t mJumpCooldown{ 0 };

		/* BLE RSSI discriminator.
		 *
		 * BLE signals are attenuated 15-25 dB by a door; UWB is much less affected.
		 * When the phone moves behind the door, BLE RSSI drops noticeably while the
		 * body in front of the radar can still trigger high p_ratio, causing false FRONT.
		 *
		 * mBleRssiEwma  — smoothed BLE RSSI in dBm (EWMA, updated via AddBleRssiMeasurement).
		 * mBleRssiValid — true once at least one RSSI sample has been received.
		 * mRefBleRssi   — RSSI captured when FRONT was first confirmed; used as baseline.
		 * mBleRefSet    — true when mRefBleRssi has been captured. */
		float mBleRssiEwma{ 0.0f };
		bool mBleRssiValid{ false };
		float mRefBleRssi{ 0.0f };
		bool mBleRefSet{ false };
	};

	std::array<SessionState, kMaxSessions> mSessions{};
};

} // namespace Aliro::Uwb::Disambiguation
