// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.security.MessageDigest

/** 校验包格式和显式前端版本；不根据模型张量猜测前端。 */
internal data class WakeModelPackage(val bytes: ByteArray, val label: String, val phrase: String,
                                    val sha256: ByteArray, val frontendVersion: Int = 1) {
    companion object {
        private const val HEADER = 136
        private const val MAX_MODEL = 65536
        data class Descriptor(val label: String, val phrase: String, val sha256: ByteArray,
                              val frontendVersion: Int = 1)
        data class Status(val busy: Boolean, val error: Int, val active: Descriptor,
                          val previous: Descriptor?, val supportsFrontendV2: Boolean = false)

        private fun headerSize(bytes: ByteArray, offset: Int): Int = when {
            bytes.size < offset + HEADER -> 0
            bytes.copyOfRange(offset, offset + 4).contentEquals("WKM1".toByteArray()) -> HEADER
            bytes.size >= offset + HEADER + 4 &&
                bytes.copyOfRange(offset, offset + 4).contentEquals("WKM2".toByteArray()) &&
                ByteBuffer.wrap(bytes, offset + HEADER, 4).int in 1..2 -> HEADER + 4
            else -> 0
        }

        private fun decodeText(bytes: ByteArray, at: Int, count: Int): String? {
            val field = bytes.copyOfRange(at, at + count)
            val end = field.indexOf(0)
            if (end <= 0 || field.drop(end).any { it != 0.toByte() }) return null
            return Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(field, 0, end)).toString()
        }

        private fun descriptor(bytes: ByteArray, offset: Int): Descriptor? {
            val header = headerSize(bytes, offset)
            if (header == 0 ||
                ByteBuffer.wrap(bytes, offset + 4, 4).int !in 1..MAX_MODEL) return null
            val label = decodeText(bytes, offset + 40, 32) ?: return null
            val phrase = decodeText(bytes, offset + 72, 64) ?: return null
            if (!label.matches(Regex("[a-z0-9_]{1,31}")) || phrase.isBlank() ||
                phrase.any { it.code < 0x20 }) return null
            val frontend = if (header == HEADER) 1 else ByteBuffer.wrap(bytes, offset + HEADER, 4).int
            return Descriptor(label, phrase, bytes.copyOfRange(offset + 8, offset + 40), frontend)
        }

        fun status(bytes: ByteArray): Status? = try {
            val legacy = bytes.size == 284 && bytes.copyOfRange(0, 4).contentEquals("WKS1".toByteArray())
            val modern = bytes.size == 292 && bytes.copyOfRange(0, 4).contentEquals("WKS2".toByteArray())
            if (!legacy && !modern) null else {
                val header = if (modern) HEADER + 4 else HEADER
                val state = ByteBuffer.wrap(bytes, 4, 4).int
                val error = ByteBuffer.wrap(bytes, 8, 4).int
                val active = descriptor(bytes, 12)
                val previousAt = 12 + header
                val previousEmpty = bytes.copyOfRange(previousAt, bytes.size).all { it == 0.toByte() }
                val previous = if (previousEmpty) null else descriptor(bytes, previousAt)
                if (state !in 0..1 || active == null || headerSize(bytes, 12) != header ||
                    (!previousEmpty && (previous == null || headerSize(bytes, previousAt) != header))) null
                else Status(state == 1, error, active, previous, modern)
            }
        } catch (_: Exception) { null }

        fun read(input: InputStream): WakeModelPackage? = try {
            val buffer = ByteArray(HEADER + 4 + MAX_MODEL + 1)
            var used = 0
            while (used < buffer.size) {
                val count = input.read(buffer, used, buffer.size - used)
                if (count < 0) break
                if (count == 0) throw java.io.IOException("Empty package read")
                used += count
            }
            val all = buffer.copyOf(used)
            val header = headerSize(all, 0)
            if (header == 0 || all.size !in (header + 1)..(header + MAX_MODEL)) null else {
                val b = ByteBuffer.wrap(all); b.position(4); val size = b.int
                if (size !in 1..MAX_MODEL || all.size != header + size) null else {
                    val digest = all.copyOfRange(8, 40); val model = all.copyOfRange(header, all.size)
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
                        else WakeModelPackage(all, label, phrase, digest,
                            if (header == HEADER) 1 else ByteBuffer.wrap(all, HEADER, 4).int)
                    }
                }
            }
        } catch (_: Exception) { null }
    }
}
