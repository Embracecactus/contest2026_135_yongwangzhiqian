// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

import android.content.Context
import android.os.Looper
import android.os.SystemClock
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.net.URI
import java.security.KeyStore
import java.security.MessageDigest
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocket
import javax.net.ssl.TrustManagerFactory

internal class OtaSourceAcceptanceFailure(
    val stage: String,
    val failureClass: String
) : Exception(stage)

private class OtaSourceTimeoutException : Exception()

internal object OtaSourceAcceptance {
    data class Report(
        val requests: Int,
        val bytes: Long,
        val keyAliases: Int,
        val extractDirectories: Int,
        val catalogSha256: String,
        val apSha256: String,
        val cpSha256: String
    )

    private const val FILE_PREFIX = "ota-source-test-"
    private const val KEY_PREFIX = "shaniu-ota-source-"
    private const val DIRECTORY_PREFIX = "ota-package-"
    private const val LOGICAL_HOST = "shaniu-update.local"
    private const val RANGE_BYTES = 16 * 1024
    private const val DEADLINE_MILLIS = 60_000L
    private const val MAX_HEADER_BYTES = 8192

    fun run(context: Context, filename: String): Report {
        var stage = "input"
        var packageFile: File? = null
        var diagnosticServer: OtaPackageServer? = null
        var serverFailureBaseline = 0L
        var identityShape = "unknown"
        try {
            check(Looper.myLooper() != Looper.getMainLooper())
            val cache = context.cacheDir.canonicalFile
            require(filename.startsWith(FILE_PREFIX))
            require(filename.length <= 160 && filename.matches(Regex("[A-Za-z0-9._-]+")))
            val candidate = File(cache, filename).canonicalFile
            require(candidate.parentFile == cache && candidate.name == filename && candidate.isFile)
            packageFile = candidate

            stage = "inventory"
            val aliasesBefore = ownAliases()
            val directoriesBefore = ownDirectories(cache)
            var server: OtaPackageServer? = null
            var stalled: SSLSocket? = null
            try {
                stage = "open"
                server = OtaPackageServer.open(context, candidate)
                val metadata = checkNotNull(server.metadata)
                diagnosticServer = server
                check(server.running)
                val aliasesDuring = ownAliases()
                val directoriesDuring = ownDirectories(cache)
                val addedAliases = aliasesDuring - aliasesBefore
                val addedDirectories = directoriesDuring - directoriesBefore
                check(addedAliases.size == 1 && addedDirectories.size == 1)
                val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
                check(keyStore.getKey(addedAliases.single(), null).encoded == null)

                stage = "record"
                val source = parseRecord(server.requestRecord)
                check(source.uri.scheme == "https" && source.uri.host == LOGICAL_HOST)
                check(source.uri.port in 1..65535 && source.uri.rawQuery == null &&
                    source.uri.rawFragment == null)
                check(source.catalogDigest.equals(metadata.catalogSha256, true))
                val certificate = CertificateFactory.getInstance("X.509")
                    .generateCertificate(ByteArrayInputStream(source.certificatePem)) as X509Certificate
                identityShape = if (certificate.subjectAlternativeNames.orEmpty().any { name ->
                        name.size >= 2 && name[0] == 2 && name[1] == LOGICAL_HOST
                    }) "san" else "cn_only"
                val trusted = clientContext(certificate)
                val address = InetAddress.getByAddress(source.ipv4)
                val deadline = SystemClock.elapsedRealtime() + DEADLINE_MILLIS

                stage = "hostname-rejection"
                expectHandshakeRejected(trusted, address, source.uri.port, "wrong.invalid", deadline)
                stage = "empty-trust-rejection"
                expectHandshakeRejected(clientContext(null), address, source.uri.port,
                    LOGICAL_HOST, deadline)
                serverFailureBaseline = server.lastFailureSequence

                var requests = 0
                var transferred = 0L
                stage = "catalog"
                val catalog = get(trusted, address, source.uri.port, LOGICAL_HOST,
                    source.uri.rawPath, null, deadline)
                check(catalog.status == 200 && catalog.contentRange == null)
                check(sha256(catalog.body).equals(metadata.catalogSha256, true))
                requests++
                transferred += catalog.body.size

                val prefix = source.uri.rawPath.removeSuffix("catalog.json")
                stage = "ap-ranges"
                val ap = rangedDigest(trusted, address, source.uri.port,
                    "$prefix${"images/ap/ap.bin"}", metadata.ap.size, deadline)
                check(ap.sha256.equals(metadata.ap.sha256, true))
                requests += ap.requests
                transferred += ap.bytes

                stage = "cp-ranges"
                val cp = rangedDigest(trusted, address, source.uri.port,
                    "$prefix${"images/cp/cp.bin"}", metadata.cp.size, deadline)
                check(cp.sha256.equals(metadata.cp.sha256, true))
                requests += cp.requests
                transferred += cp.bytes

                stage = "unsafe-path"
                val unsafe = rawRequest(trusted, address, source.uri.port, LOGICAL_HOST,
                    "GET ${prefix}../images/ap/ap.bin HTTP/1.1\r\nHost: $LOGICAL_HOST\r\n\r\n",
                    deadline)
                check(unsafe.isEmpty())

                stage = "stalled-close"
                stalled = connect(trusted, address, source.uri.port, LOGICAL_HOST, deadline)
                server.close()
                check(runCatching { stalled.inputStream.read() }.getOrDefault(-1) < 0)
                stalled.close()
                stalled = null
                server = null

                stage = "cleanup"
                check(ownAliases() == aliasesBefore)
                check(ownDirectories(cache) == directoriesBefore)
                return Report(requests, transferred, keyAliases = addedAliases.size,
                    extractDirectories = addedDirectories.size,
                    catalogSha256 = source.catalogDigest,
                    apSha256 = ap.sha256, cpSha256 = cp.sha256)
            } finally {
                runCatching { stalled?.close() }
                server?.close()
            }
        } catch (error: Throwable) {
            if (error is OtaSourceAcceptanceFailure) throw error
            val safeStage = if (error is OtaPackageServerException) {
                "open_${error.stage.name.lowercase()}"
            } else {
                stage
            }
            val clientClasses = causeClasses(error)
            val serverClasses = diagnosticServer?.takeIf {
                it.lastFailureSequence > serverFailureBaseline
            }?.lastError
            val safeDetail = buildString {
                append(clientClasses)
                append("_identity_")
                append(identityShape)
                if (serverClasses != null) {
                    append("_server_")
                    append(serverClasses)
                }
            }
            throw OtaSourceAcceptanceFailure(safeStage, safeDetail)
        } finally {
            packageFile?.delete()
        }
    }

    private data class Source(
        val uri: URI,
        val ipv4: ByteArray,
        val certificatePem: ByteArray,
        val catalogDigest: String
    )

    private fun parseRecord(record: ByteArray): Source {
        require(record.size in 44..3371)
        require(record.copyOfRange(0, 4).contentEquals("SOU1".toByteArray(Charsets.US_ASCII)))
        val urlLength = be16(record, 4)
        val certificateLength = be16(record, 6)
        require(urlLength in 1..255 && certificateLength in 1..3072)
        require(record.size == 44 + urlLength + certificateLength)
        val url = record.copyOfRange(44, 44 + urlLength).toString(Charsets.US_ASCII)
        require(url.toByteArray(Charsets.US_ASCII).size == urlLength)
        val certificate = record.copyOfRange(44 + urlLength, record.size)
        require(certificate.toString(Charsets.US_ASCII)
            .startsWith("-----BEGIN CERTIFICATE-----\n"))
        return Source(URI(url), record.copyOfRange(8, 12), certificate,
            hex(record.copyOfRange(12, 44)))
    }

    private fun clientContext(certificate: X509Certificate?): SSLContext {
        val trustStore = KeyStore.getInstance(KeyStore.getDefaultType()).apply { load(null) }
        if (certificate != null) trustStore.setCertificateEntry("source", certificate)
        val factory = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm())
        factory.init(trustStore)
        return SSLContext.getInstance("TLS").apply { init(null, factory.trustManagers, null) }
    }

    private fun connect(
        context: SSLContext,
        address: InetAddress,
        port: Int,
        host: String,
        deadline: Long
    ): SSLSocket {
        val remaining = remaining(deadline)
        val tcp = Socket()
        try {
            tcp.connect(InetSocketAddress(address, port), minOf(5000, remaining))
            tcp.soTimeout = minOf(5000, remaining(deadline))
            return (context.socketFactory.createSocket(tcp, host, port, true) as SSLSocket).apply {
                enabledProtocols = arrayOf("TLSv1.2")
                sslParameters = sslParameters.apply { endpointIdentificationAlgorithm = "HTTPS" }
                soTimeout = minOf(5000, remaining(deadline))
                startHandshake()
            }
        } catch (error: Throwable) {
            runCatching { tcp.close() }
            throw error
        }
    }

    private fun expectHandshakeRejected(
        context: SSLContext,
        address: InetAddress,
        port: Int,
        host: String,
        deadline: Long
    ) {
        var rejected = false
        try {
            connect(context, address, port, host, deadline).close()
        } catch (_: javax.net.ssl.SSLException) {
            rejected = true
        } catch (_: java.security.cert.CertificateException) {
            rejected = true
        } catch (_: java.security.InvalidAlgorithmParameterException) {
            rejected = true
        }
        check(rejected)
    }

    private data class Response(val status: Int, val contentRange: String?, val body: ByteArray)

    private fun get(
        context: SSLContext,
        address: InetAddress,
        port: Int,
        host: String,
        path: String,
        range: LongRange?,
        deadline: Long
    ): Response {
        val rangeHeader = range?.let { "Range: bytes=${it.first}-${it.last}\r\n" } ?: ""
        val socket = connect(context, address, port, host, deadline)
        return socket.use {
            it.outputStream.write(("GET $path HTTP/1.1\r\nHost: $host\r\n" + rangeHeader +
                "Connection: close\r\n\r\n").toByteArray(Charsets.US_ASCII))
            it.outputStream.flush()
            val header = readHeader(it, deadline).toString(Charsets.US_ASCII)
            val lines = header.dropLast(4).split("\r\n")
            val status = when (lines.first()) {
                "HTTP/1.1 200 OK" -> 200
                "HTTP/1.1 206 Partial Content" -> 206
                else -> error("unexpected HTTP status")
            }
            val fields = linkedMapOf<String, String>()
            lines.drop(1).forEach { line ->
                val split = line.indexOf(':')
                require(split > 0)
                val name = line.substring(0, split).lowercase()
                require(fields.put(name, line.substring(split + 1).trim()) == null)
            }
            val length = requireNotNull(fields["content-length"]).toLong()
            require(length in 0..Int.MAX_VALUE.toLong())
            require(fields["content-type"] == "application/octet-stream")
            require(fields["connection"]?.equals("close", ignoreCase = true) == true)
            val body = readExactly(it, length.toInt(), deadline)
            check(it.inputStream.read() == -1)
            Response(status, fields["content-range"], body)
        }
    }

    private data class RangedResult(val sha256: String, val requests: Int, val bytes: Long)

    private fun rangedDigest(
        context: SSLContext,
        address: InetAddress,
        port: Int,
        path: String,
        size: Long,
        deadline: Long
    ): RangedResult {
        val digest = MessageDigest.getInstance("SHA-256")
        var offset = 0L
        var requests = 0
        while (offset < size) {
            val end = minOf(size - 1L, offset + RANGE_BYTES - 1L)
            val response = get(context, address, port, LOGICAL_HOST, path,
                offset..end, deadline)
            check(response.status == 206)
            check(response.body.size.toLong() == end - offset + 1L)
            check(response.contentRange == "bytes $offset-$end/$size")
            digest.update(response.body)
            offset = end + 1L
            requests++
        }
        return RangedResult(hex(digest.digest()), requests, offset)
    }

    private fun rawRequest(
        context: SSLContext,
        address: InetAddress,
        port: Int,
        host: String,
        request: String,
        deadline: Long
    ): ByteArray {
        val socket = connect(context, address, port, host, deadline)
        return socket.use {
            it.outputStream.write(request.toByteArray(Charsets.US_ASCII))
            it.outputStream.flush()
            val result = ByteArrayOutputStream()
            val buffer = ByteArray(1024)
            try {
                while (result.size() <= MAX_HEADER_BYTES) {
                    val read = it.inputStream.read(buffer)
                    if (read < 0) break
                    result.write(buffer, 0, read)
                }
            } catch (_: Exception) {
                // TLS close alerts and resets are both valid rejection signals.
            }
            result.toByteArray()
        }
    }

    private fun readHeader(socket: SSLSocket, deadline: Long): ByteArray {
        val output = ByteArrayOutputStream()
        while (output.size() < MAX_HEADER_BYTES) {
            socket.soTimeout = minOf(5000, remaining(deadline))
            val value = socket.inputStream.read()
            require(value >= 0)
            output.write(value)
            val bytes = output.toByteArray()
            if (bytes.size >= 4 && bytes.copyOfRange(bytes.size - 4, bytes.size)
                    .contentEquals("\r\n\r\n".toByteArray(Charsets.US_ASCII))) return bytes
        }
        error("HTTP header bound exceeded")
    }

    private fun readExactly(socket: SSLSocket, size: Int, deadline: Long): ByteArray {
        val result = ByteArray(size)
        var used = 0
        while (used < size) {
            socket.soTimeout = minOf(5000, remaining(deadline))
            val read = socket.inputStream.read(result, used, size - used)
            require(read > 0)
            used += read
        }
        return result
    }

    private fun ownAliases(): Set<String> {
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        return store.aliases().toList().filter { it.startsWith(KEY_PREFIX) }.toSet()
    }

    private fun ownDirectories(cache: File): Set<String> = cache.listFiles().orEmpty()
        .filter { it.isDirectory && it.name.startsWith(DIRECTORY_PREFIX) }
        .map { it.name }.toSet()

    private fun remaining(deadline: Long): Int {
        val remaining = deadline - SystemClock.elapsedRealtime()
        if (remaining <= 0) throw OtaSourceTimeoutException()
        return minOf(remaining, Int.MAX_VALUE.toLong()).toInt()
    }

    private fun be16(bytes: ByteArray, offset: Int): Int =
        ((bytes[offset].toInt() and 0xff) shl 8) or (bytes[offset + 1].toInt() and 0xff)

    private fun sha256(bytes: ByteArray): String =
        hex(MessageDigest.getInstance("SHA-256").digest(bytes))

    private fun hex(bytes: ByteArray): String = bytes.joinToString("") { "%02x".format(it) }

    private fun causeClasses(error: Throwable): String =
        generateSequence(error) { it.cause }.take(5)
            .map { it.javaClass.simpleName.ifEmpty { "Throwable" } }
            .joinToString(">")

}
