// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.protocol

const val CONSOLE_V1 = "console-v1"

enum class Presence { UNCLAIMED, OFFLINE, ONLINE }

enum class GatewayConnection { NOT_CONFIGURED, OFFLINE, ONLINE, SYNCING }

enum class TurnPhase { IDLE, LISTENING, THINKING, SPEAKING, CANCELLING, CANCEL_UNCONFIRMED, PLAYBACK_UNCONFIRMED, OFFLINE, ERROR }

enum class PrivacyCapability {
    MICROPHONE,
    CAMERA,
    LOCATION,
    LONG_TERM_MEMORY,
    AUTHORIZED_VOICE,
}

enum class PermissionState { NOT_GRANTED, ALLOWED, DENIED }

enum class PersonaMode { GENTLE, PLAYFUL, QUIET, SERIOUS, TSUNDERE_LITE }

/** Model/device-reported affect. Android displays it but never sets it directly. */
enum class Emotion { UNKNOWN, NEUTRAL, HAPPY, SHY, SAD, SURPRISED, THINKING }

enum class UpdatePhase {
    UNKNOWN,
    IDLE,
    AWAITING_LOCAL_CONFIRMATION,
    DOWNLOADING,
    VERIFYING,
    STAGED,
    REBOOTING,
    TRIAL,
    CONFIRMED,
    ROLLED_BACK,
    FAILED,
}

enum class ConsoleErrorCode(val wireValue: String) {
    UNSUPPORTED_VERSION("unsupported_version"),
    DEVICE_UNCLAIMED("device_unclaimed"),
    GATEWAY_OFFLINE("gateway_offline"),
    STALE_GENERATION("stale_generation"),
    REVISION_CONFLICT("revision_conflict"),
    REQUEST_EXPIRED("request_expired"),
    PERMISSION_DENIED("permission_denied"),
    LOCAL_CONFIRMATION_REQUIRED("local_confirmation_required"),
    EVENT_GAP("event_gap"),
    UNSUPPORTED_OPERATION("unsupported_operation"),
    VOLUME_NOT_SUPPORTED("volume_not_supported"),
    VOLUME_BUSY("volume_busy"),
    VOLUME_TIMEOUT("volume_timeout"),
    VOLUME_DEVICE_ERROR("volume_device_error"),
    UPDATE_MANIFEST_INVALID("update_manifest_invalid"),
    UPDATE_SIGNATURE_INVALID("update_signature_invalid"),
    UPDATE_PAIR_MISMATCH("update_pair_mismatch"),
    UPDATE_TRIAL_FAILED("update_trial_failed"),
    UPDATE_DEVICE_ERROR("update_device_error"),
}

data class UpdateState(
    val phase: UpdatePhase = UpdatePhase.IDLE,
    val targetVersion: String? = null,
    val progressPercent: Int = 0,
    val error: ConsoleErrorCode? = null,
) {
    init {
        require(progressPercent in 0..100)
        if (phase == UpdatePhase.FAILED) {
            require(error != null)
        }
    }
}

data class DeviceReport(
    val claimed: Boolean,
    val presence: Presence,
    val gateway: GatewayConnection,
    val batteryPercent: Int?,
    val charging: Boolean?,
    val firmwareVersion: String?,
    val revision: Long,
    val turn: TurnPhase,
    val volumePercent: Int?,
    val personaMode: PersonaMode,
    val emotion: Emotion,
    val permissions: Map<PrivacyCapability, PermissionState>,
    val update: UpdateState = UpdateState(),
) {
    init {
        require(batteryPercent == null || batteryPercent in 0..100)
        require(volumePercent == null || volumePercent in 0..100)
        require(revision >= 0)
    }
}

sealed interface ConsoleEvent {
    data class StateSnapshot(val report: DeviceReport) : ConsoleEvent
    data class PresenceChanged(val presence: Presence) : ConsoleEvent
    data class GatewayChanged(val gateway: GatewayConnection) : ConsoleEvent
    data class BatteryChanged(val percent: Int, val charging: Boolean) : ConsoleEvent {
        init {
            require(percent in 0..100)
        }
    }

    data class TurnChanged(val phase: TurnPhase) : ConsoleEvent
    data class PermissionChanged(
        val capability: PrivacyCapability,
        val state: PermissionState,
    ) : ConsoleEvent

    data class SettingsChanged(
        val revision: Long,
        val volumePercent: Int,
        val personaMode: PersonaMode,
    ) : ConsoleEvent {
        init {
            require(revision >= 0)
            require(volumePercent in 0..100)
        }
    }

    data class EmotionChanged(val emotion: Emotion) : ConsoleEvent

    data class UpdateChanged(val update: UpdateState) : ConsoleEvent
    data class ErrorRaised(val code: ConsoleErrorCode) : ConsoleEvent
}

data class ConsoleEventEnvelope(
    val protocol: String = CONSOLE_V1,
    val eventId: String,
    val deviceId: String,
    val generation: Long,
    val sequence: Long,
    val occurredAtEpochMs: Long,
    val event: ConsoleEvent,
) {
    init {
        requireRequestId(eventId)
        requireDeviceId(deviceId)
        require(generation > 0)
        require(sequence > 0)
        require(occurredAtEpochMs >= 0)
    }
}

enum class EventDisposition {
    APPLIED,
    DUPLICATE,
    NEEDS_SNAPSHOT,
    STALE_GENERATION,
    WRONG_DEVICE,
    UNSUPPORTED_PROTOCOL,
}

data class CompanionState(
    val deviceId: String,
    val generation: Long = 0,
    val lastSequence: Long = 0,
    val revision: Long = 0,
    val claimed: Boolean = false,
    val presence: Presence = Presence.UNCLAIMED,
    val gateway: GatewayConnection = GatewayConnection.NOT_CONFIGURED,
    val batteryPercent: Int? = null,
    val charging: Boolean? = null,
    val firmwareVersion: String? = null,
    val turn: TurnPhase = TurnPhase.OFFLINE,
    val volumePercent: Int? = null,
    val personaMode: PersonaMode = PersonaMode.GENTLE,
    val emotion: Emotion = Emotion.NEUTRAL,
    val permissions: Map<PrivacyCapability, PermissionState> = emptyMap(),
    val update: UpdateState = UpdateState(),
    val needsSnapshot: Boolean = false,
    val lastError: ConsoleErrorCode? = null,
)

/**
 * Fail-closed reducer for the Android copy of Gateway reported state.
 *
 * A sequence gap never applies a partial event. Only a full snapshot can clear
 * [CompanionState.needsSnapshot]. A higher device generation is also accepted
 * only through a snapshot, preventing old UI state from being mixed with a
 * restarted device session.
 */
class CompanionStore(deviceId: String) {
    init {
        requireDeviceId(deviceId)
    }

    var state: CompanionState = CompanionState(deviceId = deviceId)
        private set

    fun apply(envelope: ConsoleEventEnvelope): EventDisposition {
        if (envelope.protocol != CONSOLE_V1) {
            return EventDisposition.UNSUPPORTED_PROTOCOL
        }
        if (envelope.deviceId != state.deviceId) {
            return EventDisposition.WRONG_DEVICE
        }
        if (envelope.generation < state.generation) {
            return EventDisposition.STALE_GENERATION
        }
        if (envelope.generation > state.generation && envelope.event !is ConsoleEvent.StateSnapshot) {
            requireSnapshot(ConsoleErrorCode.STALE_GENERATION)
            return EventDisposition.NEEDS_SNAPSHOT
        }
        if (envelope.generation == state.generation && envelope.sequence <= state.lastSequence) {
            return EventDisposition.DUPLICATE
        }

        val isSnapshot = envelope.event is ConsoleEvent.StateSnapshot
        if (!isSnapshot && (state.needsSnapshot || envelope.sequence != state.lastSequence + 1)) {
            requireSnapshot(ConsoleErrorCode.EVENT_GAP)
            return EventDisposition.NEEDS_SNAPSHOT
        }
        if (envelope.event is ConsoleEvent.SettingsChanged &&
            envelope.event.revision <= state.revision
        ) {
            requireSnapshot(ConsoleErrorCode.REVISION_CONFLICT)
            return EventDisposition.NEEDS_SNAPSHOT
        }

        state = when (val event = envelope.event) {
            is ConsoleEvent.StateSnapshot -> fromSnapshot(envelope, event.report)
            is ConsoleEvent.PresenceChanged -> next(envelope).copy(
                presence = event.presence,
                turn = if (event.presence == Presence.ONLINE) state.turn else TurnPhase.OFFLINE,
            )
            is ConsoleEvent.GatewayChanged -> next(envelope).copy(gateway = event.gateway)
            is ConsoleEvent.BatteryChanged -> next(envelope).copy(
                batteryPercent = event.percent,
                charging = event.charging,
            )
            is ConsoleEvent.TurnChanged -> next(envelope).copy(turn = event.phase)
            is ConsoleEvent.PermissionChanged -> next(envelope).copy(
                permissions = state.permissions + (event.capability to event.state),
            )
            is ConsoleEvent.SettingsChanged -> next(envelope).copy(
                revision = event.revision,
                volumePercent = event.volumePercent,
                personaMode = event.personaMode,
            )
            is ConsoleEvent.EmotionChanged -> next(envelope).copy(emotion = event.emotion)
            is ConsoleEvent.UpdateChanged -> next(envelope).copy(update = event.update)
            is ConsoleEvent.ErrorRaised -> next(envelope).copy(lastError = event.code)
        }
        return EventDisposition.APPLIED
    }

    private fun requireSnapshot(error: ConsoleErrorCode) {
        state = state.copy(
            gateway = GatewayConnection.SYNCING,
            needsSnapshot = true,
            lastError = error,
        )
    }

    private fun next(envelope: ConsoleEventEnvelope): CompanionState = state.copy(
        generation = envelope.generation,
        lastSequence = envelope.sequence,
        lastError = null,
    )

    private fun fromSnapshot(
        envelope: ConsoleEventEnvelope,
        report: DeviceReport,
    ): CompanionState = CompanionState(
        deviceId = state.deviceId,
        generation = envelope.generation,
        lastSequence = envelope.sequence,
        revision = report.revision,
        claimed = report.claimed,
        presence = report.presence,
        gateway = report.gateway,
        batteryPercent = report.batteryPercent,
        charging = report.charging,
        firmwareVersion = report.firmwareVersion,
        turn = report.turn,
        volumePercent = report.volumePercent,
        personaMode = report.personaMode,
        emotion = report.emotion,
        permissions = report.permissions.toMap(),
        update = report.update,
        needsSnapshot = false,
        lastError = null,
    )
}
