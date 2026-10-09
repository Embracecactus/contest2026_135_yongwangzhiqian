// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import java.security.KeyPairGenerator
import java.security.MessageDigest
import java.security.spec.MGF1ParameterSpec
import javax.crypto.Cipher
import javax.crypto.spec.OAEPParameterSpec
import javax.crypto.spec.PSource

/** Actual Android provider, synthetic encryption key; no grant or device I/O. */
internal object PcPairingAcceptance {
    fun run(identity: ProvisionPeerIdentity) {
        val pair = KeyPairGenerator.getInstance("RSA").apply { initialize(3072) }.generateKeyPair()
        val der = pair.public.encoded
        val now = System.currentTimeMillis()
        val request = ByteBuffer.allocate(60 + der.size)
            .putInt(0x53505131).putInt(3).putLong(now).putLong(now + 600000)
            .put(ByteArray(16) { 1 }).put(ByteArray(16) { 2 }).putInt(der.size).put(der).array()
        val key = ByteArray(32).also { it[0] = 84 }
        val digest = MessageDigest.getInstance("SHA-256").digest(request)
        val response = PcPairingExchange.parse(request, now).encrypt(identity, key, now)
        val certSize = ByteBuffer.wrap(response).getInt(36)
        check(response.size == 40 + certSize + 384)
        check(response.copyOfRange(4, 36).contentEquals(digest))
        val cipher = Cipher.getInstance("RSA/ECB/OAEPWithSHA-256AndMGF1Padding")
        cipher.init(Cipher.DECRYPT_MODE, pair.private,
            OAEPParameterSpec("SHA-256", "MGF1", MGF1ParameterSpec.SHA256,
                PSource.PSpecified("shaniu-pc-pair-v1".toByteArray(Charsets.US_ASCII) + digest)))
        val plain = cipher.doFinal(response.copyOfRange(40 + certSize, response.size))
        try {
            check(plain.size == 104 && ByteBuffer.wrap(plain).int == 0x53504b31)
            check(plain.copyOfRange(4, 36).contentEquals(digest))
            val pin = MessageDigest.getInstance("SHA-256").digest(response.copyOfRange(40, 40 + certSize))
            check(plain.copyOfRange(36, 68).contentEquals(pin))
            check(plain.copyOfRange(68, 100).contentEquals(key))
            check(ByteBuffer.wrap(plain).getInt(100) == 3)
        } finally { plain.fill(0); key.fill(0) }
    }
}
