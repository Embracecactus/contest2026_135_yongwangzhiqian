// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.file.Files
import java.security.MessageDigest
import java.security.cert.CertificateFactory
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import javax.net.ssl.SSLException
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test

/** Product JVM TLS + GATT queues against the product C mbedTLS control pair.
 * Pipes replace only physical ATT delivery/acknowledgement. No real device data.
 */
class DeviceControlTlsInteropTest {
    @Test fun pinnedTlsCarriesControlAcrossTwentyByteTransport() {
        val executable = System.getenv("SHANIU_CONTROL_TLS_PEER")
        assumeTrue("Requires mbedTLS host peer; see Android README", !executable.isNullOrBlank())
        val directory = Files.createTempDirectory("shaniu-control-tls-")
        try {
            val certificate = directory.resolve("cert.pem")
            val key = directory.resolve("key.pem")
            val generation = ProcessBuilder("openssl", "req", "-x509", "-newkey", "ec",
                "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes", "-keyout", key.toString(),
                "-out", certificate.toString(), "-subj", "/CN=control-test", "-days", "1",
                "-addext", "keyUsage=digitalSignature", "-addext", "extendedKeyUsage=serverAuth")
                .redirectOutput(directory.resolve("openssl.log").toFile())
                .redirectErrorStream(true).start()
            try {
                assertTrue(generation.waitFor(10, TimeUnit.SECONDS))
                assertEquals(0, generation.exitValue())
            } finally { generation.destroyForcibly() }
            val cert = Files.newInputStream(certificate).use {
                CertificateFactory.getInstance("X.509").generateCertificate(it)
            }
            val pin = MessageDigest.getInstance("SHA-256").digest(cert.encoded)

            fun exchange(trust: ByteArray) {
                val peer = ProcessBuilder(requireNotNull(executable), "--control-peer",
                    certificate.toString(), key.toString()).redirectError(ProcessBuilder.Redirect.INHERIT).start()
                val incoming = ArrayBlockingQueue<ByteArray>(512)
                val executor = Executors.newSingleThreadExecutor()
                val reader = executor.submit {
                    try {
                        val bytes = ByteArray(20)
                        while (true) {
                            val size = peer.inputStream.read(bytes)
                            if (size < 0) break
                            if (size > 0) check(incoming.offer(bytes.copyOf(size), 3, TimeUnit.SECONDS))
                        }
                    } finally { incoming.offer(byteArrayOf()) }
                }
                val outgoing = ArrayDeque<ByteArray>()
                val replies = mutableListOf<Pair<DeviceControlProtocol.Command, DeviceControlProtocol.Snapshot>>()
                val owner = ByteArray(32).also { it[0] = 42 }
                val protocol = DeviceControlProtocol(owner, { outgoing.add(it.copyOf()) },
                    { command, snapshot -> replies += command to snapshot })
                owner.fill(0)
                val session = ProvisionGattSession(ProvisionTls(trust), protocol::receive)
                fun pumpUntil(done: () -> Boolean) {
                    val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
                    while (!done()) {
                        check(System.nanoTime() < deadline) { "TLS/control exchange timed out" }
                        while (outgoing.isNotEmpty()) {
                            val message = outgoing.removeFirst()
                            try { session.send(message) } finally { message.fill(0) }
                        }
                        session.nextWrite()?.let {
                            peer.outputStream.write(it.value); peer.outputStream.flush()
                            session.writeCompleted(session.generation, it.token, true)
                        }
                        incoming.poll(2, TimeUnit.MILLISECONDS)?.let {
                            check(it.isNotEmpty()) { "Native TLS peer closed" }
                            session.enqueueIncoming(session.generation, it)
                        }
                        session.processInput(); session.tick(); protocol.tick()
                    }
                }
                try {
                    assertNull(session.peerIdentity)
                    session.start()
                    pumpUntil { session.established }
                    protocol.start()
                    pumpUntil { protocol.authenticated }
                    val identity = requireNotNull(session.peerIdentity)
                    assertEquals(pin.joinToString("") { "%02x".format(it.toInt() and 255) }, identity.sha256)
                    assertArrayEquals(cert.encoded, CertificateFactory.getInstance("X.509")
                        .generateCertificate(identity.certificatePem.byteInputStream()).encoded)
                    assertEquals(DeviceControlProtocol.Command.AUTH, replies.single().first)
                    assertTrue(protocol.request(DeviceControlProtocol.Command.VOLUME, 73))
                    pumpUntil { replies.size == 2 }
                    assertEquals(DeviceControlProtocol.Command.VOLUME, replies.last().first)
                    assertEquals(0, replies.last().second.error)
                    assertEquals(73, replies.last().second.volume)
                    peer.outputStream.close()
                    assertTrue(peer.waitFor(3, TimeUnit.SECONDS))
                    assertEquals(0, peer.exitValue())
                } finally {
                    protocol.close(); session.close(); assertNull(session.peerIdentity)
                    outgoing.forEach { it.fill(0) }
                    peer.destroyForcibly(); peer.inputStream.close(); peer.outputStream.close()
                    reader.cancel(true); executor.shutdownNow()
                }
            }
            exchange(pin)
            val wrongPin = pin.copyOf().also { it[0] = (it[0].toInt() xor 1).toByte() }
            assertThrows(SSLException::class.java) { exchange(wrongPin) }
        } finally { directory.toFile().deleteRecursively() }
    }
}
