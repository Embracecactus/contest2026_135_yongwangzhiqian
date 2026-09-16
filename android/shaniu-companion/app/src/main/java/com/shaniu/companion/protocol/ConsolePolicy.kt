// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.protocol

enum class PermissionLevel { L0_READ, L1_PREFERENCE, L2_PRIVACY, L3_ADMIN, FORBIDDEN }

enum class ConsoleOperation(
    val requiredLevel: PermissionLevel,
    val requiresLocalConfirmation: Boolean = false,
) {
    READ_STATUS(PermissionLevel.L0_READ),
    SET_VOLUME(PermissionLevel.L1_PREFERENCE),
    SET_PERSONA_MODE(PermissionLevel.L1_PREFERENCE),
    CANCEL_TURN(PermissionLevel.L1_PREFERENCE),
    START_REMOTE_TURN(PermissionLevel.L2_PRIVACY),
    REQUEST_SNAPSHOT(PermissionLevel.L2_PRIVACY),
    CONFIGURE_PERMISSION(PermissionLevel.L3_ADMIN, requiresLocalConfirmation = true),
    BIND_GATEWAY(PermissionLevel.L3_ADMIN, requiresLocalConfirmation = true),
    REQUEST_FIRMWARE_UPDATE(PermissionLevel.L3_ADMIN, requiresLocalConfirmation = true),
    DELETE_MEMORY(PermissionLevel.L3_ADMIN, requiresLocalConfirmation = true),
    UNBIND_DEVICE(PermissionLevel.L3_ADMIN, requiresLocalConfirmation = true),
    RAW_SHELL(PermissionLevel.FORBIDDEN),
}

enum class MemoryDeleteScope { CONVERSATIONS, ALL }

sealed interface ConsoleMutationArguments {
    data object None : ConsoleMutationArguments

    data class SetVolume(val volumePercent: Int) : ConsoleMutationArguments {
        init {
            require(volumePercent in 0..100)
        }
    }

    data class SetPersonaMode(val personaMode: PersonaMode) : ConsoleMutationArguments

    data class ConfigurePermission(
        val capability: PrivacyCapability,
        val state: PermissionState,
    ) : ConsoleMutationArguments

    /**
     * Selects an immutable Gateway release manifest. It never carries a URL,
     * local path, firmware bytes, Flash offset or signing material.
     */
    data class FirmwareUpdate(val releaseManifestSha256: String) : ConsoleMutationArguments {
        init {
            require(SHA256.matches(releaseManifestSha256))
        }
    }

    data class DeleteMemory(val scope: MemoryDeleteScope) : ConsoleMutationArguments
}

data class ConsoleMutation(
    val protocol: String = CONSOLE_V1,
    val requestId: String,
    val deviceId: String,
    val generation: Long,
    val expectedRevision: Long,
    val issuedAtEpochMs: Long,
    val expiresAtEpochMs: Long,
    val operation: ConsoleOperation,
    val arguments: ConsoleMutationArguments = ConsoleMutationArguments.None,
) {
    init {
        requireRequestId(requestId)
        requireDeviceId(deviceId)
        require(generation > 0)
        require(expectedRevision >= 0)
        require(issuedAtEpochMs >= 0)
        require(expiresAtEpochMs >= issuedAtEpochMs)
        require(arguments.compatibleWith(operation))
    }
}

private fun ConsoleMutationArguments.compatibleWith(operation: ConsoleOperation): Boolean =
    when (operation) {
        ConsoleOperation.SET_VOLUME -> this is ConsoleMutationArguments.SetVolume
        ConsoleOperation.SET_PERSONA_MODE -> this is ConsoleMutationArguments.SetPersonaMode
        ConsoleOperation.CONFIGURE_PERMISSION -> this is ConsoleMutationArguments.ConfigurePermission
        ConsoleOperation.REQUEST_FIRMWARE_UPDATE -> this is ConsoleMutationArguments.FirmwareUpdate
        ConsoleOperation.DELETE_MEMORY -> this is ConsoleMutationArguments.DeleteMemory
        else -> this === ConsoleMutationArguments.None
    }

data class MutationContext(
    val nowEpochMs: Long,
    val deviceId: String,
    val claimed: Boolean,
    val gatewayOnline: Boolean,
    val generation: Long,
    val revision: Long,
    val grantedLevels: Set<PermissionLevel>,
    val locallyConfirmed: Boolean,
)

sealed interface MutationDecision {
    data object Accepted : MutationDecision
    data class Rejected(val code: ConsoleErrorCode) : MutationDecision
}

object ConsolePolicy {
    fun evaluate(mutation: ConsoleMutation, context: MutationContext): MutationDecision {
        if (mutation.protocol != CONSOLE_V1) {
            return MutationDecision.Rejected(ConsoleErrorCode.UNSUPPORTED_VERSION)
        }
        if (mutation.deviceId != context.deviceId || !context.claimed) {
            return MutationDecision.Rejected(ConsoleErrorCode.DEVICE_UNCLAIMED)
        }
        if (context.nowEpochMs >= mutation.expiresAtEpochMs) {
            return MutationDecision.Rejected(ConsoleErrorCode.REQUEST_EXPIRED)
        }
        if (mutation.generation != context.generation) {
            return MutationDecision.Rejected(ConsoleErrorCode.STALE_GENERATION)
        }
        if (mutation.expectedRevision != context.revision) {
            return MutationDecision.Rejected(ConsoleErrorCode.REVISION_CONFLICT)
        }
        if (!context.gatewayOnline) {
            return MutationDecision.Rejected(ConsoleErrorCode.GATEWAY_OFFLINE)
        }
        if (mutation.operation.requiredLevel == PermissionLevel.FORBIDDEN) {
            return MutationDecision.Rejected(ConsoleErrorCode.UNSUPPORTED_OPERATION)
        }
        if (!context.grantedLevels.allows(mutation.operation.requiredLevel)) {
            return MutationDecision.Rejected(ConsoleErrorCode.PERMISSION_DENIED)
        }
        if (mutation.operation.requiresLocalConfirmation && !context.locallyConfirmed) {
            return MutationDecision.Rejected(ConsoleErrorCode.LOCAL_CONFIRMATION_REQUIRED)
        }
        return MutationDecision.Accepted
    }

    private fun Set<PermissionLevel>.allows(required: PermissionLevel): Boolean = when (required) {
        PermissionLevel.L0_READ -> true
        PermissionLevel.L1_PREFERENCE -> contains(PermissionLevel.L1_PREFERENCE) ||
            contains(PermissionLevel.L2_PRIVACY) || contains(PermissionLevel.L3_ADMIN)
        PermissionLevel.L2_PRIVACY -> contains(PermissionLevel.L2_PRIVACY) ||
            contains(PermissionLevel.L3_ADMIN)
        PermissionLevel.L3_ADMIN -> contains(PermissionLevel.L3_ADMIN)
        PermissionLevel.FORBIDDEN -> false
    }
}
