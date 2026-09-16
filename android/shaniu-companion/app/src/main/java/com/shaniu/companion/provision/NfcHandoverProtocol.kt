// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

/** Discovery only. This response never authorizes BLE claiming or supplies secrets.
 * The board still needs ISO-DEP support before this can form a product handover.
 */
object NfcHandoverProtocol {
    private val accepted = byteArrayOf(0x53, 0x48, 0x4e, 1, 0x90.toByte(), 0)
    fun isHandoverResponse(response: ByteArray) = response.contentEquals(accepted)

    const val AID = "F05348414E495501"
    private val select = byteArrayOf(0, 0xa4.toByte(), 4, 0, 8,
        0xf0.toByte(), 0x53, 0x48, 0x41, 0x4e, 0x49, 0x55, 1)

    fun respond(command: ByteArray, enabled: Boolean): ByteArray {
        if (!enabled) return byteArrayOf(0x69, 0x85.toByte())
        if (command.isEmpty()) return byteArrayOf(0x67, 0)
        if (command[0] != 0.toByte()) return byteArrayOf(0x6e, 0)
        if (command.size < 4) return byteArrayOf(0x67, 0)
        if (command[1] != 0xa4.toByte()) return byteArrayOf(0x6d, 0)
        val exact = command.contentEquals(select)
        val withLe = command.size == select.size + 1 && command.last() == 0.toByte() &&
            command.copyOf(select.size).contentEquals(select)
        return if (exact || withLe) accepted.copyOf()
        else byteArrayOf(0x6a, 0x82.toByte())
    }
}

/** One RF selection; a locator is only a discovery hint, never a credential. */
class NfcHandoverSession {
    private var selected = false
    fun reset() { selected = false }
    fun exchange(command: ByteArray, enabled: Boolean): Pair<ByteArray, ByteArray?> {
        if (!enabled) { reset(); return byteArrayOf(0x69, 0x85.toByte()) to null }
        if (command.firstOrNull() == 0x80.toByte()) {
            if (!selected) return byteArrayOf(0x69, 0x85.toByte()) to null
            selected = false
            if (command.size != 13 || command[1] != 0xda.toByte() ||
                command[2] != 0.toByte() || command[3] != 0.toByte() || command[4] != 8.toByte())
                return byteArrayOf(0x67, 0) to null
            return byteArrayOf(0x90.toByte(), 0) to command.copyOfRange(5, 13)
        }
        val response = NfcHandoverProtocol.respond(command, true)
        selected = NfcHandoverProtocol.isHandoverResponse(response)
        return response to null
    }
}
