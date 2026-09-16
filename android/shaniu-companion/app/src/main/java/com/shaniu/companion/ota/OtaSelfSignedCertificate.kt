// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

import java.io.ByteArrayInputStream
import java.math.BigInteger
import java.security.PrivateKey
import java.security.PublicKey
import java.security.Signature
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/** Minimal X.509 v3 leaf used only by the temporary authenticated OTA source. */
internal object OtaSelfSignedCertificate {
    private const val ECDSA_SHA256_OID = "1.2.840.10045.4.3.2"
    private const val COMMON_NAME_OID = "2.5.4.3"
    private const val SUBJECT_ALT_NAME_OID = "2.5.29.17"

    fun create(
        publicKey: PublicKey,
        signingKey: PrivateKey,
        hostname: String,
        serial: BigInteger,
        notBefore: Date,
        notAfter: Date
    ): X509Certificate {
        require(hostname.length in 1..253 &&
            hostname.matches(Regex("[a-z0-9]+(?:[.-][a-z0-9]+)*")))
        require(serial.signum() > 0 && notBefore.before(notAfter))
        val algorithm = sequence(oid(ECDSA_SHA256_OID))
        val name = sequence(set(sequence(oid(COMMON_NAME_OID), utf8(hostname))))
        val validity = sequence(utcTime(notBefore), utcTime(notAfter))
        val sanNames = sequence(tagged(0x82, hostname.toByteArray(Charsets.US_ASCII)))
        val extensions = explicit(3, sequence(sequence(
            oid(SUBJECT_ALT_NAME_OID), octetString(sanNames))))
        val tbs = sequence(
            explicit(0, integer(BigInteger.valueOf(2))),
            integer(serial),
            algorithm,
            name,
            validity,
            name,
            publicKey.encoded,
            extensions
        )
        val signature = Signature.getInstance("SHA256withECDSA").run {
            initSign(signingKey)
            update(tbs)
            sign()
        }
        val encoded = sequence(tbs, algorithm, bitString(signature))
        val certificate = CertificateFactory.getInstance("X.509")
            .generateCertificate(ByteArrayInputStream(encoded)) as X509Certificate
        certificate.verify(publicKey)
        certificate.checkValidity(Date())
        check(hasDnsName(certificate, hostname))
        return certificate
    }

    fun hasDnsName(certificate: X509Certificate, hostname: String): Boolean =
        certificate.subjectAlternativeNames.orEmpty().any { name ->
            name.size >= 2 && name[0] == 2 && name[1] == hostname
        }

    private fun sequence(vararg values: ByteArray): ByteArray =
        tagged(0x30, concatenate(values))

    private fun set(vararg values: ByteArray): ByteArray =
        tagged(0x31, concatenate(values))

    private fun explicit(number: Int, value: ByteArray): ByteArray {
        require(number in 0..30)
        return tagged(0xa0 or number, value)
    }

    private fun integer(value: BigInteger): ByteArray {
        require(value.signum() >= 0)
        return tagged(0x02, value.toByteArray())
    }

    private fun utf8(value: String): ByteArray =
        tagged(0x0c, value.toByteArray(Charsets.UTF_8))

    private fun utcTime(value: Date): ByteArray {
        val formatter = SimpleDateFormat("yyMMddHHmmss'Z'", Locale.US)
        formatter.timeZone = TimeZone.getTimeZone("UTC")
        return tagged(0x17, formatter.format(value).toByteArray(Charsets.US_ASCII))
    }

    private fun octetString(value: ByteArray): ByteArray = tagged(0x04, value)

    private fun bitString(value: ByteArray): ByteArray =
        tagged(0x03, byteArrayOf(0) + value)

    private fun oid(value: String): ByteArray {
        val arcs = value.split('.').map { it.toLong() }
        require(arcs.size >= 2 && arcs[0] in 0..2 && arcs[1] >= 0)
        if (arcs[0] < 2) require(arcs[1] <= 39)
        val encoded = ArrayList<Byte>()
        appendOidArc(encoded, arcs[0] * 40 + arcs[1])
        arcs.drop(2).forEach { appendOidArc(encoded, it) }
        return tagged(0x06, encoded.toByteArray())
    }

    private fun appendOidArc(output: MutableList<Byte>, value: Long) {
        require(value >= 0)
        var remaining = value
        val encoded = ByteArray(10)
        var offset = encoded.size
        encoded[--offset] = (remaining and 0x7f).toByte()
        remaining = remaining ushr 7
        while (remaining != 0L) {
            encoded[--offset] = ((remaining and 0x7f) or 0x80).toByte()
            remaining = remaining ushr 7
        }
        for (index in offset until encoded.size) output += encoded[index]
    }

    private fun tagged(tag: Int, value: ByteArray): ByteArray =
        byteArrayOf(tag.toByte()) + length(value.size) + value

    private fun length(value: Int): ByteArray {
        require(value >= 0)
        if (value < 128) return byteArrayOf(value.toByte())
        var remaining = value
        val bytes = ByteArray(4)
        var offset = bytes.size
        while (remaining != 0) {
            bytes[--offset] = remaining.toByte()
            remaining = remaining ushr 8
        }
        val count = bytes.size - offset
        return byteArrayOf((0x80 or count).toByte()) + bytes.copyOfRange(offset, bytes.size)
    }

    private fun concatenate(values: Array<out ByteArray>): ByteArray {
        val size = values.sumOf { it.size }
        val output = ByteArray(size)
        var offset = 0
        values.forEach { value ->
            value.copyInto(output, offset)
            offset += value.size
        }
        return output
    }
}
