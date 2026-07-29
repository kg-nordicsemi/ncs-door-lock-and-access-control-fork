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
	constexpr float kCirDampStrength{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_CIR_DAMP_STRENGTH / 1000.0f };
	constexpr float kFrontClimbFactor{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_FRONT_CLIMB_FACTOR / 1000.0f };
	constexpr float kReFrontClimbFactor{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_RE_FRONT_CLIMB_FACTOR /
					     1000.0f };
	constexpr float kBackSinkFactor{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_BACK_SINK_FACTOR / 1000.0f };
	constexpr float kJumpRatioThreshold{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_JUMP_RATIO_THRESHOLD / 100.0f };
	constexpr uint8_t kJumpCooldownTicks{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_JUMP_COOLDOWN_TICKS };
	constexpr float kFrontConfidenceRequired{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_FRONT_CONFIDENCE_REQUIRED /
						  1000.0f };
	constexpr float kLowConfidenceScoreCap{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_LOW_CONFIDENCE_SCORE_CAP /
						1000.0f };
	constexpr float kUwbRslDropDb{ static_cast<float>(CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_UWB_RSL_DROP_DB) };
	constexpr float kUwbRslMaxFrontDb{ static_cast<float>(
		CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_UWB_RSL_MAX_FRONT_DB) };
	constexpr float kUwbRslProtectFactor{ CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_UWB_RSL_PROTECT_FACTOR /
					      1000.0f };

	static_assert(kFrontThresh > kBackThresh, "FRONT threshold must be above BACK threshold (hysteresis gap)");
	static_assert(kEwmaAlpha > 0.0f && kEwmaAlpha <= 1.0f, "EWMA alpha must be in (0, 1]");
	static_assert(kCirDampStrength >= 0.0f && kCirDampStrength <= 1.0f, "CIR damp strength must be in [0, 1]");
	static_assert(kFrontClimbFactor > 0.0f && kFrontClimbFactor <= 1.0f, "FRONT climb factor must be in (0, 1]");
	static_assert(kReFrontClimbFactor > 0.0f && kReFrontClimbFactor <= 1.0f,
		      "RE_FRONT climb factor must be in (0, 1]");
	static_assert(kBackSinkFactor > 0.0f && kBackSinkFactor <= 1.0f, "BACK sink factor must be in (0, 1]");
	static_assert(kJumpRatioThreshold > 1.0f, "Jump ratio threshold must be > 1.0 (ratio of new/old p_ratio)");
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
	const float pRatioConfidence =
		(results.p_ratio == 0.0f) ? 0.0f :
					    std::min(std::fabs(results.p_ratio - pRatioThreshold) / pRatioNorm, 1.0f);

	/* 2. CIR dampening: high CIR → radar saturation / multipath → p_ratio less reliable.
	 *    cirFraction is 0 at CIR=0 and 1 at CIR=max_cir_threshold.
	 *    cirDampFactor drops from 1.0 (reliable) towards (1 - kCirDampStrength) (saturated),
	 *    so alpha shrinks and the score moves less for each saturated tick. */
	const float cirMax = static_cast<float>(mDisambiguationParams.max_cir_threshold);
	const float cirFraction = (cirMax > 0.0f) ? std::min(static_cast<float>(results.CIR) / cirMax, 1.0f) : 0.0f;
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
	const float adaptiveBackSinkFactor = kBackSinkFactor + (1.0f - kBackSinkFactor) * pRatioConfidence;

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
	const bool suspiciousJump = rawFront && !sess.lastResult.mSideIsFront && (prevPRatio > pRatioThreshold) &&
				    (results.p_ratio > kJumpRatioThreshold * prevPRatio);
	if (suspiciousJump) {
		sess.mJumpCooldown = kJumpCooldownTicks;
		LOG_DBG("sess%u suspicious p_ratio jump: %.4f → %.4f (%.1fx), cooldown=%u ticks", sessionIdx,
			static_cast<double>(prevPRatio), static_cast<double>(results.p_ratio),
			static_cast<double>(results.p_ratio / prevPRatio), kJumpCooldownTicks);
	} else if (sess.mJumpCooldown > 0) {
		sess.mJumpCooldown--;
	}
	sess.mPrevPRatio = results.p_ratio;

	/* UWB RSL veto — bidirectional phone-position discriminator.
	 *
	 * rsl_q8 from the ranging diagnostic measures UWB signal level from the PHONE itself
	 * (not body reflections). When the phone moves behind the door, rsl_q8 INCREASES
	 * (signal weakens). A positive drop = (current − reference) ≥ kUwbRslDropDb means
	 * the phone is behind the door.
	 *
	 * When vetoed, we set effectiveRawFront=false regardless of radar output. This:
	 *   • FRONT→BACK: body motion no longer sustains FRONT — score sinks via
	 *     adaptiveBackSinkFactor until it falls below BACK threshold.
	 *   • BACK→FRONT: score cannot climb past FRONT threshold.
	 *
	 * The veto is intentionally bidirectional — NO !mSideIsFront guard — because the
	 * previous one-directional veto failed when the score was stuck in the hysteresis
	 * zone (kBackThresh < score < kFrontThresh) after phone moved behind door. */
	const bool uwbRslVetoed = sess.mUwbRefSet && sess.mUwbRslValid &&
				  (sess.mUwbRslEwma - sess.mRefUwbRsl >= kUwbRslDropDb);
	const bool effectiveRawFront = rawFront && !uwbRslVetoed;

	/* Climb-factor selection for BACK→FRONT transitions (effectiveRawFront=true, score below FRONT).
	 *
	 * Three tiers:
	 *
	 * 1. kUwbRslProtectFactor (slowest) — first approach AND UWB RSL indicates phone is
	 *    NOT in front (signal too weak: mUwbRslEwma > kUwbRslMaxFrontDb).
	 *    The reference-based RSL veto is inactive when mHasBeenFront=false (no reference),
	 *    so we use a slow climb rate to prevent body-motion from producing a false first-FRONT
	 *    when the phone is behind the door.  Once the phone genuinely approaches, RSL drops
	 *    below the threshold and normal FRONT_CLIMB_FACTOR takes over.
	 *
	 * 2. kReFrontClimbFactor (fast) — re-entry after FRONT was previously confirmed
	 *    (mHasBeenFront=true) OR during jump cooldown.  At this point mUwbRefSet=true and
	 *    the bidirectional UWB RSL veto provides the false-positive protection,
	 *    so a fast climb rate is safe.
	 *
	 * 3. kFrontClimbFactor (medium) — first approach, RSL confirms phone is in front
	 *    (mUwbRslEwma ≤ kUwbRslMaxFrontDb) or no RSL data yet.  Normal first-detection speed. */
	const bool uwbRslTooWeakForFront = sess.mUwbRslValid && (sess.mUwbRslEwma > kUwbRslMaxFrontDb);
	const bool useReFrontFactor = sess.mHasBeenFront || (sess.mJumpCooldown > 0);
	const bool useRslProtectFactor = uwbRslTooWeakForFront && !sess.mHasBeenFront;
	const float climbFactor = useRslProtectFactor ? kUwbRslProtectFactor :
				  useReFrontFactor    ? kReFrontClimbFactor :
							kFrontClimbFactor;
	const float asymmetricFactor = (effectiveRawFront && !sess.lastResult.mSideIsFront) ? climbFactor :
				       (!effectiveRawFront && sess.lastResult.mSideIsFront) ? adaptiveBackSinkFactor :
											      1.0f;
	const float adaptiveAlpha = kEwmaAlpha * (0.5f + 0.5f * pRatioConfidence) * cirDampFactor * asymmetricFactor;
	const float target = effectiveRawFront ? 1.0f : 0.0f;
	sess.mFrontScore += adaptiveAlpha * (target - sess.mFrontScore);
	sess.mFrontScore = std::clamp(sess.mFrontScore, 0.0f, 1.0f);

	/* Confidence ceiling: when effectiveRawFront=true but pRatioConfidence is below
	 * kFrontConfidenceRequired, the decision is borderline. Cap the score at
	 * kLowConfidenceScoreCap (< kFrontThresh) to prevent sustained borderline signals
	 * (e.g. radar body-detection artefact) from crossing the FRONT threshold. */
	const bool cappedByLowConfidence = effectiveRawFront && (pRatioConfidence < kFrontConfidenceRequired);
	if (cappedByLowConfidence) {
		sess.mFrontScore = std::min(sess.mFrontScore, kLowConfidenceScoreCap);
	}

	/* UWB RSL hard gate — first approach only.
	 *
	 * The slow-climb factor (kUwbRslProtectFactor) alone is insufficient: given enough
	 * time (>10 s of continuous body motion), the score can still reach the FRONT
	 * threshold.  When the session has never confirmed FRONT (mHasBeenFront=false) and
	 * UWB RSL shows the phone is NOT in front (mUwbRslEwma > kUwbRslMaxFrontDb), cap
	 * the score hard at kLowConfidenceScoreCap (default 500 < FRONT threshold 650).
	 *
	 * This makes it physically impossible to reach FRONT on first approach unless the
	 * UWB signal from the phone is strong enough to indicate it is genuinely in front.
	 * Once mHasBeenFront=true, the bidirectional RSL veto takes over instead. */
	const bool cappedByRslGate = effectiveRawFront && !sess.mHasBeenFront && uwbRslTooWeakForFront;
	if (cappedByRslGate) {
		sess.mFrontScore = std::min(sess.mFrontScore, kLowConfidenceScoreCap);
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

		/* UWB RSL reference: continuously track in-front RSL while FRONT is confirmed.
		 *
		 * A fixed first-FRONT reference is unreliable: if FRONT was falsely triggered
		 * (e.g. phone far away or behind door), the reference would be wrong for the
		 * rest of the session, making the veto ineffective or inverting it.
		 *
		 * Instead we use a slow EWMA (α=0.03, τ≈33 ticks ≈ 2.4 s) so that:
		 *  • After a few seconds of genuine FRONT, reference converges to the actual
		 *    in-front UWB RSL.
		 *  • Brief BACK flickers cannot corrupt the reference (tracking only happens
		 *    when the score is confirmed FRONT, not during BACK).
		 *  • When phone moves behind door (sustained BACK), the reference stays frozen
		 *    at the last genuine in-front RSL, giving a stable comparison point. */
		constexpr float kUwbRefAlpha{ 0.03f };
		if (sess.mUwbRslValid) {
			if (!sess.mUwbRefSet) {
				sess.mRefUwbRsl = sess.mUwbRslEwma;
				sess.mUwbRefSet = true;
				LOG_DBG("sess%u UWB RSL reference init: %.1f dBm (=-%ddBm)", sessionIdx,
					static_cast<double>(sess.mRefUwbRsl), static_cast<int>(sess.mRefUwbRsl));
			} else {
				sess.mRefUwbRsl += kUwbRefAlpha * (sess.mUwbRslEwma - sess.mRefUwbRsl);
			}
		}
	}

	const int32_t pRatioU6 = static_cast<int32_t>(results.p_ratio * 1000000.0f);
	const int32_t meanPdoaMilliDeg = static_cast<int32_t>(results.mean_pdoa * 1000.0f);
	const int32_t scorePermille = static_cast<int32_t>(sess.mFrontScore * 1000.0f);
	const int32_t alphaPermille = static_cast<int32_t>(adaptiveAlpha * 1000.0f);
	const bool reFrontClimb = effectiveRawFront && !sess.lastResult.mSideIsFront && useReFrontFactor;
	/* Log BLE RSSI (0 if not yet received).
	 * UWB RSL displayed as negative dBm (rsl_q8 is "absolute value", so negate for display).
	 * e.g. uwb=-67dBm = strong (phone in front), uwb=-80dBm = weak (phone behind door).
	 * uwb-drop shows (current - ref) in dBm: positive = phone moved away/behind door. */
	const int32_t uwbRslNeg = sess.mUwbRslValid ? -static_cast<int32_t>(sess.mUwbRslEwma) : 0;
	/* uwbDrop > 0 = phone further/behind door vs reference (higher absolute dBm = weaker) */
	const int32_t uwbDropDb =
		(sess.mUwbRefSet && sess.mUwbRslValid) ? static_cast<int32_t>(sess.mUwbRslEwma - sess.mRefUwbRsl) : 0;
	LOG_DBG("sess%u %s pratio=%d cir=%d blk=%d dist=%ucm score=%d/1000 alpha=%d/1000 uwb=%ddBm drop=%ddB%s%s%s%s%s%s",
		sessionIdx, out.mSideIsFront ? "FRONT" : "BACK ", pRatioU6, results.CIR, results.noise_blocks,
		results.distance_cm, scorePermille, alphaPermille, uwbRslNeg, uwbDropDb,
		cappedByLowConfidence ? " [cap]" : "",
		uwbRslVetoed ? " [uwb-veto]" : "", reFrontClimb ? " [re]" : "",
		useRslProtectFactor ? " [rsl-prot]" : "", cappedByRslGate ? " [rsl-gate]" : "",
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
