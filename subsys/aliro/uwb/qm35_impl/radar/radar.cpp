/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "radar.h"

#include <doorlock/utils/mutex_guard.h>
#include <doorlock/utils/utils.h>

#include <aliro/types.h>

#include <aliro_uwb_adapter/aliro_uwb_adapter.h>
#include <aliro_uwb_adapter/aliro_uwb_session.h>

#include <cherry/cherry.h>
#include <cherry/cherry_radar.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <tuple>

LOG_MODULE_REGISTER(UwbRadar, CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_RADAR_LOG_LEVEL);

namespace {

constexpr uint32_t kRadarBurstPeriodMs{ 48 };
constexpr uint16_t kRadarSweepPeriodRstu{ 1200 };
constexpr uint8_t kRadarSweepsPerBurst{ 1 };
constexpr uint8_t kRadarSamplesPerSweep{ 32 };
constexpr uint16_t kRadarNumberOfBursts{ 0 };
constexpr int16_t kRadarSweepOffset{ -3 };
constexpr uint8_t kRadarTxProfileIdx{ 0 };
constexpr cherry_common_preamble_duration kRadarPreambleDuration{ CHERRY_COMMON_PREAMBLE_DURATION_32 };
constexpr cherry_common_rframe_config kRadarRframeConfig{ CHERRY_COMMON_RFRAME_CONFIG_SP1 };
constexpr uint8_t kRadarPreambleCodeIndex{ 25 };
constexpr uint8_t kRadarAntSetId{ CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_RADAR_ANT_SET_ID };
constexpr uint8_t kRadarChannel{ CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_RADAR_CHANNEL };
constexpr uint32_t kRadarSessionId{ 2 };

constexpr uint16_t kUwbMaximumReportableDistanceCm{ 500 };
constexpr uint32_t kRadarActivationDistanceCm{ CONFIG_DOOR_LOCK_ALIRO_UWB_QM35_RADAR_ACTIVATION_DISTANCE_CM };

} // namespace

namespace Aliro::Uwb {

using namespace DoorLock::Utils;

void UwbRadar::Init(cherry *ctx, OnRadarMeasurement onRadarMeasurement, OnSessionStopped onSessionStopped)
{
	mCtx = ctx;
	mSession = nullptr;
	mState = LifecycleState::Stopped;
	mStartRequestedDuringStop = false;
	mOnRadarMeasurement = onRadarMeasurement;
	mOnSessionStopped = onSessionStopped;

	VerifyOrDie(k_mutex_init(&mMutex) == 0, "Failed to initialize radar mutex");

	k_work_init(&mStartWork.mWork, StartWorkHandler);
	mStartWork.mOwner = this;
	k_work_init(&mStopWork.mWork, StopWorkHandler);
	mStopWork.mOwner = this;

	SessionEventHub::Register(mSubscriber);
}

int UwbRadar::ScheduleStart()
{
	{
		MutexGuard lock{ mMutex };

		if (mState == LifecycleState::Stopping) {
			/* A ranging session resumed before the previous radar session reached
			 * DEINIT. Coalesce repeated requests and restart after DEINIT. */
			mStartRequestedDuringStop = true;
			return 0;
		}

		VerifyOrReturnValue(mState == LifecycleState::Stopped, -EALREADY);
		mState = LifecycleState::Starting;
	}

	const int ret = k_work_submit(&mStartWork.mWork);
	if (ret < 0) {
		MutexGuard lock{ mMutex };
		mState = LifecycleState::Stopped;
		return ret;
	}
	return 0;
}

void UwbRadar::Stop()
{
	LifecycleState previousState;
	{
		MutexGuard lock{ mMutex };
		mStartRequestedDuringStop = false;
		VerifyOrReturn(mState != LifecycleState::Stopped && mState != LifecycleState::Stopping);
		previousState = mState;
		mState = LifecycleState::Stopping;
	}

	const int ret = k_work_submit(&mStopWork.mWork);
	if (ret < 0) {
		MutexGuard lock{ mMutex };
		mState = previousState;
		LOG_ERR("Failed to schedule radar session stop: %d", ret);
	}
}

void UwbRadar::CancelStart()
{
	k_work_sync sync;
	(void)k_work_cancel_sync(&mStartWork.mWork, &sync);
}

void UwbRadar::StartWorkHandler(k_work *work)
{
	auto *startWork = CONTAINER_OF(work, StartWork, mWork);

	VerifyOrReturn(startWork->mOwner);
	startWork->mOwner->StartSession();
}

void UwbRadar::StopWorkHandler(k_work *work)
{
	auto *stopWork = CONTAINER_OF(work, StopWork, mWork);

	VerifyOrReturn(stopWork->mOwner);
	stopWork->mOwner->StopSession();
}

int UwbRadar::StartSession()
{
	MutexGuard lock{ mMutex };

	/* Stop() can change the state while this work item is still queued. */
	VerifyOrReturnValue(mState == LifecycleState::Starting, 0);
	if (!mCtx) {
		mState = LifecycleState::Stopped;
		LOG_WRN("Cherry context not ready for radar");
		return -EINVAL;
	}
	if (mSession) {
		LOG_ERR("Previous radar session has not reached DEINIT");
		goto exit;
	}

	mSession = cherry_radar_session_create(mCtx, &RadarCallback, this, kRadarSessionId, kRadarBurstPeriodMs,
					       kRadarSweepPeriodRstu, kRadarSweepsPerBurst, kRadarSamplesPerSweep,
					       kRadarAntSetId);
	if (!mSession) {
		mState = LifecycleState::Stopped;
		LOG_ERR("cherry_radar_session_create failed");
		return -EIO;
	}

	VerifyOrExit(cherry_radar_session_set_rframe_config(mSession, kRadarRframeConfig) == CHERRY_ERR_NONE,
		     LOG_ERR("radar set_rframe_config failed"));
	VerifyOrExit(cherry_radar_session_set_preamble_code_index(mSession, kRadarPreambleCodeIndex) == CHERRY_ERR_NONE,
		     LOG_ERR("radar set_preamble_code_index failed"));
	VerifyOrExit(cherry_radar_session_set_preamble_duration(mSession, kRadarPreambleDuration) == CHERRY_ERR_NONE,
		     LOG_ERR("radar set_preamble_duration failed"));
	VerifyOrExit(cherry_radar_session_set_number_of_bursts(mSession, kRadarNumberOfBursts) == CHERRY_ERR_NONE,
		     LOG_ERR("radar set_number_of_bursts failed"));
	VerifyOrExit(cherry_radar_session_set_sweep_offset(mSession, kRadarSweepOffset) == CHERRY_ERR_NONE,
		     LOG_ERR("radar set_sweep_offset failed"));
	VerifyOrExit(cherry_radar_session_set_tx_profile_idx(mSession, kRadarTxProfileIdx) == CHERRY_ERR_NONE,
		     LOG_ERR("radar set_tx_profile_idx failed"));
	VerifyOrExit(cherry_radar_session_set_channel(mSession, kRadarChannel) == CHERRY_ERR_NONE,
		     LOG_ERR("radar set_channel failed"));
	VerifyOrExit(cherry_radar_session_start(mSession) == CHERRY_ERR_NONE,
		     LOG_ERR("cherry_radar_session_start failed"));

	mState = LifecycleState::Running;
	LOG_INF("Radar session started");

	return 0;

exit:
	if (mSession) {
		/* Keep the pointer until Cherry confirms DEINIT. */
		mState = LifecycleState::Stopping;
		cherry_radar_session_destroy(mSession);
	} else {
		mState = LifecycleState::Stopped;
	}
	return -EIO;
}

void UwbRadar::StopSession()
{
	/* CancelStart must run outside mMutex to avoid deadlock with StartSession. */
	// CancelStart();

	cherry_radar_session *session{};
	{
		MutexGuard lock{ mMutex };
		VerifyOrReturn(mState == LifecycleState::Stopping);
		session = mSession;
	}

	VerifyAndCall(mOnSessionStopped);

	if (!session) {
		CompleteStop();
		return;
	}

	/* Destruction is asynchronous. A new radar session is not allowed until
	 * RadarCallback receives DEINIT for this object. */
	cherry_radar_session_destroy(session);
	LOG_INF("Radar session teardown requested");
}

void UwbRadar::CompleteStop()
{
	bool restart;
	{
		MutexGuard lock{ mMutex };
		mSession = nullptr;
		mState = LifecycleState::Stopped;
		restart = mStartRequestedDuringStop && mActiveSessionCount > 0;
		mStartRequestedDuringStop = false;
	}

	LOG_INF("Radar session stopped");

	if (restart) {
		LOG_INF("Restarting radar after teardown completed");
		std::ignore = ScheduleStart();
	}
}

void UwbRadar::OnSessionEvent(const aliro_uwb_session_event &event, const SessionContext &sessionCtx, void *ctx)
{
	VerifyOrReturn(ctx);

	auto *radar = static_cast<UwbRadar *>(ctx);
	radar->HandleSessionEvent(event, sessionCtx);
}

void UwbRadar::HandleSessionEvent(const aliro_uwb_session_event &event, const SessionContext &sessionCtx)
{
	switch (event.type) {
	case ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_STATUS: {
		const auto *status = event.data.status;
		const auto oldState = sessionCtx.mSessionState;
		const auto newState = status->session_state;
		bool shouldStop = false;

		{
			MutexGuard lock{ mMutex };
			if (newState == CHERRY_CCC_SESSION_STATE_ACTIVE &&
			    oldState != CHERRY_CCC_SESSION_STATE_ACTIVE) {
				mActiveSessionCount++;
			} else if (oldState == CHERRY_CCC_SESSION_STATE_ACTIVE &&
				   newState != CHERRY_CCC_SESSION_STATE_ACTIVE) {
				if (mActiveSessionCount > 0) {
					mActiveSessionCount--;
				}
				// Stop radar only when the last ranging session ends.
				shouldStop = (mActiveSessionCount == 0);
			}
		}
		if (shouldStop) {
			Stop();
		}
		break;
	}
	case ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_ERROR:
		Stop();
		break;
	case ALIRO_UWB_SESSION_EVENT_TYPE_SESSION_CONTROLEE_REPORT: {
		const auto *report = event.data.controlee_report;
		if (!report) {
			break;
		}
		for (const auto *m = report->measurements; m; m = m->next) {
			if (m->frame_status) {
				continue;
			}
			if (m->distance_cm > kUwbMaximumReportableDistanceCm) {
				break;
			}
			if (m->distance_cm <= kRadarActivationDistanceCm) {
				std::ignore = ScheduleStart();
			}
		}
		break;
	}
	default:
		break;
	}
}

void UwbRadar::RadarCallback(cherry_radar_event *event, void *userData)
{
	VerifyOrReturn(event);

	UwbRadar *radar{};
	VerifyOrExit(userData);
	radar = static_cast<UwbRadar *>(userData);

	switch (event->type) {
	case CHERRY_RADAR_EVENT_TYPE_SESSION_REPORT: {
		const auto *report = event->data.report;
		VerifyOrExit(report);
		VerifyOrExit(report->n_sweeps > 0);
		VerifyOrExit(report->sweeps);
		const auto sweep = report->sweeps[0];
		VerifyOrExit(sweep.n_data_fragments > 0);
		VerifyOrExit(sweep.data_fragments);
		const auto dataFragment = sweep.data_fragments[0];
		VerifyAndCall(radar->mOnRadarMeasurement, dataFragment.data, dataFragment.size);
		break;
	}
	case CHERRY_RADAR_EVENT_TYPE_SESSION_STATUS:
		if (event->data.status && event->data.status->session_state == CHERRY_RADAR_SESSION_STATE_DEINIT) {
			radar->CompleteStop();
		}
		break;
	case CHERRY_RADAR_EVENT_TYPE_SESSION_ERROR:
		LOG_WRN("Radar session error: 0x%x", static_cast<uint32_t>(event->data.error->status_err));
		radar->Stop();
		break;
	default:
		break;
	}
exit:
	cherry_radar_event_free(event);
}

} // namespace Aliro::Uwb
