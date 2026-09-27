// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import org.junit.Assert.*
import org.junit.Test

/** Real controller + Session + SDC1 codec. Only the remote device is a peer. */
class PcAuthorizationControllerTest {
    private class Fixture(resume: String? = null, target: PcAuthorizationController.Target? = null) {
        lateinit var events: DeviceControlSession.Events
        lateinit var protocol: DeviceControlProtocol
        val frames = mutableListOf<ByteArray>()
        val session = DeviceControlSession({ 1000L }, { it() }, { _, _ -> object : DeviceControlSession.Cancel { override fun cancel() {} } })
        val controller: PcAuthorizationController
        val tx = ByteArray(16) { 7 }
        val observed = mutableListOf<PcAuthorizationController.State>()
        init {
            session.setForeground(true)
            session.connect(object : DeviceControlSession.Factory {
                override fun open(e: DeviceControlSession.Events): DeviceControlSession.Transport {
                    events = e
                    protocol = DeviceControlProtocol(ByteArray(32) { 42 }, { frames += it.copyOf() }, { c, s ->
                        if (c != DeviceControlProtocol.Command.AUTH) e.result(c, s)
                    })
                    return object : DeviceControlSession.Transport {
                        override fun request(c: DeviceControlProtocol.Command, v: Int, a: (Boolean) -> Unit) { a(protocol.request(c, v)) }
                        override fun requestOta(c: DeviceControlProtocol.Command, p: ByteArray, a: (Boolean) -> Unit) { error("not OTA") }
                        override fun requestPayload(c: DeviceControlProtocol.Command, p: ByteArray, a: (Boolean) -> Unit) { a(protocol.requestPayload(c, p)) }
                        override fun close() { protocol.close() }
                    }
                }
            })
            protocol.start(); reply()
            protocol.request(DeviceControlProtocol.Command.STATUS); reply()
            controller = PcAuthorizationController(session, { tx.copyOf() }, resume, target) { observed += it }
        }
        fun command() = ByteBuffer.wrap(frames.last()).getInt(4)
        fun reply(error: Int = 0, total: Int = 0, chunk: ByteArray? = null) {
            val request = ByteBuffer.wrap(frames.last())
            val frame = ByteBuffer.allocate(40).putInt(0x53444331)
                .putInt(request.getInt(4) or Int.MIN_VALUE).putInt(request.getInt(8)).putInt(24)
                .putInt(error).putInt(total)
            if (chunk != null) frame.put(chunk)
            else frame.putInt(-1).putInt(-1).putInt(-1).putInt(0)
            frame.array().toList().chunked(5).forEach { protocol.receive(it.toByteArray()) }
        }
        fun view(active: Boolean = true, revision: Long = 5, transaction: ByteArray = ByteArray(16)) =
            ByteBuffer.allocate(64).put("PCS1".toByteArray()).putInt(if (active) 1 else 0)
                .putLong(3).putLong(revision).putInt(if (active) 3 else 0).putInt(0)
                .put(ByteArray(16) { if (active) 9 else 0 }).put(transaction).array()
        fun read(value: ByteArray = view()) {
            for (offset in listOf(0, 16, 32, 48, 0, 16)) {
                assertEquals(15, command())
                assertEquals((14 shl 16) or offset, ByteBuffer.wrap(frames.last()).getInt(16))
                reply(total = 64, chunk = value.copyOfRange(offset, offset + 16))
            }
        }
        fun loaded() { assertTrue(controller.refresh()); read() }
        fun apply() {
            val snapshot = controller.current().snapshot!!
            assertTrue(controller.revoke(snapshot)); assertEquals(16, command())
            assertNull(controller.current().transaction) // Saved state must not retain unsubmitted staging.
            assertNull(observed.last().transaction)
            reply()
            val record = mutableListOf<Byte>()
            repeat(3) { assertEquals(17, command()); record += frames.last().drop(16); reply() }
            assertEquals(88, record.size)
            assertEquals("PCW1", String(record.take(4).toByteArray()))
            assertEquals(3L, ByteBuffer.wrap(record.toByteArray()).getLong(4))
            assertEquals(5L, ByteBuffer.wrap(record.toByteArray()).getLong(12))
            assertArrayEquals(tx, record.subList(20, 36).toByteArray())
            assertTrue(record.drop(36).all { it == 0.toByte() })
            assertEquals(18, command())
            assertEquals("07".repeat(16), observed.last().transaction) // Save before an APPLY response can be lost.
            reply(error = -11)
        }
        fun receipt(phase: Int, error: Int = 0, id: ByteArray = tx) {
            val data = ByteBuffer.allocate(32).put("PCR1".toByteArray()).putInt(error).putInt(phase).putInt(0).put(id).array()
            for (offset in listOf(0, 16)) {
                assertEquals(15, command()); assertEquals(36, frames.last().size)
                assertArrayEquals(tx, frames.last().copyOfRange(20, 36))
                reply(total = 32, chunk = data.copyOfRange(offset, offset + 16))
            }
        }
    }
    @Test fun revokeNeedsDurableReceiptAndMatchingReadback() {
        val f = Fixture(); f.loaded(); f.apply()
        f.receipt(1, -11)
        assertEquals(PcAuthorizationController.Outcome.PENDING, f.controller.current().outcome)
        assertFalse(f.controller.current().busy)
        assertTrue(f.controller.query()); f.receipt(2)
        assertTrue(f.controller.current().busy)
        assertNotEquals(PcAuthorizationController.Outcome.CONFIRMED, f.controller.current().outcome)
        f.read(f.view(false, 6, f.tx))
        assertEquals(PcAuthorizationController.Outcome.CONFIRMED, f.controller.current().outcome)
        assertFalse(f.controller.current().snapshot!!.active)
    }
    @Test fun knownEagainFailureDoesNotBecomePendingOrReplay() {
        val f = Fixture(); f.loaded(); f.apply(); val count = f.frames.size
        f.receipt(3, -11)
        assertEquals(PcAuthorizationController.Outcome.FAILED, f.controller.current().outcome)
        assertFalse(f.controller.current().busy)
        assertEquals(count + 1, f.frames.size)
    }
    @Test fun uncertainReceiptAndMismatchedReadbackCannotConfirm() {
        val f = Fixture(); f.loaded(); f.apply(); f.receipt(4, -115)
        assertEquals(PcAuthorizationController.Outcome.UNKNOWN, f.controller.current().outcome)
        assertTrue(f.controller.query()); f.receipt(2); f.read(f.view(true, 6, f.tx))
        assertEquals(PcAuthorizationController.Outcome.UNKNOWN, f.controller.current().outcome)
    }
    @Test fun mismatchedReceiptCannotConfirmAnotherTransaction() {
        val f = Fixture(); f.loaded(); f.apply(); f.receipt(2, id = ByteArray(16) { 8 })
        assertEquals(PcAuthorizationController.Outcome.UNKNOWN, f.controller.current().outcome)
        assertNull(f.controller.current().snapshot)
        assertFalse(f.controller.current().busy) // A wrong transaction must be rejected, not followed by a view read.
    }
    @Test fun changingEitherRevisionRejectsMixedSnapshot() {
        for (field in listOf(8, 16)) {
            val f = Fixture(); assertTrue(f.controller.refresh())
            val data = f.view()
            for (offset in listOf(0, 16, 32, 48)) f.reply(total = 64, chunk = data.copyOfRange(offset, offset + 16))
            ByteBuffer.wrap(data).putLong(field, 99)
            f.reply(total = 64, chunk = data.copyOfRange(0, 16))
            if (field == 16) f.reply(total = 64, chunk = data.copyOfRange(16, 32))
            assertNull(f.controller.current().snapshot)
            assertFalse(f.controller.current().busy)
        }
    }
    @Test fun disconnectLateAckAndRecreationNeverReplayWrite() {
        val unstaged = Fixture(); unstaged.loaded()
        assertTrue(unstaged.controller.revoke(unstaged.controller.current().snapshot!!))
        unstaged.events.closed("lost before APPLY")
        assertNull(unstaged.controller.current().transaction)
        assertEquals(PcAuthorizationController.Outcome.NONE, unstaged.controller.current().outcome)
        val f = Fixture(); f.loaded(); f.apply(); val id = f.controller.current().transaction!!
        f.events.closed("lost"); val count = f.frames.size
        f.events.result(DeviceControlProtocol.Command.CONFIG_APPLY, DeviceControlProtocol.Snapshot(0, false, false, null, null, null, null))
        assertEquals(count, f.frames.size)
        assertEquals(PcAuthorizationController.Outcome.UNKNOWN, f.controller.current().outcome)
        f.controller.close()
        val reopened = Fixture(id); assertTrue(reopened.controller.query()); reopened.receipt(1, -11)
        assertFalse(reopened.frames.any { ByteBuffer.wrap(it).getInt(4) in 16..18 })
    }
    @Test fun closeBeforeApplyCancelsStagingButAfterApplyDoesNotClaimRemoteCancel() {
        val f = Fixture(); f.loaded(); assertTrue(f.controller.revoke(f.controller.current().snapshot!!))
        f.controller.close(); assertNull(f.controller.current().transaction)
        f.reply(); assertEquals(19, f.command())
        val committed = Fixture(); committed.loaded(); committed.apply(); val count = committed.frames.size
        committed.controller.close(); assertEquals(count, committed.frames.size)
        assertFalse(committed.frames.any { ByteBuffer.wrap(it).getInt(4) == 19 })
    }
    @Test fun staleConfirmationAndOtherEditorsTransactionAreNotOverwritten() {
        val f = Fixture(); f.loaded(); val original = f.controller.current().snapshot!!
        assertTrue(f.controller.refresh()); f.read(f.view(revision = 6))
        assertFalse(f.controller.revoke(original))
        assertTrue(f.session.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN, ByteBuffer.allocate(8).putInt(10).putInt(32).array()))
        assertFalse(f.controller.revoke(f.controller.current().snapshot!!))
        assertNull(f.controller.current().transaction) // Rejected BEGIN never reached the device.
        f.reply()
        assertFalse(f.frames.any { ByteBuffer.wrap(it).getInt(4) == 19 })
        assertTrue(f.session.requestPayload(DeviceControlProtocol.Command.CONFIG_APPEND, ByteArray(32)))
    }
    @Test fun unsupportedAndInvalidSnapshotsCannotEnableRevoke() {
        val f = Fixture(); assertTrue(f.controller.refresh()); f.reply(error = -95)
        assertNull(f.controller.current().snapshot)
        assertFalse(f.controller.current().busy)
        for (field in listOf(4, 24, 28)) {
            val p = Fixture(); assertTrue(p.controller.refresh())
            val data = p.view(); ByteBuffer.wrap(data).putInt(field, 99)
            p.read(data); assertNull(p.controller.current().snapshot)
        }
    }
    @Test fun grantUsesIndependentCredentialAndRequiresExactDurableReadback() {
        val f = Fixture(); assertTrue(f.controller.refresh()); f.read(f.view(false))
        val client = ByteArray(16) { 12 }; val key = ByteArray(32) { 84 }
        assertTrue(f.controller.grant(f.controller.current().snapshot!!, client, key, 3))
        client.fill(0); key.fill(0) // Caller ownership cannot change the queued request.
        f.reply()
        val bytes = mutableListOf<Byte>()
        repeat(3) { bytes += f.frames.last().drop(16); f.reply() }
        val record = bytes.toByteArray()
        assertEquals(88, record.size)
        assertEquals("PCW1", String(record.copyOfRange(0, 4)))
        assertEquals(3L, ByteBuffer.wrap(record).getLong(4))
        assertEquals(5L, ByteBuffer.wrap(record).getLong(12))
        assertArrayEquals(f.tx, record.copyOfRange(20, 36))
        assertArrayEquals(ByteArray(16) { 12 }, record.copyOfRange(36, 52))
        assertArrayEquals(ByteArray(32) { 84 }, record.copyOfRange(52, 84))
        assertEquals(3, ByteBuffer.wrap(record).getInt(84))
        assertFalse(f.observed.toString().contains("54".repeat(32)))
        f.reply(); f.receipt(2)
        assertNotEquals(PcAuthorizationController.Outcome.CONFIRMED, f.controller.current().outcome)
        val view = f.view(true, 6, f.tx); ByteArray(16) { 12 }.copyInto(view, 32)
        f.read(view)
        assertEquals(PcAuthorizationController.Outcome.CONFIRMED, f.controller.current().outcome)
        assertTrue(f.controller.current().snapshot!!.active)
    }
    @Test fun grantRejectsWrongReadbackClientCapabilitiesAndRevision() {
        for (field in listOf(16, 24, 32, 48)) {
            val f = Fixture(); f.loaded()
            assertTrue(f.controller.grant(f.controller.current().snapshot!!, ByteArray(16) { 9 }, ByteArray(32) { 84 }, 3))
            f.reply(); repeat(3) { f.reply() }; f.reply(); f.receipt(2)
            val view = f.view(true, 6, f.tx)
            view[field] = (view[field].toInt() xor 1).toByte()
            f.read(view)
            assertNotEquals(PcAuthorizationController.Outcome.CONFIRMED, f.controller.current().outcome)
        }
    }
    @Test fun invalidGrantInputsNeverBeginOrChangeBorrowedKey() {
        val f = Fixture(); f.loaded(); val before = f.frames.size
        val key = ByteArray(32) { 84 }; val client = ByteArray(16) { 9 }
        for (caps in listOf(0, 16, -1)) assertFalse(f.controller.grant(f.controller.current().snapshot!!, client, key, caps))
        assertFalse(f.controller.grant(f.controller.current().snapshot!!, ByteArray(16), key, 3))
        assertFalse(f.controller.grant(f.controller.current().snapshot!!, client, ByteArray(32), 3))
        assertFalse(f.controller.grant(f.controller.current().snapshot!!, client, ByteArray(31), 3))
        assertEquals(before, f.frames.size)
        assertArrayEquals(ByteArray(32) { 84 }, key)
    }
    @Test fun grantCloseAndDisconnectDoNotReplayOrExposeCredential() {
        val f = Fixture(); f.loaded()
        assertTrue(f.controller.grant(f.controller.current().snapshot!!, ByteArray(16) { 9 }, ByteArray(32) { 84 }, 3))
        f.reply(); repeat(3) { f.reply() }
        val id = f.controller.current().transaction!!
        f.events.closed("lost after APPLY"); val count = f.frames.size
        assertEquals(PcAuthorizationController.Outcome.UNKNOWN, f.controller.current().outcome)
        f.controller.close(); assertEquals(count, f.frames.size)
        val restored = Fixture(id); assertTrue(restored.controller.query()); restored.receipt(2)
        restored.read(restored.view(true, 6, restored.tx))
        assertNotEquals(PcAuthorizationController.Outcome.CONFIRMED, restored.controller.current().outcome)
        assertFalse(restored.frames.any { ByteBuffer.wrap(it).getInt(4) in 16..18 })
    }

    @Test fun restoredPublicTargetConfirmsOnlyMatchingGrantWithoutReplaying() {
        val f = Fixture(); f.loaded()
        assertTrue(f.controller.grant(f.controller.current().snapshot!!, ByteArray(16) { 9 }, ByteArray(32) { 84 }, 3))
        assertNull(f.controller.current().target)
        f.reply(); repeat(3) { f.reply() }
        val pending = f.controller.current(); assertNotNull(pending.target)
        f.controller.close()
        val restored = Fixture(pending.transaction, pending.target)
        assertTrue(restored.controller.query()); restored.receipt(2)
        restored.read(restored.view(true, 6, restored.tx))
        assertEquals(PcAuthorizationController.Outcome.CONFIRMED, restored.controller.current().outcome)
        assertFalse(restored.frames.any { ByteBuffer.wrap(it).getInt(4) in 16..18 })
    }
    private fun pairingRequest(): PcPairingExchange.Request {
        val pair = java.security.KeyPairGenerator.getInstance("RSA").apply { initialize(3072) }.generateKeyPair()
        val der = pair.public.encoded
        return PcPairingExchange.parse(ByteBuffer.allocate(60 + der.size).putInt(0x53505131).putInt(3)
            .putLong(1800000000000L).putLong(1800000600000L).put(ByteArray(16) { 9 })
            .put(ByteArray(16) { 7 }).putInt(der.size).put(der).array(), 1800000000000L)
    }
    private fun pairingIdentity(): ProvisionPeerIdentity = javaClass.getResourceAsStream("/pc-identity.pem")!!.use {
        ProvisionPeerIdentity.fromDer(java.security.cert.CertificateFactory.getInstance("X.509").generateCertificate(it).encoded)
    }
    @Test fun pairingDeliveryRequiresSavedCiphertextAndDurableExactGrant() {
        val f = Fixture(); f.loaded(); val identity = pairingIdentity()
        var delivery: PcPairingDelivery? = null
        PcPairingDelivery.prepare(pairingRequest(), identity, f.controller.current().snapshot!!, 1800000000000L).use { prepared ->
            assertTrue(prepared.submit(f.controller, 1800000000001L) { delivery = PcPairingDelivery.decode(it.encode()); true })
            assertFalse(prepared.submit(f.controller, 1800000000001L) { fail("must not resubmit"); true })
        }
        val saved = delivery!!
        assertNull(saved.response(f.controller.current(), identity, 1800000000001L))
        f.reply(); repeat(3) { f.reply() }; f.reply(); f.receipt(2)
        assertNull(saved.response(f.controller.current(), identity, 1800000000001L))
        f.read(f.view(true, 6, f.tx))
        assertNotNull(saved.response(f.controller.current(), identity, 1800000000001L))
        val confirmed = f.controller.current()
        assertNull(saved.response(confirmed.copy(target = confirmed.target!!.copy(revision = 7u)), identity, 1800000000001L))
        assertNull(saved.response(confirmed.copy(snapshot = confirmed.snapshot!!.copy(capabilities = 1)), identity, 1800000000001L))
        assertNull(saved.response(f.controller.current(), null, 1800000000001L))
        assertNull(saved.response(f.controller.current(), identity, 1800000600000L))
        assertNull(saved.response(f.controller.current(), identity, 1799999999999L))
        f.events.closed("lost"); assertNull(saved.response(f.controller.current(), identity, 1800000000001L))
    }
    @Test fun pairingSaveFailureCannotStartGrantAndClosedPreparationCannotReuseKey() {
        val f = Fixture(); f.loaded(); val before = f.frames.size
        val prepared = PcPairingDelivery.prepare(pairingRequest(), pairingIdentity(), f.controller.current().snapshot!!, 1800000000000L)
        assertFalse(prepared.submit(f.controller, 1800000000001L) { false })
        assertEquals(before, f.frames.size)
        prepared.close()
        assertFalse(prepared.submit(f.controller, 1800000000001L) { fail("closed key must not return"); true })
    }
    @Test fun restoredPairingOnlyQueriesAndCannotExportUnconfirmedOrWrongTarget() {
        val f = Fixture(); f.loaded(); val identity = pairingIdentity(); var delivery: PcPairingDelivery? = null
        PcPairingDelivery.prepare(pairingRequest(), identity, f.controller.current().snapshot!!, 1800000000000L).use {
            assertTrue(it.submit(f.controller, 1800000000001L) { value -> delivery = PcPairingDelivery.decode(value.encode()); true })
        }
        f.reply(); repeat(3) { f.reply() }; f.controller.close()
        val saved = delivery!!; val restored = Fixture(saved.transaction, saved.target)
        assertTrue(restored.controller.refresh()); restored.read(restored.view(true, 6, restored.tx))
        assertNull(saved.response(restored.controller.current(), identity, 1800000000001L))
        assertTrue(restored.controller.query()); restored.receipt(2); restored.read(restored.view(true, 6, restored.tx))
        assertNotNull(saved.response(restored.controller.current(), identity, 1800000000001L))
        assertFalse(restored.frames.any { ByteBuffer.wrap(it).getInt(4) in 16..18 })
        val raw = saved.encode()
        for (invalid in listOf(raw.copyOf(10), raw + byteArrayOf(0), raw.copyOf().also { it[0] = 0 }))
            assertThrows(IllegalArgumentException::class.java) { PcPairingDelivery.decode(invalid) }
    }
    @Test fun pairingExpiredPreparationCannotSaveOrSendAndNonceIsNotReplaced() {
        val f = Fixture(); f.loaded(); val before = f.frames.size
        PcPairingDelivery.prepare(pairingRequest(), pairingIdentity(), f.controller.current().snapshot!!, 1800000000000L).use {
            assertFalse(it.submit(f.controller, 1800000600000L) { fail("expired delivery cannot be saved"); true })
        }
        assertEquals(before, f.frames.size)
        val nonce = ByteArray(16) { 8 }
        assertTrue(f.controller.grant(f.controller.current().snapshot!!, ByteArray(16) { 9 }, ByteArray(32) { 84 }, 3, nonce))
        nonce.fill(0); f.reply()
        val record = mutableListOf<Byte>()
        repeat(3) { record += f.frames.last().drop(16); f.reply() }
        assertArrayEquals(ByteArray(16) { 8 }, record.subList(20, 36).toByteArray())
        f.controller.close()
    }
    @Test fun restoredRevokeTargetAndInvalidMetadataStaySeparate() {
        val f = Fixture(); f.loaded(); f.apply(); val pending = f.controller.current()
        val restored = Fixture(pending.transaction, pending.target)
        assertTrue(restored.controller.query()); restored.receipt(2); restored.read(restored.view(false, 6, restored.tx))
        assertEquals(PcAuthorizationController.Outcome.CONFIRMED, restored.controller.current().outcome)
        val invalid = Fixture(pending.transaction, PcAuthorizationController.Target("00".repeat(16), 3, 6u))
        assertNull(invalid.controller.current().target)
        assertTrue(invalid.controller.query()); invalid.receipt(2); invalid.read(invalid.view(false, 6, invalid.tx))
        assertEquals(PcAuthorizationController.Outcome.UNKNOWN, invalid.controller.current().outcome)
    }

}
