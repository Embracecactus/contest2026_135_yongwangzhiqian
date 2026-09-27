// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.io.ByteArrayOutputStream
import java.net.Socket
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.Principal
import java.security.PrivateKey
import java.security.MessageDigest
import java.security.cert.X509Certificate
import java.security.spec.ECGenParameterSpec
import java.util.Date
import javax.net.ssl.*
import javax.security.auth.x500.X500Principal

/** Called only by instrumentation; temporary Android signing key and in-memory transport.
 * The production TLS channel runs with the device provider, not desktop JSSE.
 */
internal object ControlTlsAcceptance {
    fun run(alias: String) {
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        try {
            KeyPairGenerator.getInstance("EC", "AndroidKeyStore").apply {
                initialize(KeyGenParameterSpec.Builder(alias,
                    KeyProperties.PURPOSE_SIGN or KeyProperties.PURPOSE_VERIFY)
                    .setAlgorithmParameterSpec(ECGenParameterSpec("secp256r1"))
                    // Conscrypt delegates an already hashed TLS signature as NONEwithECDSA.
                    // SHA-256 additionally permits the temporary certificate signature.
                    .setDigests(KeyProperties.DIGEST_NONE, KeyProperties.DIGEST_SHA256)
                    .setCertificateSubject(X500Principal("CN=control-acceptance"))
                    .setCertificateNotBefore(Date(System.currentTimeMillis() - 60_000))
                    .setCertificateNotAfter(Date(System.currentTimeMillis() + 86_400_000))
                    .build())
                generateKeyPair()
            }
            val certificate = store.getCertificate(alias) as X509Certificate
            val signingKey = store.getKey(alias, null) as PrivateKey
            check(signingKey.encoded == null)
            val keys = object : X509ExtendedKeyManager() {
                override fun getPrivateKey(name: String?) = if (name == alias) signingKey else null
                override fun getCertificateChain(name: String?) = if (name == alias) arrayOf(certificate) else null
                override fun getClientAliases(type: String?, issuers: Array<out Principal>?) = null
                override fun chooseClientAlias(types: Array<out String>?, issuers: Array<out Principal>?, socket: Socket?) = null
                override fun getServerAliases(type: String?, issuers: Array<out Principal>?) = if (type == "EC") arrayOf(alias) else null
                override fun chooseServerAlias(type: String?, issuers: Array<out Principal>?, socket: Socket?) = if (type == "EC") alias else null
                override fun chooseEngineServerAlias(type: String?, issuers: Array<out Principal>?, engine: SSLEngine?) = if (type == "EC") alias else null
            }
            val context = SSLContext.getInstance("TLS").apply { init(arrayOf(keys), null, null) }
            val pin = MessageDigest.getInstance("SHA-256").digest(certificate.encoded)
            exchange(context, pin, false)
            pin[0] = (pin[0].toInt() xor 1).toByte()
            exchange(context, pin, true)
        } finally { store.deleteEntry(alias) }
    }

    private fun exchange(context: SSLContext, pin: ByteArray, reject: Boolean) {
        val toClient = ArrayDeque<ByteArray>()
        val toServer = ArrayDeque<ByteArray>()
        val clientPlain = ByteArrayOutputStream()
        val serverPlain = ByteArrayOutputStream()
        fun enqueue(queue: ArrayDeque<ByteArray>, bytes: ByteArray) {
            for (offset in bytes.indices step 20)
                queue.add(bytes.copyOfRange(offset, minOf(offset + 20, bytes.size)))
        }
        val client = ProvisionTls(pin).newChannel({ enqueue(toServer, it) }, { clientPlain.write(it) })
        val server = ProvisionTlsChannel(context.createSSLEngine().apply {
            useClientMode = false
            enabledProtocols = arrayOf("TLSv1.2")
            enabledCipherSuites = arrayOf("TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256")
        }, { enqueue(toClient, it) }, { serverPlain.write(it) })
        fun pumpUntil(done: () -> Boolean) {
            repeat(4096) {
                if (done()) return
                var progress = false
                if (toServer.isNotEmpty()) { server.receive(toServer.removeFirst()); progress = true }
                if (toClient.isNotEmpty()) { client.receive(toClient.removeFirst()); progress = true }
                check(progress) { "TLS engines stalled" }
            }
            error("TLS engines exceeded progress bound")
        }
        try {
            check(client.peerIdentity == null)
            var rejected = false
            try {
                server.start(); client.start()
                pumpUntil { client.established && server.established }
            } catch (error: SSLException) {
                if (!reject) throw error
                rejected = true
            }
            check(rejected == reject)
            if (reject) check(client.peerIdentity == null)
            if (!reject) {
                val identity = requireNotNull(client.peerIdentity)
                check(identity.sha256 == pin.joinToString("") { "%02x".format(it.toInt() and 255) })
                val exported = java.security.cert.CertificateFactory.getInstance("X.509")
                    .generateCertificate(identity.certificatePem.byteInputStream())
                check(MessageDigest.getInstance("SHA-256").digest(exported.encoded).contentEquals(pin))
                val message = ByteArray(48) { (it + 1).toByte() }
                client.send(message)
                pumpUntil { serverPlain.size() == message.size }
                check(serverPlain.toByteArray().contentEquals(message))
                for (offset in message.indices step 5)
                    server.send(message.copyOfRange(offset, minOf(offset + 5, message.size)))
                pumpUntil { clientPlain.size() == message.size }
                check(clientPlain.toByteArray().contentEquals(message))
            }
        } finally {
            client.close(); server.close()
            check(client.peerIdentity == null)
            toClient.forEach { it.fill(0) }; toServer.forEach { it.fill(0) }
        }
    }
}
