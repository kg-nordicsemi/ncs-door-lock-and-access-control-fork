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

void Disambiguator::AddBleRssiMeasurement(int8_t rssiDbm, uint8_t sessionIdx)
{
	VerifyOrReturn(mInitialized);
	VerifyOrReturn(sessionIdx < kMaxSessions);

	DoorLock::Utils::MutexGuard lock{ mMutex };

	SessionState &sess = mSessions[sessionIdx];
	constexpr float kAlpha{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_BLE_RSSI_ALPHA / 1000.0f };
	if (!sess.mBleRssiValid) {
		sess.mBleRssiEwma = static_cast<float>(rssiDbm);
		sess.mBleRssiValid = true;
	} else {
		sess.mBleRssiEwma += kAlpha * (static_cast<float>(rssiDbm) - sess.mBleRssiEwma);
	}
	LOG_DBG("sess%u BLE RSSI: %d dBm → EWMA=%.1f dBm ref=%.1f dBm%s", sessionIdx, rssiDbm,
		static_cast<double>(sess.mBleRssiEwma), static_cast<double>(sess.mRefBleRssi),
		sess.mBleRefSet ? " [ref-set]" : "");
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
	const int unlockAllowed = aliro_disambiguation(sessionIdx, &mDisambiguationParams, &results);

	/* --- EWMA confidence filter with p_ratio and CIR weighting ---
	 *
	 * Instead of counting N consecutive FRONT/BACK decisions, we maintain a soft score
	 * in [0.0, 1.0] where 0.0 = full BACK confidence, 1.0 = full FRONT confidence.
	 *
	 * Each Process() tick:
	 *  1. Compute how far p_ratio is from its threshold (confidence of the raw decision).
	 *     A borderline result (p_ratio barely over/under threshold) has little influence.
	 *  2. Dampen the EWMA step when CIR is elevated. High CIR indicates radar saturation
	 *     or multipath, which inflates p_ratio and makes it unreliable. Reducing alpha in
	 *     this condition prevents a radar-power burst from driving the score into FRONT.
	 *     NOTE: PDOA is NOT used as a discriminator here — it is only reliable within the
	 *     ±70° cone in front of the radar and is effectively random when the phone is
	 *     behind the door, so it cannot safely veto or confirm FRONT decisions.
	 *  3. Update the score with the adaptive EWMA step.
	 *  4. Apply hysteresis: flip to FRONT only above kFrontThresh; flip back to BACK only
	 *     below kBackThresh. The gap between the two thresholds is the dead-band.
	 */

	/* Compile-time constants derived from Kconfig. */
	constexpr float kEwmaAlpha{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_EWMA_ALPHA / 1000.0f };
	constexpr float kFrontThresh{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_FRONT_SCORE_THRESHOLD / 1000.0f };
	constexpr float kBackThresh{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_BACK_SCORE_THRESHOLD / 1000.0f };
	constexpr float kCirDampStrength{
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_CIR_DAMP_STRENGTH / 1000.0f };
	constexpr float kFrontClimbFactor{
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_FRONT_CLIMB_FACTOR / 1000.0f };
	constexpr float kReFrontClimbFactor{
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_RE_FRONT_CLIMB_FACTOR / 1000.0f };
	constexpr float kBackSinkFactor{
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_BACK_SINK_FACTOR / 1000.0f };
	constexpr float kJumpRatioThreshold{
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_JUMP_RATIO_THRESHOLD / 100.0f };
	constexpr uint8_t kJumpCooldownTicks{
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_JUMP_COOLDOWN_TICKS };
	constexpr float kFrontConfidenceRequired{
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_FRONT_CONFIDENCE_REQUIRED / 1000.0f };
	constexpr float kLowConfidenceScoreCap{
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_LOW_CONFIDENCE_SCORE_CAP / 1000.0f };
	constexpr float kBleRssiDropDb{
		static_cast<float>(CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_BLE_RSSI_DROP_DB) };
	constexpr float kBleRssiScoreCap{
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_BLE_RSSI_SCORE_CAP / 1000.0f };

	static_assert(kFrontThresh > kBackThresh, "FRONT threshold must be above BACK threshold (hysteresis gap)");
	static_assert(kEwmaAlpha > 0.0f && kEwmaAlpha <= 1.0f, "EWMA alpha must be in (0, 1]");
	static_assert(kCirDampStrength >= 0.0f && kCirDampStrength <= 1.0f,
		      "CIR damp strength must be in [0, 1]");
	static_assert(kFrontClimbFactor > 0.0f && kFrontClimbFactor <= 1.0f,
		      "FRONT climb factor must be in (0, 1]");
	static_assert(kReFrontClimbFactor > 0.0f && kReFrontClimbFactor <= 1.0f,
		      "RE_FRONT climb factor must be in (0, 1]");
	static_assert(kBackSinkFactor > 0.0f && kBackSinkFactor <= 1.0f,
		      "BACK sink factor must be in (0, 1]");
	static_assert(kJumpRatioThreshold > 1.0f,
		      "Jump ratio threshold must be > 1.0 (ratio of new/old p_ratio)");
	static_assert(kLowConfidenceScoreCap < kFrontThresh,
		      "LOW_CONFIDENCE_SCORE_CAP must be strictly below FRONT_SCORE_THRESHOLD");

	const bool rawFront = (results.side != 0);

	/* 1. p_ratio confidence: how far the measurement is from the decision boundary,
	 *    normalised to [0, 1]. A result barely over/under the threshold contributes
	 *    little; a result far from the boundary contributes fully.
	 *
	 *    Special case — p_ratio == 0.0: the library sets p_ratio to zero when the
	 *    phone is within the blind zone (dist < blind_distance_cm) or when CIR
	 *    exceeds max_cir_threshold. In both cases p_ratio carries no information
	 *    about the true side; the library simply reuses its previous decision.
	 *    Treating |0 − threshold| / threshold as confidence would yield 1.0
	 *    (maximum), which is the opposite of the correct interpretation.
	 *    Instead we clamp the confidence to 0.0 so the tick has the minimum
	 *    EWMA step, keeping the score frozen rather than falsely reinforcing it. */
	const float pRatioThreshold = mDisambiguationParams.p_ratio_threshold;
	const float pRatioNorm = (pRatioThreshold > 0.0f) ? pRatioThreshold : 0.1f;
	const float pRatioConfidence = (results.p_ratio == 0.0f)
					       ? 0.0f
					       : std::min(std::fabs(results.p_ratio - pRatioThreshold) /
								  pRatioNorm,
							  1.0f);

	/* 2. CIR dampening: high CIR → radar saturation / multipath → p_ratio less reliable.
	 *    cirFraction is 0 at CIR=0 and 1 at CIR=max_cir_threshold.
	 *    cirDampFactor drops from 1.0 (reliable) towards (1 - kCirDampStrength) (saturated),
	 *    so alpha shrinks and the score moves less for each saturated tick. */
	const float cirMax = static_cast<float>(mDisambiguationParams.max_cir_threshold);
	const float cirFraction = (cirMax > 0.0f)
					  ? std::min(static_cast<float>(results.CIR) / cirMax, 1.0f)
					  : 0.0f;
	const float cirDampFactor = 1.0f - kCirDampStrength * cirFraction;

	/* 3. Adaptive EWMA update.
	 *    alpha = base_alpha × p_ratio_confidence_factor × cir_damp_factor × asymmetric_factor.
	 *
	 *    asymmetric_factor: slows score movement at both transition boundaries.
	 *
	 *    kFrontClimbFactor — applied when rawFront=true and current state is BACK.
	 *      Resists false FRONT detection driven by sustained low-confidence signals
	 *      (e.g. radar body-detection artefact while phone is behind the door).
	 *
	 *    kBackSinkFactor — applied when rawFront=false and current state is FRONT.
	 *      Resists transient BACK flickers when the phone is genuinely in front.
	 *      The library can temporarily output rawFront=false when noise_blocks spikes
	 *      above nb_blocks_threshold (even with very high p_ratio), causing the score
	 *      to fall rapidly without this protection.
	 *
	 *      The factor is made adaptive: it scales with pRatioConfidence so that a
	 *      clearly-BACK measurement (p_ratio well below threshold, high confidence)
	 *      sinks at near-full alpha, while a borderline measurement (p_ratio barely
	 *      below threshold) sinks at kBackSinkFactor × alpha. Formula:
	 *        adaptiveBackSink = kBackSinkFactor + (1 - kBackSinkFactor) × pRatioConf
	 *      At pRatioConfidence=1.0 → factor=1.0 (no damping, fastest sink)
	 *      At pRatioConfidence=0.0 → factor=kBackSinkFactor (maximum damping)
	 *      This preserves noise protection for brief threshold crossings while
	 *      enabling fast BACK confirmation when signal is unambiguously BACK.
	 *
	 *    Transitions in the opposite direction (FRONT maintaining, BACK maintaining)
	 *    run at full alpha so they respond immediately to persistent signals.
	 *
	 *    p_ratio_confidence_factor scales between 0.5 and 1.0; measurements near the
	 *    decision boundary move the score half as much as highly confident ones. */
	const float adaptiveBackSinkFactor =
		kBackSinkFactor + (1.0f - kBackSinkFactor) * pRatioConfidence;

	/* Suspicious p_ratio jump detection (body-motion spike guard).
	 *
	 * Body motion causes p_ratio to jump >2x in a single 86ms tick (e.g. 86k→224k).
	 * Genuine phone approaches ramp slowly (<1.5x/tick). When a suspicious jump is
	 * detected while in BACK state (rawFront=true), start a cooldown during which the
	 * BACK→FRONT climb uses kReFrontClimbFactor regardless of mHasBeenFront, blocking
	 * the false FRONT that would otherwise follow the spike within 1–2 seconds.
	 *
	 * Detection condition:
	 *   - current p_ratio / previous p_ratio > kJumpRatioThreshold (default 2.0)
	 *   - both values must be above threshold (previous was a genuine detection)
	 *   - current state is BACK (only guard re-entry, not FRONT maintenance) */
	const float prevPRatio = sess.mPrevPRatio;
	const bool suspiciousJump =
		rawFront && !sess.lastResult.mSideIsFront && (prevPRatio > pRatioThreshold) &&
		(results.p_ratio > kJumpRatioThreshold * prevPRatio);
	if (suspiciousJump) {
		sess.mJumpCooldown = kJumpCooldownTicks;
		LOG_DBG("sess%u suspicious p_ratio jump: %.4f → %.4f (%.1fx), cooldown=%u ticks",
			sessionIdx, static_cast<double>(prevPRatio),
			static_cast<double>(results.p_ratio),
			static_cast<double>(results.p_ratio / prevPRatio), kJumpCooldownTicks);
	} else if (sess.mJumpCooldown > 0) {
		sess.mJumpCooldown--;
	}
	sess.mPrevPRatio = results.p_ratio;

	/* First approach: use FRONT_CLIMB_FACTOR (fast).
	 * Re-entry after confirmed BACK, OR during jump cooldown: use RE_FRONT_CLIMB_FACTOR. */
	const bool useReFrontFactor = sess.mHasBeenFront || (sess.mJumpCooldown > 0);
	const float climbFactor = useReFrontFactor ? kReFrontClimbFactor : kFrontClimbFactor;
	const float asymmetricFactor =
		(rawFront && !sess.lastResult.mSideIsFront)  ? climbFactor :
		(!rawFront && sess.lastResult.mSideIsFront)  ? adaptiveBackSinkFactor :
							       1.0f;
	const float adaptiveAlpha = kEwmaAlpha * (0.5f + 0.5f * pRatioConfidence) * cirDampFactor *
				    asymmetricFactor;
	const float target = rawFront ? 1.0f : 0.0f;
	sess.mFrontScore += adaptiveAlpha * (target - sess.mFrontScore);
	sess.mFrontScore = std::clamp(sess.mFrontScore, 0.0f, 1.0f);

	/* Confidence ceiling: when the library outputs rawFront=true but pRatioConfidence
	 * is below kFrontConfidenceRequired, the decision is borderline — p_ratio barely
	 * exceeds the threshold. A sustained stream of such uncertain ticks can slowly push
	 * the score past kFrontThresh even though no individual tick carries strong evidence.
	 * To prevent that, cap the score at kLowConfidenceScoreCap (< kFrontThresh) whenever
	 * a rawFront=true tick arrives with insufficient confidence.
	 *
	 * The cap is lifted the moment confidence rises above kFrontConfidenceRequired, at
	 * which point the score can grow freely from its pre-charged value. This means a
	 * genuine approach — where p_ratio first rises slowly then crosses the confidence
	 * threshold — reaches FRONT quickly from the pre-charged state, while a sustained
	 * borderline signal (e.g. radar body-detection artefact) is blocked permanently. */
	const bool cappedByLowConfidence = rawFront && (pRatioConfidence < kFrontConfidenceRequired);
	if (cappedByLowConfidence) {
		sess.mFrontScore = std::min(sess.mFrontScore, kLowConfidenceScoreCap);
	}

	/* BLE RSSI veto: physics-based discriminator independent of body-motion.
	 *
	 * BLE signals are attenuated 15-25 dB by doors; UWB is far less affected.
	 * When the phone moves behind the door, BLE RSSI drops measurably while the
	 * user's body in front of the radar keeps p_ratio high (causing false FRONT).
	 *
	 * Condition: currently in BACK state, p_ratio says rawFront=true, but BLE RSSI
	 * has fallen more than kBleRssiDropDb below the reference captured at last FRONT
	 * confirmation → cap score at kBleRssiScoreCap so the FRONT threshold cannot
	 * be reached regardless of p_ratio alone.
	 *
	 * The veto is only active during BACK→FRONT climbing. Once FRONT is confirmed,
	 * normal hysteresis keeps the state (reference was captured at that point).
	 * The cap is lifted automatically as soon as RSSI recovers above the threshold. */
	const bool bleVetoed = sess.mBleRefSet && sess.mBleRssiValid && rawFront &&
			       !sess.lastResult.mSideIsFront &&
			       (sess.mRefBleRssi - sess.mBleRssiEwma >= kBleRssiDropDb);
	if (bleVetoed) {
		sess.mFrontScore = std::min(sess.mFrontScore, kBleRssiScoreCap);
	}

	/* 4. Hysteresis decision from score. */
	if (sess.lastResult.mSideIsFront) {
		out.mSideIsFront = (sess.mFrontScore >= kBackThresh);
	} else {
		out.mSideIsFront = (sess.mFrontScore >= kFrontThresh);
	}

	out.mUnlockAllowed = out.mSideIsFront && (unlockAllowed != 0);

	if (out.mSideIsFront) {
		sess.mHasBeenFront = true;
		/* Capture BLE RSSI reference the first time FRONT is confirmed so that
		 * subsequent BACK→FRONT re-entries can be compared against it. */
		if (!sess.mBleRefSet && sess.mBleRssiValid) {
			sess.mRefBleRssi = sess.mBleRssiEwma;
			sess.mBleRefSet = true;
			LOG_DBG("sess%u BLE RSSI reference captured: %.1f dBm", sessionIdx,
				static_cast<double>(sess.mRefBleRssi));
		}
	}

	const int32_t pRatioU6 = static_cast<int32_t>(results.p_ratio * 1000000.0f);
	const int32_t meanPdoaMilliDeg = static_cast<int32_t>(results.mean_pdoa * 1000.0f);
	const int32_t scorePermille = static_cast<int32_t>(sess.mFrontScore * 1000.0f);
	const int32_t alphaPermille = static_cast<int32_t>(adaptiveAlpha * 1000.0f);
	const bool reFrontClimb = rawFront && !sess.lastResult.mSideIsFront && useReFrontFactor;
	const int32_t bleRssiInt = sess.mBleRssiValid ? static_cast<int32_t>(sess.mBleRssiEwma) : 0;
	LOG_DBG("sess%u %s pratio=%d pdoa=%d cir=%d blk=%d dist=%ucm score=%d/1000 alpha=%d/1000 ble=%ddBm%s%s%s%s",
		sessionIdx, out.mSideIsFront ? "FRONT" : "BACK ", pRatioU6, meanPdoaMilliDeg, results.CIR,
		results.noise_blocks, results.distance_cm, scorePermille, alphaPermille, bleRssiInt,
		cappedByLowConfidence ? " [cap]" : "",
		bleVetoed ? " [ble-veto]" : "",
		reFrontClimb ? " [re]" : "",
		suspiciousJump ? " [jump]" : "");

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
