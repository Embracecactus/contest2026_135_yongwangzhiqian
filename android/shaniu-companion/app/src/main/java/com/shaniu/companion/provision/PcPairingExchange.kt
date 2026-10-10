// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import java.security.KeyFactory
import java.security.MessageDigest
import java.security.interfaces.RSAPublicKey
import java.security.spec.X509EncodedKeySpec
import java.security.spec.MGF1ParameterSpec
import javax.crypto.Cipher
import javax.crypto.spec.OAEPParameterSpec
import javax.crypto.spec.PSource

/** Confidential offline envelopes, not sender authentication or grant receipts.
 * The UI must confirm the PC request, use the current authenticated peer identity,
 * obtain a durable grant receipt, and only then hand the response to that PC.
 */
internal object PcPairingExchange {
    const val TTL_MS = 600000L
    private val label = "shaniu-pc-pair-v1".toByteArray(Charsets.US_ASCII)

    class Request internal constructor(private val bytes: ByteArray, private val publicKey: RSAPublicKey,
                                      val capabilities: Int, val createdAtMs: Long, val expiresAtMs: Long) {
        private val digest = MessageDigest.getInstance("SHA-256").digest(bytes)
        val fingerprint = digest.joinToString("") { "%02x".format(it.toInt() and 255) }
        fun clientId() = bytes.copyOfRange(24, 40)
        fun transactionId() = bytes.copyOfRange(40, 56)
        fun encrypt(identity: ProvisionPeerIdentity, borrowedKey: ByteArray, nowMs: Long): ByteArray {
            var plain: ByteArray? = null
            try {
                require(nowMs >= createdAtMs && nowMs < expiresAtMs)
                require(borrowedKey.size == 32 && borrowedKey.any { it != 0.toByte() })
                val cert = java.security.cert.CertificateFactory.getInstance("X.509")
                    .generateCertificate(identity.certificatePem.byteInputStream()).encoded
                require(cert.size in 1..8192)
                val pin = MessageDigest.getInstance("SHA-256").digest(cert)
                require(pin.joinToString("") { "%02x".format(it.toInt() and 255) } == identity.sha256)
                plain = ByteBuffer.allocate(104).putInt(0x53504b31).put(digest).put(pin).put(borrowedKey).putInt(capabilities).array()
                val cipher = Cipher.getInstance("RSA/ECB/OAEPWithSHA-256AndMGF1Padding")
                cipher.init(Cipher.ENCRYPT_MODE, publicKey, OAEPParameterSpec("SHA-256", "MGF1", MGF1ParameterSpec.SHA256,
                    PSource.PSpecified(label + digest)))
                val encrypted = cipher.doFinal(plain)
                require(encrypted.size == 384)
                return ByteBuffer.allocate(40 + cert.size + encrypted.size).putInt(0x53505231).put(digest)
                    .putInt(cert.size).put(cert).put(encrypted).array()
            } catch (_: Exception) { throw IllegalArgumentException("Pairing response could not be prepared") }
            finally { plain?.fill(0) }
        }
    }
    fun parse(input: ByteArray, nowMs: Long): Request {
        try {
            require(input.size in 61..1024)
            val bytes = input.copyOf(); val b = ByteBuffer.wrap(bytes)
            require(b.int == 0x53505131)
            val caps = b.int; val created = b.long; val expires = b.long
            require(caps in 1..31 && created >= 0 && created <= Long.MAX_VALUE - TTL_MS && expires == created + TTL_MS)
            require(nowMs >= created && nowMs < expires)
            require(bytes.copyOfRange(24, 40).any { it != 0.toByte() } && bytes.copyOfRange(40, 56).any { it != 0.toByte() })
            require(b.getInt(56) == bytes.size - 60)
            val der = bytes.copyOfRange(60, bytes.size)
            val key = KeyFactory.getInstance("RSA").generatePublic(X509EncodedKeySpec(der)) as RSAPublicKey
            require(key.modulus.bitLength() == 3072 && key.publicExponent == java.math.BigInteger.valueOf(65537) && key.encoded.contentEquals(der))
            return Request(bytes, key, caps, created, expires)
        } catch (_: Exception) { throw IllegalArgumentException("Invalid or expired PC pairing request") }
    }
}
