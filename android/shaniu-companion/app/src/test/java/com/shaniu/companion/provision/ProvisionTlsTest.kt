// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.provision

import java.nio.file.Files
import java.nio.file.Path
import java.security.KeyStore
import java.security.MessageDigest
import java.security.cert.CertificateException
import java.security.cert.X509Certificate
import java.util.Date
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import javax.net.ssl.KeyManagerFactory
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLException
import javax.net.ssl.SSLServerSocket
import javax.net.ssl.SSLSocket
import org.junit.AfterClass
import org.junit.Assert.*
import org.junit.BeforeClass
import org.junit.Test

class ProvisionTlsTest {
    companion object {
        private lateinit var directory: Path
        private lateinit var store: KeyStore
        private lateinit var certificate: X509Certificate
        private lateinit var pin: ByteArray

        @JvmStatic @BeforeClass fun generateIdentity() {
            directory = Files.createTempDirectory("shaniu-provision-test-")
            val path = directory.resolve("identity.p12")
            val process = ProcessBuilder(
                Path.of(System.getProperty("java.home"), "bin", "keytool").toString(),
                "-genkeypair", "-alias", "device", "-keyalg", "EC", "-groupname", "secp256r1",
                "-dname", "CN=provision-test", "-validity", "2", "-ext", "KU=digitalSignature",
                "-ext", "EKU=serverAuth", "-storetype", "PKCS12", "-keystore", path.toString(),
                "-storepass", "test-only-password", "-noprompt",
            ).redirectErrorStream(true)
                .redirectOutput(directory.resolve("keytool.log").toFile()).start()
            if (!process.waitFor(20, TimeUnit.SECONDS)) {
                process.destroyForcibly()
                fail("Test certificate generation timed out")
            }
            assertEquals(0, process.exitValue())
            store = KeyStore.getInstance("PKCS12").apply {
                Files.newInputStream(path).use { load(it, "test-only-password".toCharArray()) }
            }
            certificate = store.getCertificate("device") as X509Certificate
            pin = MessageDigest.getInstance("SHA-256").digest(certificate.encoded)
        }

        @JvmStatic @AfterClass fun cleanup() {
            if (::directory.isInitialized) directory.toFile().deleteRecursively()
        }
    }

    @Test fun rejectsWrongPinAndInvalidDatesAcrossTrustOverloads() {
        val chain = arrayOf(certificate)
        val trust = PinnedDeviceTrust(pin)
        trust.checkServerTrusted(chain, "ECDHE_ECDSA")
        trust.checkServerTrusted(chain, "ECDHE_ECDSA", ProvisionTls(pin).newClientEngine())
        assertThrows(CertificateException::class.java) {
            PinnedDeviceTrust(ByteArray(32)).checkServerTrusted(chain, "ECDHE_ECDSA")
        }
        for (date in listOf(Date(certificate.notBefore.time - 1000), Date(certificate.notAfter.time + 1000))) {
            assertThrows(CertificateException::class.java) {
                PinnedDeviceTrust(pin) { date }.checkServerTrusted(chain, "ECDHE_ECDSA")
            }
        }
        assertThrows(CertificateException::class.java) { trust.checkServerTrusted(emptyArray(), "ECDHE_ECDSA") }
        assertThrows(CertificateException::class.java) { trust.checkClientTrusted(chain, "ECDHE_ECDSA") }
        val mutablePin = pin.copyOf()
        val tls = ProvisionTls(mutablePin)
        mutablePin.fill(0)
        assertArrayEquals(arrayOf("TLSv1.2"), tls.newClientEngine().enabledProtocols)
        assertFalse(tls.toString().contains(pin.joinToString("")))
    }

    @Test fun gattSessionsSerializeAcknowledgedWritesAcrossMtuChanges() {
        val keys = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm()).apply {
            init(store, "test-only-password".toCharArray())
        }
        val context = SSLContext.getInstance("TLS").apply { init(keys.keyManagers, null, null) }
        val serverPlain = java.io.ByteArrayOutputStream()
        var now = 1000L
        val client = ProvisionGattSession(ProvisionTls(pin), {}, { now })
        val server = ProvisionGattSession({ output, input ->
            ProvisionTlsChannel(context.createSSLEngine().apply {
                useClientMode = false
                enabledProtocols = arrayOf("TLSv1.2")
                enabledCipherSuites = arrayOf("TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256")
            }, output, input)
        }, { serverPlain.write(it) }, { now })
        var maximumPayload = 20
        fun deliver(from: ProvisionGattSession, to: ProvisionGattSession): Boolean {
            val write = from.nextWrite() ?: return false
            assertTrue(write.value.size in 1..maximumPayload)
            assertNull(from.nextWrite())
            // Stale-generation and duplicate-token callbacks cannot consume it.
            from.writeCompleted(from.generation + 100, write.token, false)
            from.writeCompleted(from.generation, write.token - 1, true)
            assertFalse(from.closed)
            assertNull(from.nextWrite())
            to.enqueueIncoming(to.generation, write.value)
            from.writeCompleted(from.generation, write.token, true)
            assertTrue(write.value.all { it == 0.toByte() })
            to.processInput()
            return true
        }
        fun drain() {
            var steps = 0
            while (true) {
                val sent = deliver(client, server)
                val received = deliver(server, client)
                if (!sent && !received) return
                check(++steps < 10000)
            }
        }
        try {
            server.start()
            client.start()
            drain()
            assertTrue(client.established && server.established)
            client.promoteToControl()
            client.negotiatedMtu(client.generation, 70)
            server.negotiatedMtu(server.generation, 70)
            val message = ByteArray(4096) { (it % 251).toByte() }
            client.send(message)
            drain()
            assertArrayEquals(message, serverPlain.toByteArray())
            client.negotiatedMtu(client.generation, 185)
            server.negotiatedMtu(server.generation, 185)
            client.send(message)
            drain()
            assertArrayEquals(message + message, serverPlain.toByteArray())
            /* Valid control responses renew only a promoted control lease. Two
             * activity periods exceed the former absolute 120s lifetime. */
            now += 119_999
            client.touchControlActivity()
            now += 119_999
            client.touchControlActivity()
            assertFalse(client.closed)
            client.negotiatedMtu(client.generation - 1, 23) // stale callbacks never renew a lease
            now += 120_000
            client.tick()
            assertEquals("idle_timeout", client.failure)
            client.disconnected(client.generation - 1)
            assertFalse(client.established)
            client.disconnected(client.generation)
            assertTrue(client.closed)
            assertFalse(client.established)
            client.enqueueIncoming(client.generation, byteArrayOf(1))
            assertNull(client.nextWrite())
        } finally {
            client.close()
            server.close()
        }
    }

    @Test fun gattDeadlinesAndQueueLimitsFailClosed() {
        var now = 0L
        fun session() = ProvisionGattSession(ProvisionTls(pin), {}, { now }).also { it.start() }
        val blocked = session()
        val held = requireNotNull(blocked.nextWrite())
        now = 4999
        blocked.tick()
        assertFalse(blocked.closed)
        now = 5000
        blocked.tick()
        assertEquals("write_timeout", blocked.failure)
        assertTrue(held.value.all { it == 0.toByte() })
        val handshake = session()
        now += 30000
        handshake.tick()
        assertEquals("handshake_timeout", handshake.failure)
        now = 0
        val provisioning = session()
        now = 120_000
        provisioning.tick()
        assertEquals("session_timeout", provisioning.failure)
        val full = session()
        repeat(256) { full.enqueueIncoming(full.generation, byteArrayOf(1)) }
        assertThrows(java.io.IOException::class.java) {
            full.enqueueIncoming(full.generation, byteArrayOf(1))
        }
        assertTrue(full.closed)
        val failed = session()
        val write = requireNotNull(failed.nextWrite())
        assertThrows(java.io.IOException::class.java) {
            failed.writeCompleted(failed.generation, write.token, false)
        }
        assertTrue(failed.closed)
    }

    @Test fun engineChannelHandlesTwentyByteFragmentsAndRejectsCorruption() {
        val keys = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm()).apply {
            init(store, "test-only-password".toCharArray())
        }
        val context = SSLContext.getInstance("TLS").apply { init(keys.keyManagers, null, null) }
        val toServer = java.util.ArrayDeque<ByteArray>()
        val toClient = java.util.ArrayDeque<ByteArray>()
        val clientPlain = java.io.ByteArrayOutputStream()
        val serverPlain = java.io.ByteArrayOutputStream()
        fun enqueue(queue: java.util.ArrayDeque<ByteArray>, data: ByteArray) {
            for (offset in data.indices step 20) {
                queue.add(data.copyOfRange(offset, minOf(offset + 20, data.size)))
            }
        }
        val client = ProvisionTls(pin).newChannel({ enqueue(toServer, it) }, { clientPlain.write(it) })
        val server = ProvisionTlsChannel(context.createSSLEngine().apply {
            useClientMode = false
            enabledProtocols = arrayOf("TLSv1.2")
            enabledCipherSuites = arrayOf("TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256")
        }, { enqueue(toClient, it) }, { serverPlain.write(it) })
        fun drain() {
            var iterations = 0
            while (toServer.isNotEmpty() || toClient.isNotEmpty()) {
                check(++iterations < 10000)
                if (toServer.isNotEmpty()) server.receive(toServer.remove())
                if (toClient.isNotEmpty()) client.receive(toClient.remove())
            }
        }
        try {
            server.start()
            client.start()
            assertFalse(client.established)
            drain()
            assertTrue(client.established && server.established)
            val sent = ByteArray(4096) { (it % 251).toByte() }
            client.send(sent)
            drain()
            assertArrayEquals(sent, serverPlain.toByteArray())
            server.send(byteArrayOf(7, 8, 9))
            drain()
            assertArrayEquals(byteArrayOf(7, 8, 9), clientPlain.toByteArray())
            client.send(byteArrayOf(1, 2, 3))
            val last = requireNotNull(toServer.peekLast())
            last[last.lastIndex] = (last.last().toInt() xor 1).toByte()
            assertThrows(SSLException::class.java) { drain() }
            assertFalse(server.established)
            assertEquals(sent.size, serverPlain.size())
        } finally {
            client.close()
            server.close()
        }
    }

    @Test fun channelRefusesEarlyWritesAndFailsClosedOnBackpressure() {
        var output = 0
        val early = ProvisionTls(pin).newChannel({ output++ }, { fail("Unexpected plaintext") })
        assertThrows(IllegalStateException::class.java) { early.send(byteArrayOf(1)) }
        assertEquals(0, output)
        assertThrows(IllegalStateException::class.java) { early.start() }
        val blocked = ProvisionTls(pin).newChannel({ throw java.io.IOException("queue full") }, {})
        assertThrows(java.io.IOException::class.java) { blocked.start() }
        assertThrows(IllegalStateException::class.java) { blocked.receive(byteArrayOf(1)) }
        val oversized = ProvisionTls(pin).newChannel({}, {})
        oversized.start()
        assertThrows(IllegalArgumentException::class.java) { oversized.receive(ByteArray(32769)) }
        assertFalse(oversized.established)
        assertThrows(IllegalStateException::class.java) { oversized.receive(byteArrayOf(1)) }
    }

    @Test fun realTlsAllowsApplicationBytesOnlyWithMatchingPin() {
        val keys = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm()).apply {
            init(store, "test-only-password".toCharArray())
        }
        val serverContext = SSLContext.getInstance("TLS").apply { init(keys.keyManagers, null, null) }
        for (matches in listOf(true, false)) {
            val executor = Executors.newSingleThreadExecutor()
            (serverContext.serverSocketFactory.createServerSocket(0, 1,
                java.net.InetAddress.getLoopbackAddress()) as SSLServerSocket).use { server ->
                server.soTimeout = 3000
                server.enabledProtocols = arrayOf("TLSv1.2")
                val received = executor.submit<Int> {
                    try {
                        (server.accept() as SSLSocket).use { socket ->
                            socket.soTimeout = 3000
                            socket.startHandshake()
                            socket.inputStream.read()
                        }
                    } catch (_: java.io.IOException) { -1 }
                }
                try {
                    val context = ProvisionTls(if (matches) pin else ByteArray(32)).newClientContext()
                    (context.socketFactory.createSocket(server.inetAddress, server.localPort) as SSLSocket).use { client ->
                        client.soTimeout = 3000
                        client.enabledProtocols = arrayOf("TLSv1.2")
                        if (matches) {
                            client.startHandshake()
                            client.outputStream.write(90) // Synthetic test marker, never a credential.
                        } else {
                            assertThrows(SSLException::class.java) { client.startHandshake() }
                        }
                    }
                    assertEquals(if (matches) 90 else -1, received.get(5, TimeUnit.SECONDS))
                } finally {
                    executor.shutdownNow()
                }
            }
        }
    }
}
