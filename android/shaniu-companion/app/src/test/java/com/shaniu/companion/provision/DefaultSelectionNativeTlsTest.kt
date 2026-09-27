// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.io.File
import java.security.MessageDigest
import java.security.cert.CertificateFactory
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test

/** Named consumer: test_pack_trial.py android-default-tls-{save,cancel,recovery}.
 * That driver requires exactly one fresh, unskipped JUnit case. Ordinary JVM
 * runs without the compiled native fixture cannot supply integration evidence.
 * Only ATT delivery, scheduling and hardware sinks are externally controlled.
 */
class DefaultSelectionNativeTlsTest {
    private class Fixture : AutoCloseable {
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
        val controller: DefaultSelectionController
        private var nonce = 10

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
            controller = DefaultSelectionController(session,
                { ByteArray(16).also { it[0] = (++nonce).toByte() } }) {}
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
        private fun pump(done: () -> Boolean) {
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
        fun read(): DefaultSelectionController.Snapshot {
            assertTrue(controller.refresh()); pump { !controller.current().busy }
            return requireNotNull(controller.current().snapshot) { controller.current().message }
        }
        fun act(action: Int, name: String? = null): DefaultSelectionController.Snapshot {
            assertTrue(controller.act(action, name)); pump { !controller.current().busy }
            return requireNotNull(controller.current().snapshot) { controller.current().message }
        }
        fun stats() = external("stats").single().split(' ')
        fun prepare() {
            assertEquals(0, read().state)
            assertFalse(controller.canAct(1))
            assertEquals(1, act(2).state)
            external("step")
            val state = read()
            assertEquals(6, state.state); assertEquals(1uL, state.revision)
            assertEquals("shaniu-default-v1.bkep", state.filename)
        }
        override fun close() {
            try {
                controller.close(); session.disconnect()
                output.close()
                assertTrue(peer.waitFor(3, TimeUnit.SECONDS))
                assertEquals("native cleanup assertions", 0, peer.exitValue())
            } finally { peer.destroyForcibly(); input.close(); watchdog.shutdownNow() }
        }
    }

    @Test fun authenticatedNativeSavePreservesAckAndRenderBoundary() {
        Fixture().use { f ->
            f.prepare(); val before = f.stats()
            val pending = f.act(1, "shaniu-upload-v1.bkep")
            assertEquals(1, pending.state); assertFalse(pending.saved); assertFalse(pending.rendered)
            assertEquals(before, f.stats()) // ACK and queries did not touch the store or FB.
            f.external("step")
            val done = f.read(); val after = f.stats()
            assertEquals(6, done.state); assertTrue(done.saved && done.rendered)
            assertEquals(2uL, done.revision); assertEquals("shaniu-upload-v1.bkep", done.filename)
            assertEquals("2", after[5]); assertEquals(done.filename, after[6])
            assertEquals(before[2].toInt() + 2, after[2].toInt()) // Every pixel checked by native sink.
            repeat(3) { assertEquals(6, f.read().state) }
            assertEquals(after, f.stats())
        }
    }
    @Test fun confirmedNativeCancelDoesNotWriteOrRender() {
        Fixture().use { f ->
            f.prepare(); val before = f.stats()
            assertEquals(1, f.act(1, "shaniu-upload-v1.bkep").state)
            val canceled = f.act(3)
            assertEquals(7, canceled.state); assertFalse(canceled.saved || canceled.rendered)
            f.external("step"); assertEquals(7, f.read().state)
            assertEquals(before, f.stats())
            assertFalse(f.controller.canAct(3))
        }
    }
    @Test fun nativeReleaseFailureRemainsUnknownAfterRecovery() {
        Fixture().use { f ->
            f.prepare(); val before = f.stats()
            f.act(1, "shaniu-upload-v1.bkep")
            f.external("fail-unmount"); f.external("step")
            val unknown = f.read()
            assertEquals(9, unknown.state); assertTrue(unknown.saved); assertFalse(unknown.rendered)
            assertEquals(-5, unknown.releaseError); assertFalse(f.controller.canAct(1))
            f.act(4); f.external("step")
            val recovered = f.read(); val after = f.stats()
            assertEquals(9, recovered.state); assertEquals(0, recovered.releaseError)
            assertFalse(recovered.rendered); assertEquals(before[2], after[2])
            assertEquals(after[3], after[4]); assertEquals("2", after[5])
            f.act(2); f.external("step")
            val refreshed = f.read()
            assertEquals(6, refreshed.state); assertTrue(refreshed.refresh)
            assertEquals(2uL, refreshed.revision); assertFalse(refreshed.rendered)
        }
    }
}
