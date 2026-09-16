// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.net.Inet4Address
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.net.URI
import java.security.KeyStore
import java.security.cert.X509Certificate
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocket
import javax.net.ssl.TrustManagerFactory
import javax.net.ssl.X509TrustManager

/** Resolve and authenticate a cloud endpoint off the UI thread. No API key is
 * sent here. The returned CA is from Android's trust store, never an unverified
 * certificate supplied by the endpoint. Provisioning TLS has its own identity CA.
 */
object CloudEndpoint {
    data class Verified(val address: ByteArray, val caDer: ByteArray)

    fun resolve(baseUrl: String): Verified {
        val uri = URI(baseUrl)
        require(uri.scheme == "https" && uri.rawUserInfo == null && uri.rawFragment == null && uri.rawQuery == null)
        val host = requireNotNull(uri.host)
        val port = if (uri.port == -1) 443 else uri.port
        require(port in 1..65535)
        val managers = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm())
        managers.init(null as KeyStore?)
        val manager = managers.trustManagers.filterIsInstance<X509TrustManager>().single()
        val context = SSLContext.getInstance("TLS")
        context.init(null, arrayOf(manager), null)
        val addresses = InetAddress.getAllByName(host).filterIsInstance<Inet4Address>().take(4)
        var failure: Exception? = null
        for (address in addresses) {
            try {
                Socket().use { raw ->
                    raw.connect(InetSocketAddress(address, port), 5000)
                    raw.soTimeout = 7000
                    (context.socketFactory.createSocket(raw, host, port, true) as SSLSocket).use { tls ->
                        tls.soTimeout = 7000
                        // Match the board cloud transport: accepting a TLS 1.3-only
                        // endpoint here would provision a service it cannot use.
                        require("TLSv1.2" in tls.supportedProtocols)
                        tls.enabledProtocols = arrayOf("TLSv1.2")
                        tls.sslParameters = tls.sslParameters.apply { endpointIdentificationAlgorithm = "HTTPS" }
                        tls.startHandshake()
                        val chain = tls.session.peerCertificates.map { it as X509Certificate }
                        // Find the system trust anchor that signs the final peer-chain cert.
                        val last = chain.last()
                        val root = manager.acceptedIssuers.firstOrNull { issuer ->
                            issuer.subjectX500Principal == last.issuerX500Principal &&
                                runCatching { last.verify(issuer.publicKey) }.isSuccess
                        } ?: throw IllegalArgumentException("No supported cloud trust anchor")
                        root.checkValidity()
                        val der = root.encoded
                        require(der.size in 1..4096)
                        return Verified(address.address, der)
                    }
                }
            } catch (error: Exception) { failure = error }
        }
        throw failure ?: IllegalArgumentException("No IPv4 endpoint")
    }
}
