// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.security.MessageDigest

/** Validates a public WKM1 package before it ever enters the control queue. */
internal data class WakeModelPackage(val bytes: ByteArray, val label: String, val phrase: String,
                                    val sha256: ByteArray) {
    companion object {
        private const val HEADER = 136
        private const val MAX_MODEL = 65536
        data class Descriptor(val label: String, val phrase: String, val sha256: ByteArray)
        data class Status(val busy: Boolean, val error: Int, val active: Descriptor, val previous: Descriptor?)

        private fun decodeText(bytes: ByteArray, at: Int, count: Int): String? {
            val field = bytes.copyOfRange(at, at + count)
            val end = field.indexOf(0)
            if (end <= 0 || field.drop(end).any { it != 0.toByte() }) return null
            return Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(field, 0, end)).toString()
        }

        private fun descriptor(bytes: ByteArray, offset: Int): Descriptor? {
            if (!bytes.copyOfRange(offset, offset + 4).contentEquals("WKM1".toByteArray()) ||
                ByteBuffer.wrap(bytes, offset + 4, 4).int !in 1..MAX_MODEL) return null
            val label = decodeText(bytes, offset + 40, 32) ?: return null
            val phrase = decodeText(bytes, offset + 72, 64) ?: return null
            if (!label.matches(Regex("[a-z0-9_]{1,31}")) || phrase.isBlank() ||
                phrase.any { it.code < 0x20 }) return null
            return Descriptor(label, phrase, bytes.copyOfRange(offset + 8, offset + 40))
        }

        fun status(bytes: ByteArray): Status? = try {
            if (bytes.size != 284 || !bytes.copyOfRange(0, 4).contentEquals("WKS1".toByteArray())) null else {
                val state = ByteBuffer.wrap(bytes, 4, 4).int
                val error = ByteBuffer.wrap(bytes, 8, 4).int
                val active = descriptor(bytes, 12)
                val previousEmpty = bytes.copyOfRange(148, 284).all { it == 0.toByte() }
                val previous = if (previousEmpty) null else descriptor(bytes, 148)
                if (state !in 0..1 || active == null || (!previousEmpty && previous == null)) null
                else Status(state == 1, error, active, previous)
            }
        } catch (_: Exception) { null }

        fun read(input: InputStream): WakeModelPackage? = try {
            val buffer = ByteArray(HEADER + MAX_MODEL + 1)
            var used = 0
            while (used < buffer.size) {
                val count = input.read(buffer, used, buffer.size - used)
                if (count < 0) break
                if (count == 0) throw java.io.IOException("Empty package read")
                used += count
            }
            val all = buffer.copyOf(used)
            if (all.size !in (HEADER + 1)..(HEADER + MAX_MODEL) ||
                !all.copyOfRange(0, 4).contentEquals("WKM1".toByteArray())) null else {
                val b = ByteBuffer.wrap(all); b.position(4); val size = b.int
                if (size !in 1..MAX_MODEL || all.size != HEADER + size) null else {
                    val digest = all.copyOfRange(8, 40); val model = all.copyOfRange(HEADER, all.size)
                    if (!MessageDigest.getInstance("SHA-256").digest(model).contentEquals(digest)) null else {
                        fun text(at: Int, count: Int): String? {
                            val x = all.copyOfRange(at, at + count); val zero = x.indexOf(0)
                            if (zero < 0 || x.copyOfRange(zero, count).any { it != 0.toByte() }) return null
                            return Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                                .onUnmappableCharacter(CodingErrorAction.REPORT)
                                .decode(ByteBuffer.wrap(x, 0, zero)).toString()
                        }
                        val label = text(40, 32); val phrase = text(72, 64)
                        if (label == null || phrase == null || !label.matches(Regex("[a-z0-9_]{1,31}")) ||
                            phrase.isBlank() || phrase.any { it.code < 0x20 }) null
                        else WakeModelPackage(all, label, phrase, digest)
                    }
                }
            }
        } catch (_: Exception) { null }
    }
}
