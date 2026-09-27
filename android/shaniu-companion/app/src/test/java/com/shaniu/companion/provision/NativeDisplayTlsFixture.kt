// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.io.File
import java.security.MessageDigest
import java.security.cert.CertificateFactory
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Assume.assumeTrue

/** Existing native display peer. Only host ATT/scheduling/hardware boundaries
 * are controlled; default and temporary-trial tests share real TLS and Session.
 * The named runner requires fresh, unskipped collection.
 */
internal open class NativeDisplayTlsFixture : AutoCloseable {
    val peer: Process
    private val watchdog = Executors.newSingleThreadScheduledExecutor { task ->
        Thread(task, "native-peer-timeout").apply { isDaemon = true }
    }
    private val input: java.io.BufferedReader
    private val output: java.io.BufferedWriter
    private val outgoing = ArrayDeque<ByteArray>()
    private val incoming = ArrayDeque<ByteArray>()
    private val posted = ArrayDeque<() -> Unit>()
    private val protocol: DeviceControlProtocol
    private val gatt: ProvisionGattSession
    val session = DeviceControlSession({ System.nanoTime() / 1000000 },
        { posted.add(it) }, { _, _ -> object : DeviceControlSession.Cancel {
            override fun cancel() {}
        } })

    init {
        val command = System.getenv("SHANIU_SELECTION_TLS_PEER")
        assumeTrue("native fixture is required", !command.isNullOrBlank())
        peer = ProcessBuilder(requireNotNull(command).split('\n'))
            .redirectError(ProcessBuilder.Redirect.INHERIT).start()
        watchdog.schedule({ peer.destroyForcibly() }, 30, TimeUnit.SECONDS)
        input = peer.inputStream.bufferedReader()
        output = peer.outputStream.bufferedWriter()
        assertEquals("READY", input.readLine())
        val cert = File(requireNotNull(System.getenv("SHANIU_TEST_CERT"))).inputStream().use {
            CertificateFactory.getInstance("X.509").generateCertificate(it)
        }
        val pin = MessageDigest.getInstance("SHA-256").digest(cert.encoded)
        lateinit var events: DeviceControlSession.Events
        protocol = DeviceControlProtocol(ByteArray(32).also { it[0] = 42 },
            { outgoing.add(it.copyOf()) }, { commandId, snapshot ->
                if (commandId == DeviceControlProtocol.Command.AUTH) {
                    assertEquals(0, snapshot.error)
                    assertTrue(protocolRequestStatus())
                } else events.result(commandId, snapshot)
            })
        gatt = ProvisionGattSession(ProvisionTls(pin), protocol::receive)
        session.setForeground(true)
        session.connect(object : DeviceControlSession.Factory {
            override fun open(e: DeviceControlSession.Events): DeviceControlSession.Transport {
                events = e
                return object : DeviceControlSession.Transport {
                    override fun request(c: DeviceControlProtocol.Command, v: Int, a: (Boolean) -> Unit) { a(protocol.request(c, v)) }
                    override fun requestPayload(c: DeviceControlProtocol.Command, p: ByteArray, a: (Boolean) -> Unit) { a(protocol.requestPayload(c, p)) }
                    override fun requestOta(c: DeviceControlProtocol.Command, p: ByteArray, a: (Boolean) -> Unit) { error("unexpected OTA") }
                    override fun close() { protocol.close(); gatt.close() }
                }
            }
        })
        gatt.start(); pump { gatt.established }
        events.peerIdentity(requireNotNull(gatt.peerIdentity))
        protocol.start(); pump { session.current().authenticated }
    }
    private fun protocolRequestStatus() = protocol.request(DeviceControlProtocol.Command.STATUS)
    fun external(line: String): List<String> {
        output.write(line); output.newLine(); output.flush()
        val evidence = mutableListOf<String>()
        while (true) {
            val reply = checkNotNull(input.readLine()) { "native peer closed" }
            if (reply == "READY") return evidence
            if (reply.startsWith("DATA ")) {
                incoming.add(reply.removePrefix("DATA ").chunked(2).map { it.toInt(16).toByte() }.toByteArray())
            } else evidence += reply
        }
    }
    fun pump(done: () -> Boolean) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
        while (!done()) {
            check(System.nanoTime() < deadline) { "TLS/control timed out" }
            while (posted.isNotEmpty()) posted.removeFirst().invoke()
            while (outgoing.isNotEmpty()) gatt.send(outgoing.removeFirst())
            val write = gatt.nextWrite()
            if (write == null) external("poll") else {
                external("wire " + write.value.joinToString("") { "%02x".format(it.toInt() and 255) })
                gatt.writeCompleted(gatt.generation, write.token, true)
            }
            while (incoming.isNotEmpty()) gatt.enqueueIncoming(gatt.generation, incoming.removeFirst())
            gatt.processInput(); gatt.tick(); protocol.tick()
        }
    }
    fun stats() = external("stats").single().split(' ')
    override fun close() {
        try {
            session.disconnect()
            output.close()
            assertTrue(peer.waitFor(3, TimeUnit.SECONDS))
            assertEquals("native cleanup assertions", 0, peer.exitValue())
        } finally { peer.destroyForcibly(); input.close(); watchdog.shutdownNow() }
    }
}

