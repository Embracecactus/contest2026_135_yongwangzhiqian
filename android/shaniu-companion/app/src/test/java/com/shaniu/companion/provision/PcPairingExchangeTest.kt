// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import java.security.KeyPair
import java.security.KeyPairGenerator
import java.security.MessageDigest
import java.security.cert.CertificateFactory
import java.security.spec.MGF1ParameterSpec
import javax.crypto.Cipher
import javax.crypto.spec.OAEPParameterSpec
import javax.crypto.spec.PSource
import org.junit.Assert.*
import org.junit.BeforeClass
import org.junit.Test

class PcPairingExchangeTest {
    companion object {
        private lateinit var pair: KeyPair
        private const val NOW = 1800000000000L
        @JvmStatic @BeforeClass fun setup() { pair = KeyPairGenerator.getInstance("RSA").apply { initialize(3072) }.generateKeyPair() }
        private fun request(caps: Int = 3, expires: Long = NOW + 600000): ByteArray {
            val der = pair.public.encoded
            return ByteBuffer.allocate(60 + der.size).putInt(0x53505131).putInt(caps).putLong(NOW).putLong(expires)
                .put(ByteArray(16) { 1 }).put(ByteArray(16) { 2 }).putInt(der.size).put(der).array()
        }
        private fun identity(): ProvisionPeerIdentity {
            val input = PcPairingExchangeTest::class.java.getResourceAsStream("/pc-identity.pem")!!
            return input.use { ProvisionPeerIdentity.fromDer(CertificateFactory.getInstance("X.509").generateCertificate(it).encoded) }
        }
    }
    @Test fun independentVectorEncryptsOnlyRequestedPrincipalAndDevice() {
        val raw = request(); val parsed = PcPairingExchange.parse(raw, NOW)
        val digest = MessageDigest.getInstance("SHA-256").digest(raw)
        val key = ByteArray(32).also { it[0] = 84 }; val identity = identity()
        val response = parsed.encrypt(identity, key, NOW + 1)
        assertEquals("SPR1", String(response.copyOfRange(0, 4)))
        assertArrayEquals(digest, response.copyOfRange(4, 36))
        val size = ByteBuffer.wrap(response).getInt(36)
        val cert = CertificateFactory.getInstance("X.509").generateCertificate(response.copyOfRange(40, 40 + size).inputStream())
        val cipher = Cipher.getInstance("RSA/ECB/OAEPWithSHA-256AndMGF1Padding")
        cipher.init(Cipher.DECRYPT_MODE, pair.private, OAEPParameterSpec("SHA-256", "MGF1", MGF1ParameterSpec.SHA256,
            PSource.PSpecified("shaniu-pc-pair-v1".toByteArray() + digest)))
        val plain = cipher.doFinal(response.copyOfRange(40 + size, response.size))
        assertEquals(104, plain.size); assertEquals("SPK1", String(plain.copyOfRange(0, 4)))
        assertArrayEquals(digest, plain.copyOfRange(4, 36))
        assertArrayEquals(MessageDigest.getInstance("SHA-256").digest(cert.encoded), plain.copyOfRange(36, 68))
        assertArrayEquals(key, plain.copyOfRange(68, 100)); assertEquals(3, ByteBuffer.wrap(plain).getInt(100))
        assertEquals(84, key[0].toInt()); plain.fill(0)
    }
    @Test fun requestOwnsInputAndReturnsIndependentPublicIdentifiers() {
        val raw = request(); val parsed = PcPairingExchange.parse(raw, NOW); val fingerprint = parsed.fingerprint
        raw.fill(0); parsed.clientId().fill(0); parsed.transactionId().fill(0)
        assertArrayEquals(ByteArray(16) { 1 }, parsed.clientId())
        assertArrayEquals(ByteArray(16) { 2 }, parsed.transactionId())
        assertEquals(fingerprint, parsed.fingerprint); assertEquals(3, parsed.capabilities)
    }
    @Test fun malformedExpiredAndUnauthorizedRequestsAreRejected() {
        for (raw in listOf(byteArrayOf(), request() + byteArrayOf(0), request(0), request(32), request(expires = NOW + 600001)))
            assertThrows(IllegalArgumentException::class.java) { PcPairingExchange.parse(raw, NOW) }
        for (now in listOf(NOW - 1, NOW + 600000))
            assertThrows(IllegalArgumentException::class.java) { PcPairingExchange.parse(request(), now) }
        val zero = request(); zero.fill(0, 24, 40)
        assertThrows(IllegalArgumentException::class.java) { PcPairingExchange.parse(zero, NOW) }
    }
    @Test fun encryptionRechecksExpiryAndRejectsEmptyCredentials() {
        val parsed = PcPairingExchange.parse(request(), NOW)
        for (key in listOf(ByteArray(32), ByteArray(31)))
            assertThrows(IllegalArgumentException::class.java) { parsed.encrypt(identity(), key, NOW) }
        assertThrows(IllegalArgumentException::class.java) { parsed.encrypt(identity(), ByteArray(32) { 84 }, NOW + 600000) }
    }
    @Test fun hostInteropUsesProductionCodecWhenExplicitlySelected() {
        // Always exercise the production parser. Extra process I/O is selected by
        // the cross-language harness, not counted as another ordinary unit.
        assertEquals(3, PcPairingExchange.parse(request(), NOW).capabilities)
        val requestPath = System.getenv("SHANIU_PC_PAIR_REQUEST") ?: return
        val raw = java.nio.file.Files.readAllBytes(java.nio.file.Path.of(requestPath))
        val now = System.getenv("SHANIU_PC_PAIR_NOW").toLong()
        val certificate = java.nio.file.Files.newInputStream(java.nio.file.Path.of(System.getenv("SHANIU_PC_PAIR_CERT"))).use {
            ProvisionPeerIdentity.fromDer(CertificateFactory.getInstance("X.509").generateCertificate(it).encoded)
        }
        val key = ByteArray(32).also { it[0] = 84 }
        try {
            val response = PcPairingExchange.parse(raw, now).encrypt(certificate, key, now)
            java.nio.file.Files.write(java.nio.file.Path.of(System.getenv("SHANIU_PC_PAIR_RESPONSE")), response)
        } finally { key.fill(0) }
    }
}
