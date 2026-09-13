// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.Looper
import android.os.SystemClock
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import android.util.Log
import java.io.File
import java.io.RandomAccessFile
import java.math.BigInteger
import java.net.Inet4Address
import java.net.InetSocketAddress
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.PrivateKey
import java.security.SecureRandom
import java.security.cert.X509Certificate
import java.security.interfaces.ECKey
import java.security.spec.ECGenParameterSpec
import java.util.Date
import java.util.Timer
import java.util.TimerTask
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean
import java.util.zip.ZipFile
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLEngine
import javax.net.ssl.SSLServerSocket
import javax.net.ssl.SSLSocket
import javax.net.ssl.X509ExtendedKeyManager
import javax.security.auth.x500.X500Principal

internal enum class OtaPackageServerStage {
    PACKAGE,
    WIFI,
    KEYSTORE,
    TLS,
    LOCAL_IO
}

/** Stable startup failure category; never carries the original exception. */
internal class OtaPackageServerException(val stage: OtaPackageServerStage) :
    IllegalStateException("OTA source startup failed: $stage")

/**
 * A short-lived, Wi-Fi-local source for one already validated package.
 *
 * Call [open] and [close] from a worker thread. The server exposes only the
 * five verified package members; device-side signature verification remains
 * required and is not performed here.
 */
internal class OtaPackageServer private constructor(
    val metadata: BkpackInspector.Metadata,
    val requestRecord: ByteArray,
    private val prefix: String,
    private val entries: Map<String, File>,
    private val sizes: Map<String, Long>,
    private val extractDir: File,
    private val keyAlias: String,
    private val serverSocket: SSLServerSocket
) : AutoCloseable {
    @Volatile
    var lastError: String? = null
        private set
    @Volatile
    var lastFailureSequence: Long = 0
        private set

    @Volatile
    private var closed = false
    @Volatile
    private var workerRunning = false
    private val clients = mutableSetOf<SSLSocket>()
    private val clientLock = Any()
    private val deadlineElapsedMillis = SystemClock.elapsedRealtime() + SERVER_TTL_MILLIS
    private val worker = Thread(::acceptLoop, "shaniu-ota-source")
    private val deadlineTimer = Timer("shaniu-ota-expiry", true)
    private var workerStarted = false
    private val cleaned = AtomicBoolean(false)
    private val listenerLogged = AtomicBoolean(false)
    private val acceptedLogged = AtomicBoolean(false)
    private val tlsLogged = AtomicBoolean(false)

    val running: Boolean
        get() = workerRunning && !closed

    init {
        worker.isDaemon = true
        deadlineTimer.schedule(object : TimerTask() {
            override fun run() = close()
        }, SERVER_TTL_MILLIS)
    }

    override fun close() {
        check(Thread.currentThread() !== Looper.getMainLooper().thread) {
            "OtaPackageServer.close must not run on the UI thread"
        }
        closed = true
        runCatching { serverSocket.close() }
        synchronized(clientLock) {
            clients.toList().forEach { socket -> runCatching { socket.close() } }
        }
        if (Thread.currentThread() !== worker) {
            runCatching { worker.join(CLOSE_JOIN_MILLIS) }
        }
    }

    private fun start() {
        if (listenerLogged.compareAndSet(false, true)) {
            Log.i(LOG_TAG, "listener ${serverSocket.inetAddress.hostAddress}:${serverSocket.localPort}")
        }
        workerRunning = true
        worker.start()
        workerStarted = true
    }

    private fun abortStartup() {
        if (workerStarted) {
            close()
            return
        }

        closed = true
        workerRunning = false
        deadlineTimer.cancel()
        runCatching { serverSocket.close() }
        cleanupOwnedResources()
    }

    private fun acceptLoop() {
        try {
            while (!closed && SystemClock.elapsedRealtime() < deadlineElapsedMillis) {
                val socket = try {
                    serverSocket.accept() as SSLSocket
                } catch (_: java.net.SocketTimeoutException) {
                    // Poll the monotonic deadline; this is not a client failure.
                    continue
                } catch (error: Exception) {
                    if (!closed) recordFailure("accept", error)
                    continue
                }
                try {
                    val accepted = synchronized(clientLock) {
                        if (closed) false else clients.add(socket)
                    }
                    if (!accepted) {
                        runCatching { socket.close() }
                        continue
                    }
                    if (acceptedLogged.compareAndSet(false, true)) {
                        Log.i(LOG_TAG, "first_tcp_accepted")
                    }
                    try {
                        serve(socket)
                    } catch (_: Exception) {
                        /* serve() records a safe stage and exception chain. */
                    } finally {
                        synchronized(clientLock) { clients -= socket }
                        runCatching { socket.close() }
                    }
                } catch (error: Exception) {
                    if (!closed) recordFailure("client", error)
                }
            }
        } finally {
            closed = true
            workerRunning = false
            deadlineTimer.cancel()
            runCatching { serverSocket.close() }
            cleanupOwnedResources()
        }
    }

    private fun cleanupOwnedResources() {
        if (cleaned.compareAndSet(false, true)) {
            deleteKeyAlias(keyAlias)
            extractDir.deleteRecursively()
        }
    }

    private fun serve(socket: SSLSocket) {
        socket.soTimeout = CLIENT_TIMEOUT_MILLIS
        socket.enabledProtocols = arrayOf("TLSv1.2")
        socket.enabledCipherSuites = arrayOf(TLS_CIPHER)
        socket.useClientMode = false
        socket.needClientAuth = false
        socket.wantClientAuth = false
        try {
            socket.startHandshake()
            if (tlsLogged.compareAndSet(false, true)) {
                Log.i(LOG_TAG, "first_tls_handshake")
            }
        } catch (error: Exception) {
            recordFailure("tls", error)
            throw error
        }

        try {
            val request = OtaHttpRange.parse(readHeader(socket), prefix, sizes)
            socket.outputStream.use { output ->
                output.write(request.responseHeader)
                RandomAccessFile(requireNotNull(entries[request.entry]), "r").use { input ->
                    input.seek(request.start)
                    var remaining = request.count
                    val buffer = ByteArray(COPY_CHUNK_SIZE)
                    while (remaining > 0L) {
                        val wanted = minOf(buffer.size.toLong(), remaining).toInt()
                        val read = input.read(buffer, 0, wanted)
                        check(read > 0) { "verified entry changed" }
                        output.write(buffer, 0, read)
                        remaining -= read.toLong()
                    }
                }
                output.flush()
            }
        } catch (error: Exception) {
            recordFailure("request", error)
            throw error
        }
    }

    @Synchronized
    private fun recordFailure(stage: String, error: Throwable) {
        if (closed) return
        val classes = generateSequence(error) { it.cause }.take(4)
            .map { it.javaClass.simpleName.ifEmpty { "Throwable" } }
            .joinToString(">")
        lastError = "$stage:$classes"
        lastFailureSequence++
        Log.w(LOG_TAG, "failure:$stage:$classes")
    }

    private fun readHeader(socket: SSLSocket): ByteArray {
        val input = socket.inputStream
        val header = ByteArray(MAX_HEADER_BYTES)
        var used = 0

        while (used < header.size) {
            val value = input.read()
            require(value >= 0) { "incomplete request header" }
            header[used++] = value.toByte()
            if (used >= 4 && header[used - 4] == '\r'.code.toByte() &&
                header[used - 3] == '\n'.code.toByte() &&
                header[used - 2] == '\r'.code.toByte() &&
                header[used - 1] == '\n'.code.toByte()) {
                return header.copyOf(used)
            }
        }

        throw IllegalArgumentException("request header too large")
    }

    companion object {
        private const val ANDROID_KEY_STORE = "AndroidKeyStore"
        private const val KEY_ALIAS_PREFIX = "shaniu-ota-source-"
        private const val SERVER_TTL_MILLIS = 15L * 60L * 1000L
        private const val CLOSE_JOIN_MILLIS = 2000L
        private const val CLIENT_TIMEOUT_MILLIS = 5000
        private const val MAX_HEADER_BYTES = 8192
        private const val COPY_CHUNK_SIZE = 8192
        private const val MAX_METADATA_BYTES = 64L * 1024L
        private const val MAX_IMAGE_BYTES = 8L * 1024L * 1024L
        private const val TLS_CIPHER = "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256"
        private const val LOG_TAG = "ShaniuOtaSource"
        private val packageEntries = listOf(
            "manifest.json", "catalog.json", "catalog.sig",
            "images/ap/ap.bin", "images/cp/cp.bin"
        )

        /** Opens a new server. This performs ZIP extraction and must not run on UI. */
        fun open(context: Context, packageFile: File): OtaPackageServer {
            var extractDir: File? = null
            var keyAlias: String? = null
            var socket: SSLServerSocket? = null
            var service: OtaPackageServer? = null
            try {
                val localAddress = try {
                    activeWifiIpv4(context)
                } catch (_: Exception) {
                    throw OtaPackageServerException(OtaPackageServerStage.WIFI)
                }
                val metadata = try {
                    BkpackInspector.inspect(packageFile)
                } catch (_: Exception) {
                    throw OtaPackageServerException(OtaPackageServerStage.PACKAGE)
                }
                val directory = File(context.cacheDir,
                    "ota-package-${UUID.randomUUID()}").also {
                    if (!it.mkdir()) throw OtaPackageServerException(OtaPackageServerStage.LOCAL_IO)
                }
                extractDir = directory
                val extracted = try {
                    extractVerifiedEntries(packageFile, metadata, directory)
                } catch (_: Exception) {
                    throw OtaPackageServerException(OtaPackageServerStage.PACKAGE)
                }
                val sizes = extracted.mapValues { it.value.length() }
                val alias = "$KEY_ALIAS_PREFIX${UUID.randomUUID()}"
                keyAlias = alias
                val certificate = try {
                    createServerCertificate(alias)
                } catch (_: Exception) {
                    throw OtaPackageServerException(OtaPackageServerStage.KEYSTORE)
                }
                val nonce = randomNonce()
                val boundSocket = try {
                    createServerSocket(alias, certificate, localAddress)
                } catch (_: Exception) {
                    throw OtaPackageServerException(OtaPackageServerStage.TLS)
                }
                socket = boundSocket
                val prefix = "/u/$nonce/"
                val url = "https://shaniu-update.local:${boundSocket.localPort}${prefix}catalog.json"
                val requestRecord = try {
                    requestRecord(url, localAddress, certificate, metadata.catalogSha256)
                } catch (_: Exception) {
                    throw OtaPackageServerException(OtaPackageServerStage.PACKAGE)
                }
                service = OtaPackageServer(metadata, requestRecord, prefix, extracted, sizes,
                    directory, alias, boundSocket)
                service.start()
                return service
            } catch (failure: OtaPackageServerException) {
                service?.abortStartup()
                runCatching { socket?.close() }
                keyAlias?.let(::deleteKeyAlias)
                extractDir?.deleteRecursively()
                throw failure
            } catch (_: Exception) {
                service?.abortStartup()
                runCatching { socket?.close() }
                keyAlias?.let(::deleteKeyAlias)
                extractDir?.deleteRecursively()
                throw OtaPackageServerException(OtaPackageServerStage.LOCAL_IO)
            }
        }

        private fun activeWifiIpv4(context: Context): Inet4Address {
            val manager = context.getSystemService(ConnectivityManager::class.java)
            val network = requireNotNull(manager.activeNetwork)
            val capabilities = requireNotNull(manager.getNetworkCapabilities(network))
            require(capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI))
            val properties = requireNotNull(manager.getLinkProperties(network))
            return requireNotNull(properties.linkAddresses
                .mapNotNull { it.address as? Inet4Address }
                .firstOrNull { !it.isLoopbackAddress && !it.isAnyLocalAddress })
        }

        private fun extractVerifiedEntries(
            packageFile: File,
            metadata: BkpackInspector.Metadata,
            directory: File
        ): Map<String, File> = ZipFile(packageFile).use { zip ->
            packageEntries.associateWith { name ->
                val entry = requireNotNull(zip.getEntry(name))
                val limit = if (name.startsWith("images/")) MAX_IMAGE_BYTES else MAX_METADATA_BYTES
                val expected = when (name) {
                    "images/ap/ap.bin" -> metadata.ap.size
                    "images/cp/cp.bin" -> metadata.cp.size
                    else -> entry.size
                }
                require(entry.size in 1..limit && entry.size == expected)
                val target = File(directory, name.replace('/', '_'))
                var copied = 0L
                zip.getInputStream(entry).use { input ->
                    target.outputStream().use { output ->
                        val buffer = ByteArray(COPY_CHUNK_SIZE)
                        while (true) {
                            val read = input.read(buffer)
                            if (read < 0) break
                            copied += read.toLong()
                            require(copied <= limit)
                            output.write(buffer, 0, read)
                        }
                    }
                }
                require(copied == entry.size && target.length() == entry.size)
                target
            }
        }

        private fun createServerCertificate(alias: String): X509Certificate {
            val now = System.currentTimeMillis()
            val generator = KeyPairGenerator.getInstance(
                KeyProperties.KEY_ALGORITHM_EC, ANDROID_KEY_STORE)
            val specification = KeyGenParameterSpec.Builder(alias,
                KeyProperties.PURPOSE_SIGN or KeyProperties.PURPOSE_VERIFY)
                .setAlgorithmParameterSpec(ECGenParameterSpec("secp256r1"))
                /* Conscrypt signs the already hashed TLS 1.2 transcript via
                 * NONEwithECDSA; SHA-256 remains needed for the certificate.
                 */
                .setDigests(KeyProperties.DIGEST_NONE, KeyProperties.DIGEST_SHA256)
                .setCertificateSubject(X500Principal("CN=shaniu-update.local"))
                .setCertificateSerialNumber(BigInteger.valueOf(now))
                .setCertificateNotBefore(Date(now - 24L * 60L * 60L * 1000L))
                .setCertificateNotAfter(Date(now + 24L * 60L * 60L * 1000L))
                .build()
            generator.initialize(specification)
            val pair = generator.generateKeyPair()
            return OtaSelfSignedCertificate.create(
                pair.public,
                pair.private,
                "shaniu-update.local",
                BigInteger.valueOf(now),
                Date(now - 24L * 60L * 60L * 1000L),
                Date(now + 24L * 60L * 60L * 1000L)
            )
        }

        private fun createServerSocket(
            alias: String,
            certificate: X509Certificate,
            address: Inet4Address
        ): SSLServerSocket {
            val store = KeyStore.getInstance(ANDROID_KEY_STORE).apply { load(null) }
            val privateKey = store.getKey(alias, null) as? PrivateKey
                ?: throw IllegalStateException("server key unavailable")
            require(privateKey is ECKey)
            val context = SSLContext.getInstance("TLS")
            context.init(arrayOf(SingleAliasKeyManager(alias, privateKey, certificate)), null,
                SecureRandom())
            return (context.serverSocketFactory.createServerSocket() as SSLServerSocket).apply {
                enabledProtocols = arrayOf("TLSv1.2")
                enabledCipherSuites = arrayOf(TLS_CIPHER)
                needClientAuth = false
                wantClientAuth = false
                soTimeout = 1000
                bind(InetSocketAddress(address, 0))
            }
        }

        private fun requestRecord(
            url: String,
            address: Inet4Address,
            certificate: X509Certificate,
            catalogSha256: String
        ): ByteArray {
            val urlBytes = url.toByteArray(Charsets.US_ASCII)
            val certificatePem = certificatePem(certificate).toByteArray(Charsets.US_ASCII)
            val digest = hexToBytes(catalogSha256)
            require(urlBytes.size in 1..255 && certificatePem.size in 1..3072 &&
                    digest.size == 32 && digest.any { it.toInt() != 0 })
            val result = ByteArray(44 + urlBytes.size + certificatePem.size)
            result[0] = 'S'.code.toByte()
            result[1] = 'O'.code.toByte()
            result[2] = 'U'.code.toByte()
            result[3] = '1'.code.toByte()
            writeBe16(result, 4, urlBytes.size)
            writeBe16(result, 6, certificatePem.size)
            System.arraycopy(address.address, 0, result, 8, 4)
            System.arraycopy(digest, 0, result, 12, digest.size)
            System.arraycopy(urlBytes, 0, result, 44, urlBytes.size)
            System.arraycopy(certificatePem, 0, result, 44 + urlBytes.size, certificatePem.size)
            return result
        }

        private fun certificatePem(certificate: X509Certificate): String =
            "-----BEGIN CERTIFICATE-----\n" +
                Base64.encodeToString(certificate.encoded, Base64.NO_WRAP) +
                "\n-----END CERTIFICATE-----\n"

        private fun hexToBytes(value: String): ByteArray {
            require(value.matches(Regex("[0-9a-fA-F]{64}")))
            return ByteArray(32) { index ->
                value.substring(index * 2, index * 2 + 2).toInt(16).toByte()
            }
        }

        private fun writeBe16(target: ByteArray, offset: Int, value: Int) {
            target[offset] = (value ushr 8).toByte()
            target[offset + 1] = value.toByte()
        }

        private fun randomNonce(): String {
            val bytes = ByteArray(18)
            SecureRandom().nextBytes(bytes)
            return Base64.encodeToString(bytes,
                Base64.URL_SAFE or Base64.NO_PADDING or Base64.NO_WRAP)
        }

        private fun deleteKeyAlias(alias: String) {
            runCatching {
                KeyStore.getInstance(ANDROID_KEY_STORE).apply { load(null) }.deleteEntry(alias)
            }
        }
    }

    private class SingleAliasKeyManager(
        private val alias: String,
        private val privateKey: PrivateKey,
        private val certificate: X509Certificate
    ) : X509ExtendedKeyManager() {
        override fun getClientAliases(keyType: String?, issuers: Array<java.security.Principal>?) = null
        override fun chooseClientAlias(keyType: Array<String>?, issuers: Array<java.security.Principal>?, socket: java.net.Socket?) = null
        override fun getServerAliases(keyType: String?, issuers: Array<java.security.Principal>?) = arrayOf(alias)
        override fun chooseServerAlias(keyType: String?, issuers: Array<java.security.Principal>?, socket: java.net.Socket?) = alias
        override fun getCertificateChain(requestedAlias: String?) = if (requestedAlias == alias) arrayOf(certificate) else null
        override fun getPrivateKey(requestedAlias: String?) = if (requestedAlias == alias) privateKey else null
        override fun chooseEngineClientAlias(keyType: Array<String>?, issuers: Array<java.security.Principal>?, engine: SSLEngine?) = null
        override fun chooseEngineServerAlias(keyType: String?, issuers: Array<java.security.Principal>?, engine: SSLEngine?) = alias
    }
}
