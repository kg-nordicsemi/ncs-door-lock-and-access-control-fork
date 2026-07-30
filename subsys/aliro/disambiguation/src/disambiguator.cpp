/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <disambiguator.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
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

	/* Track UWB RSL for front/back discrimination.
	 * rsl_q8 is a signed Q8.8 value (stored as int16_t), where rsl_dBm = rssiQ88 / 256.0f.
	 * More negative = weaker signal. When phone goes behind door, RSL drops by ~5-15 dB. */
	if (!rssiError) {
		constexpr float kUwbRslAlpha{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_UWB_RSL_ALPHA / 1000.0f };
		const float rslDbm = static_cast<float>(rssiQ88) / 256.0f;
		SessionState &sess = mSessions[sessionIdx];
		if (!sess.mUwbRslValid) {
			sess.mUwbRslEwma = rslDbm;
			sess.mUwbRslValid = true;
		} else {
			sess.mUwbRslEwma += kUwbRslAlpha * (rslDbm - sess.mUwbRslEwma);
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
	SessionState &sess = mSessions[sessionIdx];
	if (mCirCount < ALIRO_DISAMBIGUATION_WINDOW_SIZE || sess.distanceCount < kUwbReadyThreshold ||
	    sess.pdoaCount < kUwbReadyThreshold) {
		return -EBUSY;
	}

	disambiguation_debug_results results{};
	(void)aliro_disambiguation(sessionIdx, &mDisambiguationParams, &results);

	/* The Qorvo side bit is intentionally diagnostic only. It is a binary threshold
	 * over p_ratio/noise_blocks and cannot distinguish body motion in front of the
	 * radar from the phone being in front. Convert p_ratio to continuous evidence,
	 * smooth it once, and apply RSL only as a final phone-position safety gate. */
	constexpr float kEwmaAlpha{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_EWMA_ALPHA / 1000.0f };
	constexpr float kFrontThresh{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_FRONT_SCORE_THRESHOLD / 1000.0f };
	constexpr float kBackThresh{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_BACK_SCORE_THRESHOLD / 1000.0f };
	constexpr float kPRatioFull{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_P_RATIO_FULL / 10000.0f };
	constexpr float kUwbRslDropDb{ static_cast<float>(CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_UWB_RSL_DROP_DB) };
	constexpr float kUwbRslBlockDb{ static_cast<float>(
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_UWB_RSL_MAX_FRONT_DB) };
	constexpr float kUwbRslEnableDb{ static_cast<float>(
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_UWB_RSL_FRONT_ENABLE_DB) };

	static_assert(kFrontThresh > kBackThresh, "FRONT threshold must be above BACK threshold (hysteresis gap)");
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

	/* Invalid samples occur in the blind zone or when CIR exceeds MAX_CIR. The
	 * library then reports p_ratio=0 and reuses its previous side, so holding the
	 * score is the only non-biased update. */
	if (radarSampleValid) {
		sess.mFrontScore += kEwmaAlpha * (radarEvidence - sess.mFrontScore);
		sess.mFrontScore = std::clamp(sess.mFrontScore, 0.0f, 1.0f);
	}

	const bool radarFront =
		sess.lastResult.mSideIsFront ? (sess.mFrontScore >= kBackThresh) : (sess.mFrontScore >= kFrontThresh);

	/* RSL is a final fail-safe only; it never modifies radar evidence or EWMA.
	 * Before first FRONT, use 71/73 dB hysteresis and fail closed without RSL.
	 * Afterwards compare against the frozen/slowly tracked in-front reference. */
	bool coldStartRslVetoed = false;
	bool relativeRslVetoed = false;
	if (!sess.mHasBeenFront) {
		if (!sess.mUwbRslValid) {
			sess.mColdStartRslBlocked = true;
		} else if (sess.mUwbRslEwma <= kUwbRslEnableDb) {
			sess.mColdStartRslBlocked = false;
		} else if (sess.mUwbRslEwma >= kUwbRslBlockDb) {
			sess.mColdStartRslBlocked = true;
		}
		coldStartRslVetoed = sess.mColdStartRslBlocked;
	} else {
		relativeRslVetoed =
			!sess.mUwbRslValid || !sess.mUwbRefSet || (sess.mUwbRslEwma - sess.mRefUwbRsl >= kUwbRslDropDb);
	}
	const bool uwbRslVetoed = coldStartRslVetoed || relativeRslVetoed;
	out.mSideIsFront = radarFront && !uwbRslVetoed;
	out.mLibrarySideIsFront = libraryRawFront;

	if (out.mSideIsFront) {
		sess.mHasBeenFront = true;

		/* Initialize only after both radar and cold-start RSL agree on FRONT.
		 * Freeze while vetoed; otherwise track slowly to accommodate normal path
		 * loss without learning the attenuated behind-door level. */
		constexpr float kUwbRefAlpha{ 0.03f };
		if (!sess.mUwbRefSet) {
			sess.mRefUwbRsl = sess.mUwbRslEwma;
			sess.mUwbRefSet = true;
			LOG_DBG("sess%u UWB RSL reference init: -%ddBm", sessionIdx, static_cast<int>(sess.mRefUwbRsl));
		} else if (!relativeRslVetoed) {
			sess.mRefUwbRsl += kUwbRefAlpha * (sess.mUwbRslEwma - sess.mRefUwbRsl);
		}
	}

	/* The radar EWMA already provides temporal confirmation, so do not retain the
	 * library's separate three-raw-FRONT door_open gate. Keep only the calibrated
	 * secure-bubble and blind-zone distance checks. */
	const bool distanceAllowsUnlock = results.distance_cm > mDisambiguationParams.blind_distance &&
					  results.distance_cm <= mDisambiguationParams.secure_bubble_radius;
	out.mUnlockAllowed = out.mSideIsFront && distanceAllowsUnlock;

	const int32_t pRatioU6 = static_cast<int32_t>(results.p_ratio * 1000000.0f);
	const int32_t scorePermille = static_cast<int32_t>(sess.mFrontScore * 1000.0f);
	const int32_t evidencePermille = static_cast<int32_t>(radarEvidence * 1000.0f);
	/* UWB RSL displayed as negative dBm (rsl_q8 is "absolute value", so negate for display).
	 * e.g. uwb=-67dBm = strong (phone in front), uwb=-80dBm = weak (phone behind door).
	 * uwb-drop shows (current - ref) in dBm: positive = phone moved away/behind door. */
	const int32_t uwbRslNeg = sess.mUwbRslValid ? -static_cast<int32_t>(sess.mUwbRslEwma) : 0;
	/* uwbDrop > 0 = phone further/behind door vs reference (higher absolute dBm = weaker) */
	const int32_t uwbDropDb =
		(sess.mUwbRefSet && sess.mUwbRslValid) ? static_cast<int32_t>(sess.mUwbRslEwma - sess.mRefUwbRsl) : 0;
	LOG_DBG("sess%u lib=%s final=%s pratio=%d evidence=%d/1000 cir=%d blk=%d dist=%ucm score=%d/1000 uwb=%ddBm drop=%ddB%s%s%s",
		sessionIdx, libraryRawFront ? "FRONT" : "BACK", out.mSideIsFront ? "FRONT" : "BACK", pRatioU6,
		evidencePermille, results.CIR, results.noise_blocks, results.distance_cm, scorePermille, uwbRslNeg,
		uwbDropDb, !radarSampleValid ? " [radar-invalid]" : "", coldStartRslVetoed ? " [rsl-cold]" : "",
		relativeRslVetoed ? " [rsl-drop]" : "");

	out.mDistanceCm = results.distance_cm;
	out.mMeanPdoaDeg = results.mean_pdoa;
	out.mPRatio = results.p_ratio;
	out.mCir = results.CIR;
	out.mNoiseBlocks = results.noise_blocks;
	sess.lastResult = out;
	sess.hasResult = true;

	return 0;
}

bool Disambiguator::IsAnyUnlockAllowed() const
{
	DoorLock::Utils::MutexGuard lock{ mMutex };

	// Any session on the front side within the secure bubble allows unlock.
	for (const SessionState &sess : mSessions) {
		if (sess.hasResult && sess.lastResult.mUnlockAllowed) {
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
