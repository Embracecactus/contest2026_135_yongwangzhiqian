// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import com.google.gson.Gson
import com.google.gson.Strictness
import com.google.gson.stream.JsonReader
import com.google.gson.stream.JsonToken
import java.io.CharArrayReader
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import java.util.Base64

/** Supplied with the physical device, never downloaded from a BLE advertisement.
 * One package supplies ownership proof and the authenticated service route.
 * It contains no device private key or model-provider key.
 */
class ProvisionActivation private constructor(
    private var bootstrap: ProvisionBootstrap?,
    private val ca: ByteArray?,
    val host: String?,
    val address: String?,
    val port: Int?,
) : AutoCloseable {
    fun takeBootstrap(): ProvisionBootstrap = checkNotNull(bootstrap).also { bootstrap = null }
    fun caCopy(): ByteArray? = ca?.copyOf()
    val hasLegacyRoute: Boolean get() = ca != null && host != null && address != null && port != null
    override fun close() { bootstrap?.close(); bootstrap = null; ca?.fill(0) }
    override fun toString() = "ProvisionActivation(redacted)"

    companion object {
        const val MAX_BYTES = 8192
        private val fields = setOf("protocol", "device_id", "certificate_sha256", "possession_secret",
                                   "gateway_host", "gateway_ipv4", "gateway_port", "gateway_ca_der")
        fun parse(input: CharArray): ProvisionActivation {
            var bootstrap: ProvisionBootstrap? = null
            var ca: ByteArray? = null
            try {
                require(input.size in 1..MAX_BYTES)
                val values = mutableMapOf<String, String>()
                JsonReader(CharArrayReader(input)).use { reader ->
                    reader.strictness = Strictness.STRICT
                    reader.beginObject()
                    while (reader.hasNext()) {
                        val name = reader.nextName()
                        require(name in fields && name !in values && reader.peek() == JsonToken.STRING)
                        values[name] = reader.nextString()
                    }
                    reader.endObject()
                    require(reader.peek() == JsonToken.END_DOCUMENT)
                }
                val protocol = values["protocol"]
                val bootstrapFields = setOf("protocol", "device_id", "certificate_sha256", "possession_secret")
                val legacyFields = bootstrapFields + setOf("gateway_host", "gateway_ipv4", "gateway_port", "gateway_ca_der")
                require(values.keys == bootstrapFields || values.keys == legacyFields)
                val encoded = Gson().toJson(mapOf(
                    "protocol" to "provision-bootstrap-v1", "device_id" to values["device_id"],
                    "certificate_sha256" to values["certificate_sha256"],
                    "possession_secret" to values["possession_secret"],
                )).toCharArray()
                bootstrap = try { ProvisionBootstrap.parse(encoded) } finally { encoded.fill('\u0000') }
                if (values.keys == bootstrapFields) {
                    require(protocol == "provision-bootstrap-v1")
                    return ProvisionActivation(bootstrap, null, null, null, null)
                }
                require(protocol == "provision-activation-v1")
                val caText = values.getValue("gateway_ca_der")
                ca = Base64.getDecoder().decode(caText)
                require(ca.size in 1..4096 && Base64.getEncoder().encodeToString(ca) == caText)
                val certificate = CertificateFactory.getInstance("X.509")
                    .generateCertificate(ca.inputStream()) as X509Certificate
                require(certificate.basicConstraints >= 0 && certificate.encoded.contentEquals(ca))
                certificate.checkValidity()
                val address = values.getValue("gateway_ipv4")
                val parts = address.split('.')
                require(parts.size == 4 && parts.all { it.toIntOrNull() in 0..255 && it.toInt().toString() == it })
                val ipv4 = parts.map { it.toInt().toByte() }.toByteArray()
                val host = values.getValue("gateway_host")
                val portText = values.getValue("gateway_port")
                val port = portText.toInt()
                require(port.toString() == portText)
                // Reuse the candidate encoder's route constraints, then wipe its temporary record.
                ProvisionSettings.encode("_", charArrayOf(), host, ipv4, port, ca,
                    System.currentTimeMillis() / 1000).fill(0)
                return ProvisionActivation(bootstrap, ca, host, address, port)
            } catch (_: Exception) {
                bootstrap?.close(); ca?.fill(0)
                throw IllegalArgumentException("Invalid device activation")
            }
        }
    }
}
