/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "front_back_detection.h"

#include <algorithm>
#include <disambiguator.h>
#include <uwb_utils.h>

#include <aliro_uwb_adapter/aliro_uwb_adapter.h>
#include <aliro_uwb_adapter/aliro_uwb_session.h>

#include <cherry/cherry_ccc.h>
#include <cherry/cherry_common.h>

#include <doorlock/utils/mutex_guard.h>
#include <doorlock/utils/utils.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/slist.h>

#include <errno.h>

LOG_MODULE_REGISTER(FrontBackDetection, CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_FRONT_BACK_DETECTION_LOG_LEVEL);

namespace {

constexpr uint16_t kUwbMaximumReportableDistanceCm{ 500 };

constexpr disambiguation_parameters kFrontBackDetectionParams = {
	.max_cir_threshold = CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_MAX_CIR,
	.p_ratio_threshold = CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_P_RATIO / 10000.0f,
	.nb_blocks_threshold = CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_NB_BLOCKS,
	.radar_distance = 0,
	.noise_threshold = CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_NOISE_THRESHOLD,
	.shadow_noise_threshold = CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_SHADOW_THRESHOLD,
	.blind_distance = CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_BLIND_DISTANCE_CM,
	.secure_bubble_radius = CONFIG_DOOR_LOCK_ALIRO_UWB_DISAMBIGUATION_SECURE_BUBBLE_CM,
};

void LogFrontBackDetectionResult(uint8_t sessionIdx, size_t activeRangingSessions,
				 const Aliro::Uwb::Disambiguation::Result &result) noexcept
{
	using namespace Aliro::Uwb::Utils;

	/* pratio_u6 = p_ratio * 1e6 — avoids %f. Threshold 0.6 → 600000. */
	const int32_t pRatioU6 = static_cast<int32_t>(result.mPRatio * 1000000.0f);
	const int32_t pdoaMilliDeg = static_cast<int32_t>(result.mMeanPdoaDeg * 1000.0f);
	const auto pdoa = SplitMilli(pdoaMilliDeg);

	const char *rawStr = result.IsFront() ? "FRONT" : "BACK ";

#ifdef CONFIG_DOOR_LOCK_ALIRO_UWB_RANGING_SESSION_LOG
	LOG_INF("[sess:%u|total:%zu] [side] raw:%s | dist:%3dcm | pratio_u6:%7d | cir:%4d | blk:%2d | pdoa:%s%u.%03u",
		sessionIdx, activeRangingSessions, rawStr, result.mDistanceCm, pRatioU6, result.mCir,
		result.mNoiseBlocks, pdoa.mSign, pdoa.mInteger, pdoa.mFraction);
#else
	ARG_UNUSED(sessionIdx);
	ARG_UNUSED(activeRangingSessions);
	LOG_INF("[side] raw:%s | dist:%3dcm | pratio_u6:%7d | cir:%4d | blk:%2d | pdoa:%s%u.%03u", rawStr,
		result.mDistanceCm, pRatioU6, result.mCir, result.mNoiseBlocks, pdoa.mSign, pdoa.mInteger,
		pdoa.mFraction);
#endif
}

/* Minimum interval between BLE RSSI reads per session (ms).
 * HCI READ_RSSI is a blocking round-trip; ~2 Hz is more than sufficient. */
constexpr int64_t kBleRssiReadIntervalMs{ 500 };

/* Per-session timestamp of the last BLE RSSI HCI read. */
int64_t sBleRssiLastReadMs[CONFIG_DOOR_LOCK_BLE_UWB_MAX_SESSIONS]{};

/* Reads BLE RSSI for a connection via the HCI Read RSSI command.
 * Safe to call from the system workqueue (CONFIG_BT_RECV_WORKQ_BT=1 means BT RX
 * uses a dedicated BT workqueue, so bt_hci_cmd_send_sync() will not deadlock here).
 * Returns the RSSI in dBm, or BT_HCI_LE_RSSI_NOT_AVAILABLE (0x7F) on failure. */
int8_t ReadBleRssiHci(bt_conn *conn)
{
	uint16_t handle;
	int err = bt_hci_get_conn_handle(conn, &handle);
	if (err != 0) {
		LOG_DBG("bt_hci_get_conn_handle failed: %d", err);
		return BT_HCI_LE_RSSI_NOT_AVAILABLE;
	}

	/* Use K_FOREVER: the HCI round-trip is ~1-5 ms; on the system workqueue this is
	 * acceptable since BT uses its own dedicated bt_work_q workqueue. */
	net_buf *buf = bt_hci_cmd_alloc(K_FOREVER);
	if (!buf) {
		LOG_ERR("BLE RSSI: bt_hci_cmd_alloc returned NULL");
		return BT_HCI_LE_RSSI_NOT_AVAILABLE;
	}

	auto *cp = static_cast<bt_hci_cp_read_rssi *>(net_buf_add(buf, sizeof(bt_hci_cp_read_rssi)));
	cp->handle = sys_cpu_to_le16(handle);

	net_buf *rsp = nullptr;
	err = bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp);
	if (err != 0) {
		LOG_DBG("bt_hci_cmd_send_sync(READ_RSSI) failed: %d", err);
		return BT_HCI_LE_RSSI_NOT_AVAILABLE;
	}
	if (!rsp) {
		LOG_DBG("bt_hci_cmd_send_sync(READ_RSSI) returned null response");
		return BT_HCI_LE_RSSI_NOT_AVAILABLE;
	}

	const auto *rp = static_cast<const bt_hci_rp_read_rssi *>(static_cast<const void *>(rsp->data));
	const int8_t rssi = (rp->status == 0) ? rp->rssi : INT8_C(BT_HCI_LE_RSSI_NOT_AVAILABLE);
	if (rp->status != 0) {
		LOG_DBG("READ_RSSI HCI status error: 0x%02x", rp->status);
	}
	net_buf_unref(rsp);
	return rssi;
}

} // namespace

namespace Aliro::Uwb {

int FrontBackDetection::Init(sys_slist_t *activeSessions, k_mutex *sessionsMutex)
{
	VerifyOrReturnValue(activeSessions && sessionsMutex, -EINVAL);

	mActiveSessions = activeSessions;
	mSessionsMutex = sessionsMutex;
	mProcessingEnabled = false;

	int err = Disambiguation::Disambiguator::Instance().Init(kFrontBackDetectionParams);
	VerifyOrReturnValue(err == 0, err, LOG_ERR("Failed to initialize disambiguation: %d", err));

	LOG_INF("Front/back detection initialized");

	k_work_init_delayable(&mProcessWork.mDwork, ProcessWorkHandler);
	mProcessWork.mOwner = this;

	SessionEventHub::Register(mSubscriber);

	return 0;
}

void FrontBackDetection::HandleRadarMeasurement(const uint8_t *data, size_t size)
{
	VerifyOrReturn(data && size > 0);
	Disambiguation::Disambiguator::Instance().AddCirMeasurement(const_cast<uint8_t *>(data),
								    static_cast<uint16_t>(size));
}

int FrontBackDetection::AssignSessionIndex(SessionContext &sessionCtx, DoorLock::Utils::MutexGuard &)
{
	for (uint8_t i = 0; i < mSessions.size(); i++) {
		if (!mSessions[i]) {
			mSessions[i] = &sessionCtx;
			sessionCtx.mDisambiguationSessionIdx = i;
			return 0;
		}
	}

	return -ENOSPC;
}

void FrontBackDetection::ReleaseSessionIndex(const SessionContext &sessionCtx, DoorLock::Utils::MutexGuard &)
{
	VerifyOrReturn(sessionCtx.mDisambiguationSessionIdx < mSessions.size());

	// Flush the shared CIR counter only when this is the last active session.
	const bool isLastSession =
		std::none_of(mSessions.begin(), mSessions.end(),
			     [&sessionCtx](const SessionContext *s) { return s != nullptr && s != &sessionCtx; });

	Disambiguation::Disambiguator::Instance().ResetSession(sessionCtx.mDisambiguationSessionIdx);
	if (isLastSession) {
		Disambiguation::Disambiguator::Instance().FlushCir();
	}
	mSessions[sessionCtx.mDisambiguationSessionIdx] = nullptr;
}

void FrontBackDetection::CancelProcessing()
{
	mProcessingEnabled = false;
	k_work_sync sync;
	(void)k_work_cancel_delayable_sync(&mProcessWork.mDwork, &sync);
}

void FrontBackDetection::ProcessWorkHandler(k_work *work)
{
	auto *dwork = k_work_delayable_from_work(work);
	auto *processWork = CONTAINER_OF(dwork, ProcessWork, mDwork);

	VerifyOrReturn(processWork->mOwner && processWork->mOwner->mActiveSessions);
	processWork->mOwner->ProcessSessions(processWork->mOwner->mActiveSessions);
}

void FrontBackDetection::ProcessSessions(sys_slist_t *activeSessions)
{
	if (!mProcessingEnabled || !activeSessions || !mSessionsMutex) {
		return;
	}

	Disambiguation::Result result{};

	/* BLE sessions whose RSSI we want to read after releasing the mutex.
	 * HCI READ_RSSI is a synchronous blocking call — must NOT be called under
	 * the sessions mutex (risk of deadlock with BT stack callbacks). */
	struct BleSessionEntry {
		bt_conn *conn;
		uint8_t sessionIdx;
	};
	BleSessionEntry bleSessions[CONFIG_DOOR_LOCK_BLE_UWB_MAX_SESSIONS]{};
	uint8_t bleSessionCount{ 0 };

	SessionContext *sessionCtx{};
	{
		DoorLock::Utils::MutexGuard lock{ *mSessionsMutex };

		const size_t activeRangingSessions = CountActiveRangingSessions(activeSessions);

		SYS_SLIST_FOR_EACH_CONTAINER (activeSessions, sessionCtx, mSessionContextNode) {
			if (sessionCtx->mRangingSessionState != RangingSessionState::Ranging &&
			    sessionCtx->mRangingSessionState != RangingSessionState::RangingResumed) {
				continue;
			}

			if (Disambiguation::Disambiguator::Instance().Process(
				    result, sessionCtx->mDisambiguationSessionIdx) == 0) {
#if defined(CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_FRONT_BACK_DETECTION) && defined(ALIRO_VENDOR1_EXTENSION)
				Vendor1Ext::ProcessDisambiguationResult(sessionCtx, result);
#endif
				LogFrontBackDetectionResult(sessionCtx->mDisambiguationSessionIdx,
							    activeRangingSessions, result);
			}

			/* Collect BLE sessions for post-mutex RSSI reading. */
			if (sessionCtx->mSessionContextData.IsBle() &&
			    bleSessionCount < ARRAY_SIZE(bleSessions)) {
				bt_conn *conn = sessionCtx->mSessionContextData.GetBtConn();
				/* bt_conn_ref keeps the connection alive until we release it below. */
				bt_conn_ref(conn);
				bleSessions[bleSessionCount++] = { conn,
								   sessionCtx->mDisambiguationSessionIdx };
			}
		}
	} /* sessions mutex released */

	/* Phase 2: read BLE RSSI outside the mutex and feed samples to the disambiguator.
	 * Throttled to kBleRssiReadIntervalMs to avoid flooding HCI. */
	const int64_t nowMs = k_uptime_get();
	for (uint8_t i = 0; i < bleSessionCount; i++) {
		const uint8_t idx = bleSessions[i].sessionIdx;
		if (nowMs - sBleRssiLastReadMs[idx] >= kBleRssiReadIntervalMs) {
			const int8_t rssi = ReadBleRssiHci(bleSessions[i].conn);
			if (rssi != static_cast<int8_t>(BT_HCI_LE_RSSI_NOT_AVAILABLE)) {
				LOG_DBG("sess%u BLE RSSI read: %d dBm", idx, rssi);
				Disambiguation::Disambiguator::Instance().AddBleRssiMeasurement(rssi, idx);
			}
			sBleRssiLastReadMs[idx] = nowMs;
		}
		bt_conn_unref(bleSessions[i].conn);
	}

	ScheduleProcessing();
}

void FrontBackDetection::ScheduleProcessing()
{
	mProcessingEnabled = true;
	(void)k_work_schedule(&mProcessWork.mDwork,
			      K_MSEC(CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_FRONT_BACK_DETECTION_PROCESSING_INTERVAL_MS));
}

void FrontBackDetection::OnSessionEvent(const aliro_uwb_session_event &event, const SessionContext &sessionCtx,
					void *ctx)
{
	VerifyOrReturn(ctx);

	static_cast<FrontBackDetection *>(ctx)->HandleSessionEvent(event, sessionCtx);
}

void FrontBackDetection::ResetSession(const SessionContext &sessionCtx)
{
	Disambiguation::Disambiguator::Instance().ResetSession(sessionCtx.mDisambiguationSessionIdx);
#if defined(CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_FRONT_BACK_DETECTION) && defined(ALIRO_VENDOR1_EXTENSION)
	Vendor1Ext::ResetSession(sessionCtx);
#endif
}

void FrontBackDetection::HandleSessionEvent(const aliro_uwb_session_event &event, const SessionContext &sessionCtx)
{
	switch (event.type) {
	case ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_STATUS: {
		const auto *status = event.data.status;
		const auto oldState = sessionCtx.mSessionState;
		const auto newState = status->session_state;

		if (oldState == CHERRY_CCC_SESSION_STATE_IDLE && newState == CHERRY_CCC_SESSION_STATE_ACTIVE) {
			if (sessionCtx.mRangingSessionState == RangingSessionState::Idle) {
				ResetSession(sessionCtx);
				LOG_INF("Front/back detection reset for new ranging session");
				ScheduleProcessing();
			} else if (sessionCtx.mRangingSessionState == RangingSessionState::RangingSuspended) {
				ResetSession(sessionCtx);
				LOG_INF("Front/back detection reset on ranging session resume");
				ScheduleProcessing();
			}
		} else if (oldState == CHERRY_CCC_SESSION_STATE_ACTIVE && newState == CHERRY_CCC_SESSION_STATE_IDLE) {
			// Stop processing only when the last session goes idle.
			const bool hasOtherSessions =
				std::any_of(mSessions.begin(), mSessions.end(), [&sessionCtx](const SessionContext *s) {
					return s != nullptr && s != &sessionCtx;
				});
			if (!hasOtherSessions) {
				CancelProcessing();
			}
		}
		break;
	}
	case ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_CONTROLEE_REPORT: {
		const auto *report = event.data.controlee_report;
		if (report) {
			HandleControleeReport(sessionCtx, report->measurements);
		}
		break;
	}
	case ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_DIAGNOSTIC_REPORT:
		HandleDiagnosticReport(sessionCtx, event.data.diagnostics);
		break;
	default:
		break;
	}
}

void FrontBackDetection::HandleControleeReport(const SessionContext &sessionCtx,
					       const cherry_ccc_session_controlee_measurements *measurements)
{
	for (const auto *m = measurements; m; m = m->next) {
		if (m->frame_status) {
			Disambiguation::Disambiguator::Instance().AddDistanceMeasurement(
				0, sessionCtx.mDisambiguationSessionIdx, true);
			sessionCtx.mRangingRoundHadError = true;
			continue;
		}

		if (m->distance_cm > kUwbMaximumReportableDistanceCm) {
			LOG_INF("Ignoring measurements above maximum reportable distance");
			Disambiguation::Disambiguator::Instance().AddDistanceMeasurement(
				0, sessionCtx.mDisambiguationSessionIdx, true);
			sessionCtx.mRangingRoundHadError = true;
			break;
		}

		sessionCtx.mRangingRoundHadError = false;
		Disambiguation::Disambiguator::Instance().AddDistanceMeasurement(
			m->distance_cm, sessionCtx.mDisambiguationSessionIdx, false);

#if defined(CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_FRONT_BACK_DETECTION) && defined(ALIRO_VENDOR1_EXTENSION)
		Vendor1Ext::ProcessMeasurement(sessionCtx, *m);
#endif

		if (!mProcessingEnabled) {
			ScheduleProcessing();
		}
	}
}

void FrontBackDetection::HandleDiagnosticReport(const SessionContext &sessionCtx,
						const cherry_common_diag_report *diagnostics)
{
	// Aliro CCC with 1 ranging round produces 2 frame reports; PDOA is in frame[1]
	// (controlee reply). The Qorvo demo uses 5 rounds and reads frame[4].
	if (!diagnostics || diagnostics->n_frame_report < 2 || sessionCtx.mRangingRoundHadError) {
		sessionCtx.mRangingRoundHadError = false;
		Disambiguation::Disambiguator::Instance().AddPdoaMeasurement(0, 0, sessionCtx.mDisambiguationSessionIdx,
									     true, true);
		return;
	}

	const auto &frame = diagnostics->frame_report[1];

	int16_t pdoa = 0;
	int16_t rssi = 0;
	bool pdoaError = true;
	bool rssiError = true;

	if (frame.aoas && frame.n_aoa > 0) {
		const auto &aoaMeas = frame.aoas[0];
		if (aoaMeas.fom > 0) {
			pdoa = aoaMeas.pdoa;
			pdoaError = false;
		} else {
			LOG_ERR("PDOA fom=0 (invalid), skipping measurement");
		}
	}

	if (frame.seg_metrics && frame.n_seg_metrics > 0) {
		rssi = frame.seg_metrics[0].rsl_q8;
		rssiError = false;
	}

	Disambiguation::Disambiguator::Instance().AddPdoaMeasurement(pdoa, rssi, sessionCtx.mDisambiguationSessionIdx,
								     pdoaError, rssiError);
}

size_t FrontBackDetection::CountActiveRangingSessions(sys_slist_t *activeSessions) const
{
#ifdef CONFIG_DOOR_LOCK_ALIRO_UWB_RANGING_SESSION_LOG
	size_t count = 0;
	SessionContext *sessionCtx{};
	SYS_SLIST_FOR_EACH_CONTAINER (activeSessions, sessionCtx, mSessionContextNode) {
		if (sessionCtx->mRangingSessionState == RangingSessionState::Ranging ||
		    sessionCtx->mRangingSessionState == RangingSessionState::RangingResumed) {
			count++;
		}
	}
	return count;
#else
	ARG_UNUSED(activeSessions);
	return 0;
#endif
}

} // namespace Aliro::Uwb
