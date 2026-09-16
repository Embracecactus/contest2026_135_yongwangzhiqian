// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.protocol

import com.google.gson.JsonArray
import com.google.gson.JsonElement
import com.google.gson.JsonNull
import com.google.gson.JsonObject
import com.google.gson.JsonParser
import com.google.gson.JsonPrimitive
import com.google.gson.Strictness
import com.google.gson.stream.JsonReader
import com.google.gson.stream.JsonToken
import java.io.StringReader

/** Strict, bounded JSON encoding for the Android/Gateway console-v1 contract. */
object ConsoleWireV1 {
    const val MAX_MESSAGE_BYTES = 64 * 1024

    fun encodeEvent(value: ConsoleEventEnvelope): String = JsonObject().apply {
        addProperty("protocol", value.protocol)
        addProperty("kind", "event")
        addProperty("event_id", value.eventId)
        addProperty("device_id", value.deviceId)
        addProperty("generation", value.generation)
        addProperty("sequence", value.sequence)
        addProperty("occurred_at_ms", value.occurredAtEpochMs)
        val (type, payload) = encodeEventPayload(value.event)
        addProperty("type", type)
        add("payload", payload)
    }.toString()

    fun decodeEvent(json: String): ConsoleEventEnvelope = decode(json) { root ->
        root.requireFields(EVENT_FIELDS)
        root.requireProtocol()
        root.requireLiteral("kind", "event")
        val event = decodeEventPayload(root.string("type"), root.obj("payload"))
        ConsoleEventEnvelope(
            protocol = root.string("protocol"),
            eventId = root.string("event_id"),
            deviceId = root.string("device_id"),
            generation = root.positiveLong("generation"),
            sequence = root.positiveLong("sequence"),
            occurredAtEpochMs = root.nonNegativeLong("occurred_at_ms"),
            event = event,
        )
    }

    fun encodeMutation(value: ConsoleMutation): String = JsonObject().apply {
        if (value.operation == ConsoleOperation.RAW_SHELL) {
            unsupportedType("forbidden operation has no wire representation")
        }
        addProperty("protocol", value.protocol)
        addProperty("kind", "mutation")
        addProperty("request_id", value.requestId)
        addProperty("device_id", value.deviceId)
        addProperty("generation", value.generation)
        addProperty("expected_revision", value.expectedRevision)
        addProperty("issued_at_ms", value.issuedAtEpochMs)
        addProperty("expires_at_ms", value.expiresAtEpochMs)
        addProperty("operation", value.operation.wireValue())
        add("arguments", encodeArguments(value.arguments))
    }.toString()

    fun decodeMutation(json: String): ConsoleMutation = decode(json) { root ->
        root.requireFields(MUTATION_FIELDS)
        root.requireProtocol()
        root.requireLiteral("kind", "mutation")
        val operation = operationFromWire(root.string("operation"))
        ConsoleMutation(
            protocol = root.string("protocol"),
            requestId = root.string("request_id"),
            deviceId = root.string("device_id"),
            generation = root.positiveLong("generation"),
            expectedRevision = root.nonNegativeLong("expected_revision"),
            issuedAtEpochMs = root.nonNegativeLong("issued_at_ms"),
            expiresAtEpochMs = root.nonNegativeLong("expires_at_ms"),
            operation = operation,
            arguments = decodeArguments(operation, root.obj("arguments")),
        )
    }

    fun encodeReceipt(value: ConsoleMutationReceipt): String = JsonObject().apply {
        addProperty("protocol", value.protocol)
        addProperty("kind", "mutation.receipt")
        addProperty("request_id", value.requestId)
        addProperty("device_id", value.deviceId)
        addProperty("generation", value.generation)
        addProperty("revision", value.revision)
        addProperty("status", value.status.wireValue())
        addNullableString("error", value.error?.wireValue)
    }.toString()

    fun decodeReceipt(json: String): ConsoleMutationReceipt = decode(json) { root ->
        root.requireFields(RECEIPT_FIELDS)
        root.requireProtocol()
        root.requireLiteral("kind", "mutation.receipt")
        ConsoleMutationReceipt(
            protocol = root.string("protocol"),
            requestId = root.string("request_id"),
            deviceId = root.string("device_id"),
            generation = root.positiveLong("generation"),
            revision = root.nonNegativeLong("revision"),
            status = receiptStatusFromWire(root.string("status")),
            error = root.nullableString("error")?.let(::errorFromWire),
        )
    }

    fun encodeReleaseCatalog(value: FirmwareReleaseCatalog): String = JsonObject().apply {
        addProperty("protocol", value.protocol)
        addProperty("kind", "firmware.release_catalog")
        addProperty("device_id", value.deviceId)
        addProperty("generation", value.generation)
        add(
            "releases",
            JsonArray().also { array ->
                value.releases.forEach { array.add(encodeRelease(it)) }
            },
        )
    }.toString()

    fun decodeReleaseCatalog(json: String): FirmwareReleaseCatalog = decode(json) { root ->
        root.requireFields(RELEASE_CATALOG_FIELDS)
        root.requireProtocol()
        root.requireLiteral("kind", "firmware.release_catalog")
        val array = root.array("releases")
        if (array.size() > MAX_RELEASES) {
            schema("release catalog has too many entries")
        }
        FirmwareReleaseCatalog(
            protocol = root.string("protocol"),
            deviceId = root.string("device_id"),
            generation = root.positiveLong("generation"),
            releases = array.map { decodeRelease(it.requireObject("release")) },
        )
    }

    private fun encodeEventPayload(event: ConsoleEvent): Pair<String, JsonObject> = when (event) {
        is ConsoleEvent.StateSnapshot -> "state.snapshot" to encodeReport(event.report)
        is ConsoleEvent.PresenceChanged -> "presence.changed" to JsonObject().apply {
            addProperty("presence", event.presence.wireValue())
        }
        is ConsoleEvent.GatewayChanged -> "gateway.changed" to JsonObject().apply {
            addProperty("gateway", event.gateway.wireValue())
        }
        is ConsoleEvent.BatteryChanged -> "battery.changed" to JsonObject().apply {
            addProperty("percent", event.percent)
            addProperty("charging", event.charging)
        }
        is ConsoleEvent.TurnChanged -> "turn.changed" to JsonObject().apply {
            addProperty("phase", event.phase.wireValue())
        }
        is ConsoleEvent.PermissionChanged -> "permission.changed" to JsonObject().apply {
            addProperty("capability", event.capability.wireValue())
            addProperty("state", event.state.wireValue())
        }
        is ConsoleEvent.SettingsChanged -> "settings.changed" to JsonObject().apply {
            addProperty("revision", event.revision)
            addProperty("volume_percent", event.volumePercent)
            addProperty("persona_mode", event.personaMode.wireValue())
        }
        is ConsoleEvent.EmotionChanged -> "emotion.changed" to JsonObject().apply {
            addProperty("emotion", event.emotion.wireValue())
        }
        is ConsoleEvent.UpdateChanged -> "update.changed" to encodeUpdate(event.update)
        is ConsoleEvent.ErrorRaised -> "error.raised" to JsonObject().apply {
            addProperty("code", event.code.wireValue)
        }
    }

    private fun decodeEventPayload(type: String, payload: JsonObject): ConsoleEvent = when (type) {
        "state.snapshot" -> ConsoleEvent.StateSnapshot(decodeReport(payload))
        "presence.changed" -> {
            payload.requireFields(setOf("presence"))
            ConsoleEvent.PresenceChanged(presenceFromWire(payload.string("presence")))
        }
        "gateway.changed" -> {
            payload.requireFields(setOf("gateway"))
            ConsoleEvent.GatewayChanged(gatewayFromWire(payload.string("gateway")))
        }
        "battery.changed" -> {
            payload.requireFields(setOf("percent", "charging"))
            ConsoleEvent.BatteryChanged(payload.percent("percent"), payload.boolean("charging"))
        }
        "turn.changed" -> {
            payload.requireFields(setOf("phase"))
            ConsoleEvent.TurnChanged(turnFromWire(payload.string("phase")))
        }
        "permission.changed" -> {
            payload.requireFields(setOf("capability", "state"))
            ConsoleEvent.PermissionChanged(
                privacyFromWire(payload.string("capability")),
                permissionFromWire(payload.string("state")),
            )
        }
        "settings.changed" -> {
            payload.requireFields(setOf("revision", "volume_percent", "persona_mode"))
            ConsoleEvent.SettingsChanged(
                revision = payload.nonNegativeLong("revision"),
                volumePercent = payload.percent("volume_percent"),
                personaMode = personaFromWire(payload.string("persona_mode")),
            )
        }
        "emotion.changed" -> {
            payload.requireFields(setOf("emotion"))
            ConsoleEvent.EmotionChanged(emotionFromWire(payload.string("emotion")))
        }
        "update.changed" -> ConsoleEvent.UpdateChanged(decodeUpdate(payload))
        "error.raised" -> {
            payload.requireFields(setOf("code"))
            ConsoleEvent.ErrorRaised(errorFromWire(payload.string("code")))
        }
        else -> unsupportedType("unsupported console event type")
    }

    private fun encodeReport(value: DeviceReport): JsonObject = JsonObject().apply {
        addProperty("claimed", value.claimed)
        addProperty("presence", value.presence.wireValue())
        addProperty("gateway", value.gateway.wireValue())
        addNullableNumber("battery_percent", value.batteryPercent)
        if (value.charging == null) add("charging", JsonNull.INSTANCE)
        else addProperty("charging", value.charging)
        addNullableString("firmware_version", value.firmwareVersion)
        addProperty("revision", value.revision)
        addProperty("turn", value.turn.wireValue())
        addNullableNumber("volume_percent", value.volumePercent)
        addProperty("persona_mode", value.personaMode.wireValue())
        addProperty("emotion", value.emotion.wireValue())
        add(
            "permissions",
            JsonObject().also { permissions ->
                PrivacyCapability.entries.forEach { capability ->
                    permissions.addProperty(
                        capability.wireValue(),
                        (value.permissions[capability] ?: PermissionState.NOT_GRANTED).wireValue(),
                    )
                }
            },
        )
        add("update", encodeUpdate(value.update))
    }

    private fun decodeReport(value: JsonObject): DeviceReport {
        value.requireFields(REPORT_FIELDS)
        val permissions = value.obj("permissions")
        val permissionNames = PrivacyCapability.entries.map { it.wireValue() }.toSet()
        permissions.requireFields(permissionNames)
        return DeviceReport(
            claimed = value.boolean("claimed"),
            presence = presenceFromWire(value.string("presence")),
            gateway = gatewayFromWire(value.string("gateway")),
            batteryPercent = value.nullableInt("battery_percent")?.also {
                if (it !in 0..100) schema("battery_percent is out of range")
            },
            charging = value.nullableBoolean("charging"),
            firmwareVersion = value.nullableString("firmware_version"),
            revision = value.nonNegativeLong("revision"),
            turn = turnFromWire(value.string("turn")),
            volumePercent = value.nullableInt("volume_percent")?.also {
                if (it !in 0..100) schema("volume_percent is out of range")
            },
            personaMode = personaFromWire(value.string("persona_mode")),
            emotion = emotionFromWire(value.string("emotion")),
            permissions = PrivacyCapability.entries.associateWith { capability ->
                permissionFromWire(permissions.string(capability.wireValue()))
            },
            update = decodeUpdate(value.obj("update")),
        )
    }

    private fun encodeUpdate(value: UpdateState): JsonObject = JsonObject().apply {
        addProperty("phase", value.phase.wireValue())
        addNullableString("target_version", value.targetVersion)
        addProperty("progress_percent", value.progressPercent)
        addNullableString("error", value.error?.wireValue)
    }

    private fun decodeUpdate(value: JsonObject): UpdateState {
        value.requireFields(UPDATE_FIELDS)
        return UpdateState(
            phase = updateFromWire(value.string("phase")),
            targetVersion = value.nullableString("target_version"),
            progressPercent = value.percent("progress_percent"),
            error = value.nullableString("error")?.let(::errorFromWire),
        )
    }

    private fun encodeArguments(value: ConsoleMutationArguments): JsonObject = JsonObject().apply {
        when (value) {
            ConsoleMutationArguments.None -> Unit
            is ConsoleMutationArguments.SetVolume ->
                addProperty("volume_percent", value.volumePercent)
            is ConsoleMutationArguments.SetPersonaMode ->
                addProperty("persona_mode", value.personaMode.wireValue())
            is ConsoleMutationArguments.ConfigurePermission -> {
                addProperty("capability", value.capability.wireValue())
                addProperty("state", value.state.wireValue())
            }
            is ConsoleMutationArguments.FirmwareUpdate ->
                addProperty("release_manifest_sha256", value.releaseManifestSha256)
            is ConsoleMutationArguments.DeleteMemory ->
                addProperty("scope", value.scope.wireValue())
        }
    }

    private fun decodeArguments(
        operation: ConsoleOperation,
        value: JsonObject,
    ): ConsoleMutationArguments = when (operation) {
        ConsoleOperation.SET_VOLUME -> {
            value.requireFields(setOf("volume_percent"))
            ConsoleMutationArguments.SetVolume(value.percent("volume_percent"))
        }
        ConsoleOperation.SET_PERSONA_MODE -> {
            value.requireFields(setOf("persona_mode"))
            ConsoleMutationArguments.SetPersonaMode(
                personaFromWire(value.string("persona_mode")),
            )
        }
        ConsoleOperation.CONFIGURE_PERMISSION -> {
            value.requireFields(setOf("capability", "state"))
            ConsoleMutationArguments.ConfigurePermission(
                privacyFromWire(value.string("capability")),
                permissionFromWire(value.string("state")),
            )
        }
        ConsoleOperation.REQUEST_FIRMWARE_UPDATE -> {
            value.requireFields(setOf("release_manifest_sha256"))
            ConsoleMutationArguments.FirmwareUpdate(value.string("release_manifest_sha256"))
        }
        ConsoleOperation.DELETE_MEMORY -> {
            value.requireFields(setOf("scope"))
            ConsoleMutationArguments.DeleteMemory(memoryScopeFromWire(value.string("scope")))
        }
        else -> {
            value.requireFields(emptySet())
            ConsoleMutationArguments.None
        }
    }

    private fun encodeRelease(value: FirmwareRelease): JsonObject = JsonObject().apply {
        addProperty("manifest_sha256", value.manifestSha256)
        addProperty("target_version", value.targetVersion)
        addProperty("required_source_version", value.requiredSourceVersion)
        addProperty("required_source_root_sha256", value.requiredSourceRootSha256)
        addProperty("board_family", value.boardFamily)
        addProperty("physical_board", value.physicalBoard)
        addProperty("layout_identity", value.layoutIdentity)
        addProperty("layout_sha256", value.layoutSha256)
        addProperty("package_sha256", value.packageSha256)
        addProperty("package_size_bytes", value.packageSizeBytes)
    }

    private fun decodeRelease(value: JsonObject): FirmwareRelease {
        value.requireFields(RELEASE_FIELDS)
        return FirmwareRelease(
            manifestSha256 = value.string("manifest_sha256"),
            targetVersion = value.string("target_version"),
            requiredSourceVersion = value.string("required_source_version"),
            requiredSourceRootSha256 = value.string("required_source_root_sha256"),
            boardFamily = value.string("board_family"),
            physicalBoard = value.string("physical_board"),
            layoutIdentity = value.string("layout_identity"),
            layoutSha256 = value.string("layout_sha256"),
            packageSha256 = value.string("package_sha256"),
            packageSizeBytes = value.positiveLong("package_size_bytes"),
        )
    }

    private inline fun <T> decode(json: String, block: (JsonObject) -> T): T {
        val root = parseRoot(json)
        return try {
            block(root)
        } catch (error: ConsoleWireException) {
            throw error
        } catch (error: RuntimeException) {
            throw ConsoleWireException(
                ConsoleWireFailure.SCHEMA_MISMATCH,
                "console-v1 message failed schema validation",
                error,
            )
        }
    }

    private fun parseRoot(json: String): JsonObject {
        if (json.length > MAX_MESSAGE_BYTES || json.toByteArray(Charsets.UTF_8).size > MAX_MESSAGE_BYTES) {
            throw ConsoleWireException(
                ConsoleWireFailure.MESSAGE_TOO_LARGE,
                "console-v1 message exceeds $MAX_MESSAGE_BYTES bytes",
            )
        }
        return try {
            val reader = JsonReader(StringReader(json))
            reader.setStrictness(Strictness.STRICT)
            val element = JsonParser.parseReader(reader)
            if (reader.peek() != JsonToken.END_DOCUMENT || !element.isJsonObject) {
                malformed("console-v1 root must be one JSON object")
            }
            element.asJsonObject
        } catch (error: ConsoleWireException) {
            throw error
        } catch (error: RuntimeException) {
            throw ConsoleWireException(
                ConsoleWireFailure.MALFORMED_JSON,
                "console-v1 message is not strict JSON",
                error,
            )
        }
    }

    private fun JsonObject.requireProtocol() {
        if (string("protocol") != CONSOLE_V1) {
            throw ConsoleWireException(
                ConsoleWireFailure.UNSUPPORTED_PROTOCOL,
                "unsupported console protocol",
            )
        }
    }

    private fun JsonObject.requireFields(required: Set<String>) {
        if (keySet() != required) {
            schema("console-v1 object fields do not match the schema")
        }
    }

    private fun JsonObject.requireLiteral(name: String, expected: String) {
        if (string(name) != expected) {
            unsupportedType("unsupported console message kind")
        }
    }

    private fun JsonObject.string(name: String): String {
        val primitive = get(name) as? JsonPrimitive
            ?: schema("$name must be a string")
        if (!primitive.isString) schema("$name must be a string")
        return primitive.asString
    }

    private fun JsonObject.nullableString(name: String): String? {
        val value = get(name) ?: schema("$name is required")
        if (value.isJsonNull) return null
        val primitive = value as? JsonPrimitive
            ?: schema("$name must be a string or null")
        if (!primitive.isString) schema("$name must be a string or null")
        return primitive.asString
    }

    private fun JsonObject.boolean(name: String): Boolean {
        val primitive = get(name) as? JsonPrimitive
            ?: schema("$name must be a boolean")
        if (!primitive.isBoolean) schema("$name must be a boolean")
        return primitive.asBoolean
    }

    private fun JsonObject.nullableBoolean(name: String): Boolean? {
        val value = get(name) ?: schema("$name is required")
        return if (value.isJsonNull) null else boolean(name)
    }

    private fun JsonObject.nonNegativeLong(name: String): Long = integer(name).also {
        if (it < 0) schema("$name must be non-negative")
    }

    private fun JsonObject.positiveLong(name: String): Long = integer(name).also {
        if (it <= 0) schema("$name must be positive")
    }

    private fun JsonObject.integer(name: String): Long {
        val primitive = get(name) as? JsonPrimitive
            ?: schema("$name must be an integer")
        if (!primitive.isNumber) schema("$name must be an integer")
        val raw = primitive.asString
        if (!INTEGER.matches(raw)) schema("$name must be an integer")
        return raw.toLongOrNull() ?: schema("$name is out of range")
    }

    private fun JsonObject.nullableInt(name: String): Int? {
        val value = get(name) ?: schema("$name is required")
        if (value.isJsonNull) return null
        val primitive = value as? JsonPrimitive
            ?: schema("$name must be an integer or null")
        if (!primitive.isNumber || !INTEGER.matches(primitive.asString)) {
            schema("$name must be an integer or null")
        }
        return primitive.asString.toIntOrNull() ?: schema("$name is out of range")
    }

    private fun JsonObject.percent(name: String): Int = integer(name).also {
        if (it !in 0..100) schema("$name is out of range")
    }.toInt()

    private fun JsonObject.obj(name: String): JsonObject =
        get(name)?.takeIf { it.isJsonObject }?.asJsonObject
            ?: schema("$name must be an object")

    private fun JsonObject.array(name: String): JsonArray =
        get(name)?.takeIf { it.isJsonArray }?.asJsonArray
            ?: schema("$name must be an array")

    private fun JsonElement.requireObject(label: String): JsonObject =
        takeIf { it.isJsonObject }?.asJsonObject ?: schema("$label must be an object")

    private fun JsonObject.addNullableString(name: String, value: String?) {
        if (value == null) add(name, JsonNull.INSTANCE) else addProperty(name, value)
    }

    private fun JsonObject.addNullableNumber(name: String, value: Number?) {
        if (value == null) add(name, JsonNull.INSTANCE) else addProperty(name, value)
    }

    private fun Presence.wireValue() = name.lowercase()
    private fun GatewayConnection.wireValue() = name.lowercase()
    private fun TurnPhase.wireValue() = name.lowercase()
    private fun PermissionState.wireValue() = name.lowercase()
    private fun PersonaMode.wireValue() = name.lowercase()
    private fun Emotion.wireValue() = name.lowercase()
    private fun UpdatePhase.wireValue() = name.lowercase()
    private fun PrivacyCapability.wireValue() = name.lowercase()
    private fun MemoryDeleteScope.wireValue() = name.lowercase()
    private fun MutationReceiptStatus.wireValue() = name.lowercase()

    private fun ConsoleOperation.wireValue(): String = when (this) {
        ConsoleOperation.READ_STATUS -> "status.read"
        ConsoleOperation.SET_VOLUME -> "volume.set"
        ConsoleOperation.SET_PERSONA_MODE -> "persona_mode.set"
        ConsoleOperation.CANCEL_TURN -> "turn.cancel"
        ConsoleOperation.START_REMOTE_TURN -> "turn.start_remote"
        ConsoleOperation.REQUEST_SNAPSHOT -> "snapshot.request"
        ConsoleOperation.CONFIGURE_PERMISSION -> "permission.configure"
        ConsoleOperation.BIND_GATEWAY -> "gateway.bind"
        ConsoleOperation.REQUEST_FIRMWARE_UPDATE -> "firmware.update"
        ConsoleOperation.DELETE_MEMORY -> "memory.delete"
        ConsoleOperation.UNBIND_DEVICE -> "device.unbind"
        ConsoleOperation.RAW_SHELL -> unsupportedType("forbidden operation has no wire representation")
    }

    private fun presenceFromWire(value: String) = enumFromWire<Presence>(value, "presence")
    private fun gatewayFromWire(value: String) = enumFromWire<GatewayConnection>(value, "gateway")
    private fun turnFromWire(value: String) = enumFromWire<TurnPhase>(value, "turn")
    private fun permissionFromWire(value: String) = enumFromWire<PermissionState>(value, "permission")
    private fun personaFromWire(value: String) = enumFromWire<PersonaMode>(value, "persona")
    private fun emotionFromWire(value: String) = enumFromWire<Emotion>(value, "emotion")
    private fun updateFromWire(value: String) = enumFromWire<UpdatePhase>(value, "update")
    private fun privacyFromWire(value: String) = enumFromWire<PrivacyCapability>(value, "capability")
    private fun memoryScopeFromWire(value: String) = enumFromWire<MemoryDeleteScope>(value, "scope")
    private fun receiptStatusFromWire(value: String) =
        enumFromWire<MutationReceiptStatus>(value, "receipt status")

    private inline fun <reified T : Enum<T>> enumFromWire(value: String, label: String): T =
        enumValues<T>().singleOrNull { it.name.lowercase() == value }
            ?: unsupportedType("unsupported $label value")

    private fun operationFromWire(value: String): ConsoleOperation = when (value) {
        "status.read" -> ConsoleOperation.READ_STATUS
        "volume.set" -> ConsoleOperation.SET_VOLUME
        "persona_mode.set" -> ConsoleOperation.SET_PERSONA_MODE
        "turn.cancel" -> ConsoleOperation.CANCEL_TURN
        "turn.start_remote" -> ConsoleOperation.START_REMOTE_TURN
        "snapshot.request" -> ConsoleOperation.REQUEST_SNAPSHOT
        "permission.configure" -> ConsoleOperation.CONFIGURE_PERMISSION
        "gateway.bind" -> ConsoleOperation.BIND_GATEWAY
        "firmware.update" -> ConsoleOperation.REQUEST_FIRMWARE_UPDATE
        "memory.delete" -> ConsoleOperation.DELETE_MEMORY
        "device.unbind" -> ConsoleOperation.UNBIND_DEVICE
        else -> unsupportedType("unsupported console operation")
    }

    private fun errorFromWire(value: String): ConsoleErrorCode =
        ConsoleErrorCode.entries.singleOrNull { it.wireValue == value }
            ?: unsupportedType("unsupported console error code")

    private fun malformed(message: String): Nothing =
        throw ConsoleWireException(ConsoleWireFailure.MALFORMED_JSON, message)

    private fun schema(message: String): Nothing =
        throw ConsoleWireException(ConsoleWireFailure.SCHEMA_MISMATCH, message)

    private fun unsupportedType(message: String): Nothing =
        throw ConsoleWireException(ConsoleWireFailure.UNSUPPORTED_TYPE, message)

    private const val MAX_RELEASES = 32
    private val INTEGER = Regex("^-?(0|[1-9][0-9]*)$")
    private val EVENT_FIELDS = setOf(
        "protocol", "kind", "event_id", "device_id", "generation", "sequence",
        "occurred_at_ms", "type", "payload",
    )
    private val MUTATION_FIELDS = setOf(
        "protocol", "kind", "request_id", "device_id", "generation", "expected_revision",
        "issued_at_ms", "expires_at_ms", "operation", "arguments",
    )
    private val RECEIPT_FIELDS = setOf(
        "protocol", "kind", "request_id", "device_id", "generation", "revision", "status", "error",
    )
    private val RELEASE_CATALOG_FIELDS = setOf(
        "protocol", "kind", "device_id", "generation", "releases",
    )
    private val REPORT_FIELDS = setOf(
        "claimed", "presence", "gateway", "battery_percent", "charging", "firmware_version",
        "revision", "turn", "volume_percent", "persona_mode", "emotion", "permissions", "update",
    )
    private val UPDATE_FIELDS = setOf("phase", "target_version", "progress_percent", "error")
    private val RELEASE_FIELDS = setOf(
        "manifest_sha256", "target_version", "required_source_version",
        "required_source_root_sha256", "board_family", "physical_board", "layout_identity",
        "layout_sha256", "package_sha256", "package_size_bytes",
    )
}
