// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.provision

import java.net.Socket
import java.security.MessageDigest
import java.security.cert.CertificateException
import java.security.cert.X509Certificate
import java.util.Date
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLEngine
import javax.net.ssl.X509ExtendedTrustManager

/** BLE identity is the owner-supplied exact certificate pin, never a BLE name/address.
 * This trust policy is exclusive to provisioning; it must not replace Gateway PKI.
 * TLS itself verifies possession of the pinned certificate's private key.
 */
class ProvisionTls(certificateSha256: ByteArray) {
    private val pin = certificateSha256.copyOf().also {
        require(it.size == 32) { "Invalid provisioning certificate pin" }
    }

    fun newClientEngine(): SSLEngine = newClientContext().createSSLEngine().apply {
        useClientMode = true
        enabledProtocols = arrayOf("TLSv1.2")
        // Match the selected device P-256 certificate; no anonymous or PSK fallback.
        enabledCipherSuites = arrayOf("TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256")
    }

    internal fun newClientContext(): SSLContext = SSLContext.getInstance("TLS").apply {
        init(null, arrayOf(PinnedDeviceTrust(pin)), null)
    }

    fun newChannel(ciphertext: (ByteArray) -> Unit, plaintext: (ByteArray) -> Unit): ProvisionTlsChannel =
        ProvisionTlsChannel(newClientEngine(), ciphertext, plaintext)

    override fun toString(): String = "ProvisionTls(pin=redacted)"
}

internal class PinnedDeviceTrust(
    certificateSha256: ByteArray,
    private val now: () -> Date = { Date() },
) : X509ExtendedTrustManager() {
    private val pin = certificateSha256.copyOf().also { require(it.size == 32) }

    private fun verify(chain: Array<out X509Certificate>?, authType: String?) {
        if (chain.isNullOrEmpty() || chain.size > 4 || authType.isNullOrBlank()) {
            throw CertificateException("Invalid provisioning certificate chain")
        }
        val leaf = chain[0]
        val observed = MessageDigest.getInstance("SHA-256").digest(leaf.encoded)
        if (!MessageDigest.isEqual(pin, observed)) {
            throw CertificateException("Provisioning certificate mismatch")
        }
        leaf.checkValidity(now())
        if (leaf.publicKey.algorithm != "EC") {
            throw CertificateException("Unsupported provisioning public key")
        }
        val usage = leaf.keyUsage
        if (usage != null && (usage.isEmpty() || !usage[0])) {
            throw CertificateException("Provisioning certificate cannot sign")
        }
        val extended = leaf.extendedKeyUsage
        if (extended != null && "1.3.6.1.5.5.7.3.1" !in extended) {
            throw CertificateException("Provisioning server authentication missing")
        }
    }

    override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?) = verify(chain, authType)
    override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?, socket: Socket?) = verify(chain, authType)
    override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?, engine: SSLEngine?) = verify(chain, authType)
    override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?): Unit =
        throw CertificateException("Provisioning client trust is not provided here")
    override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?, socket: Socket?) = checkClientTrusted(chain, authType)
    override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?, engine: SSLEngine?) = checkClientTrusted(chain, authType)
    override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
}

/** Immutable public identity copied from an authenticated TLS peer. It contains
 * no owner/PC credential and does not itself grant control authorization.
 */
class ProvisionPeerIdentity private constructor(val certificatePem: String, val sha256: String) {
    companion object {
        internal fun fromDer(encoded: ByteArray): ProvisionPeerIdentity {
            try {
                require(encoded.size in 1..8192)
                val cert = java.security.cert.CertificateFactory.getInstance("X.509")
                    .generateCertificate(encoded.inputStream()) as X509Certificate
                require(cert.encoded.contentEquals(encoded))
                val pin = MessageDigest.getInstance("SHA-256").digest(encoded)
                    .joinToString("") { "%02x".format(it.toInt() and 255) }
                val body = java.util.Base64.getMimeEncoder(64, byteArrayOf(10)).encodeToString(encoded)
                return ProvisionPeerIdentity("-----BEGIN CERTIFICATE-----\n$body\n-----END CERTIFICATE-----\n", pin)
            } catch (_: Exception) { throw IllegalArgumentException("Invalid public peer certificate") }
        }
    }
}
