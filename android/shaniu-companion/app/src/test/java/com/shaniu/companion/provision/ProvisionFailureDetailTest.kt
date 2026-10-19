// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.provision

import java.security.cert.CertificateException
import java.security.cert.CertificateExpiredException
import java.security.cert.CertificateNotYetValidException
import javax.net.ssl.SSLException
import javax.net.ssl.SSLHandshakeException
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class ProvisionFailureDetailTest {
    @Test fun wrappedHandshakeReportsOnlyPinMismatchCode() {
        val error = SSLHandshakeException("handshake failed").apply {
            initCause(CertificateException("Provisioning certificate mismatch"))
        }
        assertEquals("certificate_pin_mismatch", ProvisionGattSession.classifyFailure(error))
    }

    @Test fun wrappedHandshakeReportsExpiredCertificateCode() {
        val error = SSLHandshakeException("handshake failed").apply {
            initCause(CertificateExpiredException())
        }
        assertEquals("certificate_expired", ProvisionGattSession.classifyFailure(error))
    }

    @Test fun wrappedHandshakeReportsNotYetValidCertificateCode() {
        val error = SSLHandshakeException("handshake failed").apply {
            initCause(CertificateNotYetValidException())
        }
        assertEquals("certificate_not_yet_valid", ProvisionGattSession.classifyFailure(error))
    }

    @Test fun providerMessageIsNeverReturned() {
        val secret = "provider detail: token=must-not-leak"
        val detail = ProvisionGattSession.classifyFailure(SSLException(secret))
        assertEquals("tls_provider_error", detail)
        assertFalse(detail.contains(secret))
    }

    @Test fun cyclicCauseIsBounded() {
        assertEquals("operation_failed", ProvisionGattSession.classifyFailure(CyclicCause()))
    }

    @Test fun fixedTlsProgressLimitHasStableCode() {
        assertEquals(
            "tls_progress_limit",
            ProvisionGattSession.classifyFailure(SSLException("TLS progress limit")),
        )
    }

    private class CyclicCause : RuntimeException() {
        override val cause: Throwable get() = this
    }
}
