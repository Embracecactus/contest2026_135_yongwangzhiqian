// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer

/** Public preference carried by the existing authenticated CONFIG transaction. */
internal data class ResponseLengthPreference(val mode: Int, val revision: Long, val applied: Int) {
    fun receipt(expected: Int?): String = when {
        expected == null -> ""
        mode != expected -> "设备回读与所选回答长度不一致，请重新读取"
        applied == mode -> "回答长度已保存并回读确认"
        else -> "回答长度已保存；服务尚未应用，请重新读取"
    }

    companion object {
        const val KIND = 21
        const val SIZE = 24

        fun record(mode: Int, revision: Long, transaction: ByteArray): ByteArray {
            require(mode in 0..2 && revision != -1L)
            require(transaction.size == 16 && transaction.any { it != 0.toByte() })
            return ByteBuffer.allocate(32).putInt(0x524c5031).putInt(mode)
                .putLong(revision).put(transaction).array()
        }

        /** Reread the header after the tail so concurrent saves cannot mix revisions. */
        fun decode(wire: ByteArray, confirmedHeader: ByteArray): ResponseLengthPreference {
            require(wire.size == SIZE && confirmedHeader.size == 16)
            require(wire.copyOfRange(0, 16).contentEquals(confirmedHeader))
            val input = ByteBuffer.wrap(wire)
            require(input.int == 0x524c5331)
            val mode = input.int
            val revision = input.long
            val applied = input.int
            require(mode in 0..2 && (applied in 0..2 || applied == -1) && input.int == 0)
            return ResponseLengthPreference(mode, revision, applied)
        }
    }
}
