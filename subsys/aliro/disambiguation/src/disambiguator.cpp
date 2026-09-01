/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <disambiguator.h>

#include <algorithm>
#include <cerrno>
#include <doorlock/utils/mutex_guard.h>
#include <doorlock/utils/utils.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(Disambiguator, CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_LOG_LEVEL);

namespace {
/* Convert PDOA offset from degrees to Q4.11 fixed-point used by the library. */
constexpr double kPi{ 3.14159265358979323846 };
constexpr int16_t kCalibPdoaOffsetQ411{ static_cast<int16_t>(
	kPi * CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_PDOA_OFFSET_DEG / 180.0 * 2048.0) };
constexpr int16_t kCalibDistanceOffsetCm{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_DISTANCE_OFFSET_CM };
} // namespace

static_assert(CONFIG_DOOR_LOCK_BLE_UWB_MAX_SESSIONS <= ALIRO_DISAMBIGUATION_NB_SESSION_MAX,
	      "CONFIG_DOOR_LOCK_BLE_UWB_MAX_SESSIONS must be less than or equal to ALIRO_DISAMBIGUATION_NB_SESSION_MAX");

namespace Aliro::Uwb::Disambiguation {

int Disambiguator::Init(const disambiguation_parameters &params)
{
	int err = k_mutex_init(&mMutex);
	VerifyOrReturnValue(err == 0, err);

	err = aliro_disambiguation_init_processing();
	VerifyOrReturnValue(err == 0, err);

	mDisambiguationParams = params;
	mInitialized = true;

	return 0;
}

void Disambiguator::ResetAllSessions()
{
	VerifyOrReturn(mInitialized);

	DoorLock::Utils::MutexGuard lock{ mMutex };

	mCirCount = 0;
	mSessions.fill({});

	for (uint8_t sessionIdx = 0; sessionIdx < kMaxSessions; sessionIdx++) {
		aliro_disambiguation_reset_session(sessionIdx);
	}
}

void Disambiguator::ResetSession(uint8_t sessionIdx)
{
	VerifyOrReturn(mInitialized);
	VerifyOrReturn(sessionIdx < kMaxSessions);

	DoorLock::Utils::MutexGuard lock{ mMutex };

	mSessions[sessionIdx] = {};
	aliro_disambiguation_reset_session(sessionIdx);
}

void Disambiguator::FlushCir()
{
	VerifyOrReturn(mInitialized);

	DoorLock::Utils::MutexGuard lock{ mMutex };

	mCirCount = 0;
}

void Disambiguator::AddDistanceMeasurement(uint16_t distanceCm, uint8_t sessionIdx, bool error)
{
	VerifyOrReturn(mInitialized);

	DoorLock::Utils::MutexGuard lock{ mMutex };

	if (sessionIdx >= kMaxSessions) {
		return;
	}
	uint16_t adjusted = distanceCm;
	if (!error && kCalibDistanceOffsetCm != 0) {
		int32_t val = static_cast<int32_t>(distanceCm) - kCalibDistanceOffsetCm;
		if (val < 0) {
			val = 0;
		}
		adjusted = static_cast<uint16_t>(val);
	}
	aliro_disambiguation_put_distance_data(adjusted, sessionIdx, error,
					       CONFIG_DOOR_LOCK_ALIRO_UWB_MIN_RAN_MULTIPLIER);
	mSessions[sessionIdx].distanceCount++;
}

void Disambiguator::AddPdoaMeasurement(int16_t pdoaQ411, int16_t rssiQ88, uint8_t sessionIdx, bool pdoaError,
				       bool rssiError)
{
	VerifyOrReturn(mInitialized);

	DoorLock::Utils::MutexGuard lock{ mMutex };

	if (sessionIdx >= kMaxSessions) {
		return;
	}
	int16_t adjusted = pdoaQ411;
	if (!pdoaError && kCalibPdoaOffsetQ411 != 0) {
		adjusted = static_cast<int16_t>(pdoaQ411 - kCalibPdoaOffsetQ411);
	}
	aliro_disambiguation_put_pdoa_rssi_data(adjusted, static_cast<uint16_t>(rssiQ88), sessionIdx, pdoaError,
						rssiError, CONFIG_DOOR_LOCK_ALIRO_UWB_MIN_RAN_MULTIPLIER);
	mSessions[sessionIdx].pdoaCount++;

	/* Smooth the Q8.8 RSL used by the front/back safety gate. */
	if (!rssiError) {
		constexpr float kUwbRslAlpha{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_UWB_RSL_ALPHA / 1000.0f };
		const float rslDbm = static_cast<float>(rssiQ88) / 256.0f;
		SessionState &session = mSessions[sessionIdx];
		if (!session.mUwbRslValid) {
			session.mUwbRslEwma = rslDbm;
			session.mUwbRslValid = true;
		} else {
			session.mUwbRslEwma += kUwbRslAlpha * (rslDbm - session.mUwbRslEwma);
		}
	}
}

void Disambiguator::AddCirMeasurement(uint8_t *data, uint16_t size)
{
	VerifyOrReturn(mInitialized);

	DoorLock::Utils::MutexGuard lock{ mMutex };

	VerifyOrReturn(data);
	VerifyOrReturn(size > 0);
	aliro_disambiguation_put_cir_buffer(data, size);
	mCirCount++;
}

int Disambiguator::Process(Result &out, uint8_t sessionIdx)
{
	VerifyOrReturnValue(mInitialized, -EBUSY);

	DoorLock::Utils::MutexGuard lock{ mMutex };

	VerifyOrReturnValue(sessionIdx < kMaxSessions, -EINVAL);

	constexpr uint32_t kActualDupFactor{ CONFIG_DOOR_LOCK_ALIRO_UWB_MIN_RAN_MULTIPLIER * 2 };
	constexpr uint32_t kUwbReadyThreshold{ (ALIRO_DISAMBIGUATION_WINDOW_SIZE + kActualDupFactor - 1) /
					       kActualDupFactor };
	SessionState &session = mSessions[sessionIdx];
	if (mCirCount < ALIRO_DISAMBIGUATION_WINDOW_SIZE || session.distanceCount < kUwbReadyThreshold ||
	    session.pdoaCount < kUwbReadyThreshold) {
		return -EBUSY;
	}

	disambiguation_debug_results results{};
	(void)aliro_disambiguation(sessionIdx, &mDisambiguationParams, &results);

	/* Smooth p_ratio into radar evidence; use RSL only as a final safety gate. */
	constexpr float kEwmaAlpha{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_EWMA_ALPHA / 1000.0f };
	constexpr float kFrontThreshold{
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_FRONT_SCORE_THRESHOLD / 1000.0f
	};
	constexpr float kBackThreshold{
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_BACK_SCORE_THRESHOLD / 1000.0f
	};
	constexpr float kPRatioFull{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_P_RATIO_FULL / 10000.0f };
	constexpr float kUwbRslDropDb{ static_cast<float>(CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_UWB_RSL_DROP_DB) };
	constexpr float kUwbRslBlockDb{ static_cast<float>(
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_UWB_RSL_MAX_FRONT_DB) };
	constexpr float kUwbRslEnableDb{ static_cast<float>(
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_UWB_RSL_FRONT_ENABLE_DB) };

	static_assert(kFrontThreshold > kBackThreshold, "FRONT threshold must be above BACK threshold");
	static_assert(kEwmaAlpha > 0.0f && kEwmaAlpha <= 1.0f, "EWMA alpha must be in (0, 1]");
	static_assert(kPRatioFull > CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_P_RATIO / 10000.0f,
		      "P_RATIO_FULL must be greater than P_RATIO");
	static_assert(kUwbRslEnableDb < kUwbRslBlockDb, "UWB_RSL_FRONT_ENABLE_DB must be below UWB_RSL_MAX_FRONT_DB");

	const bool libraryRawFront = (results.side != 0);
	const bool radarSampleValid = results.p_ratio > 0.0f;
	const float pRatioLow = mDisambiguationParams.p_ratio_threshold;
	const float pRatioSpan = kPRatioFull - pRatioLow;
	const float radarEvidence =
		radarSampleValid ? std::clamp((results.p_ratio - pRatioLow) / pRatioSpan, 0.0f, 1.0f) : 0.0f;

	/* A zero p_ratio marks an invalid sample; retain the previous score. */
	if (radarSampleValid) {
		session.mFrontScore += kEwmaAlpha * (radarEvidence - session.mFrontScore);
		session.mFrontScore = std::clamp(session.mFrontScore, 0.0f, 1.0f);
	}

	const bool radarFront = session.lastResult.mSideIsFront ?
					(session.mFrontScore >= kBackThreshold) :
					(session.mFrontScore >= kFrontThreshold);

	/* Use absolute RSL before the first FRONT result, then compare with its reference. */
	bool coldStartRslVetoed = false;
	bool relativeRslVetoed = false;
	if (!session.mHasBeenFront) {
		if (!session.mUwbRslValid) {
			session.mColdStartRslBlocked = true;
		} else if (session.mUwbRslEwma <= kUwbRslEnableDb) {
			session.mColdStartRslBlocked = false;
		} else if (session.mUwbRslEwma >= kUwbRslBlockDb) {
			session.mColdStartRslBlocked = true;
		}
		coldStartRslVetoed = session.mColdStartRslBlocked;
	} else {
		relativeRslVetoed = !session.mUwbRslValid || !session.mUwbRefSet ||
				   (session.mUwbRslEwma - session.mRefUwbRsl >= kUwbRslDropDb);
	}
	const bool uwbRslVetoed = coldStartRslVetoed || relativeRslVetoed;
	out.mSideIsFront = radarFront && !uwbRslVetoed;
	out.mLibrarySideIsFront = libraryRawFront;

	if (out.mSideIsFront) {
		session.mHasBeenFront = true;

		/* Initialize on FRONT and track slowly without learning a vetoed level. */
		constexpr float kUwbRefAlpha{ 0.03f };
		if (!session.mUwbRefSet) {
			session.mRefUwbRsl = session.mUwbRslEwma;
			session.mUwbRefSet = true;
			LOG_DBG("sess%u UWB RSL reference init: -%ddBm", sessionIdx,
				static_cast<int>(session.mRefUwbRsl));
		} else if (!relativeRslVetoed) {
			session.mRefUwbRsl += kUwbRefAlpha * (session.mUwbRslEwma - session.mRefUwbRsl);
		}
	}

	/* The radar EWMA replaces the library's separate temporal gate. */
	const bool distanceAllowsUnlock = results.distance_cm > mDisambiguationParams.blind_distance &&
					  results.distance_cm <= mDisambiguationParams.secure_bubble_radius;
	out.mUnlockAllowed = out.mSideIsFront && distanceAllowsUnlock;

	const int32_t pRatioU6 = static_cast<int32_t>(results.p_ratio * 1000000.0f);
	const int32_t scorePermille = static_cast<int32_t>(session.mFrontScore * 1000.0f);
	const int32_t evidencePermille = static_cast<int32_t>(radarEvidence * 1000.0f);
	const int32_t displayedRslDbm =
		session.mUwbRslValid ? -static_cast<int32_t>(session.mUwbRslEwma) : 0;
	const int32_t rslDropDb = (session.mUwbRefSet && session.mUwbRslValid) ?
					  static_cast<int32_t>(session.mUwbRslEwma - session.mRefUwbRsl) :
					  0;
	LOG_DBG("sess%u lib=%s final=%s pratio=%d evidence=%d/1000 cir=%d blk=%d dist=%ucm score=%d/1000 uwb=%ddBm drop=%ddB%s%s%s",
		sessionIdx, libraryRawFront ? "FRONT" : "BACK", out.mSideIsFront ? "FRONT" : "BACK", pRatioU6,
		evidencePermille, results.CIR, results.noise_blocks, results.distance_cm, scorePermille,
		displayedRslDbm, rslDropDb, !radarSampleValid ? " [radar-invalid]" : "",
		coldStartRslVetoed ? " [rsl-cold]" : "",
		relativeRslVetoed ? " [rsl-drop]" : "");

	out.mDistanceCm = results.distance_cm;
	out.mMeanPdoaDeg = results.mean_pdoa;
	out.mPRatio = results.p_ratio;
	out.mCir = results.CIR;
	out.mNoiseBlocks = results.noise_blocks;
	session.lastResult = out;
	session.hasResult = true;

	return 0;
}

bool Disambiguator::IsAnyUnlockAllowed() const
{
	DoorLock::Utils::MutexGuard lock{ mMutex };

	// Any session on the front side within the secure bubble allows unlock.
	for (const SessionState &session : mSessions) {
		if (session.hasResult && session.lastResult.mUnlockAllowed) {
			return true;
		}
	}
	return false;
}

std::optional<Result> Disambiguator::TryGetLastResult(uint8_t sessionIdx)
{
	VerifyOrReturnValue(mInitialized && sessionIdx < kMaxSessions, std::nullopt);

	DoorLock::Utils::MutexGuard lock{ mMutex };

	VerifyOrReturnValue(mSessions[sessionIdx].hasResult, std::nullopt);

	return mSessions[sessionIdx].lastResult;
}

} // namespace Aliro::Uwb::Disambiguation
