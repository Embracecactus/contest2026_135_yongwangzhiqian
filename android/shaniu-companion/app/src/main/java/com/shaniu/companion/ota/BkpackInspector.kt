// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

import com.google.gson.Strictness
import com.google.gson.stream.JsonReader
import com.google.gson.stream.JsonToken
import java.io.StringReader
import java.io.File
import java.security.MessageDigest
import java.util.zip.ZipFile

/** Local integrity inspection only. catalog.sig is retained for the device;
 * this code does not possess a trust key and never claims signature validity.
 */
internal object BkpackInspector {
    private const val MAX_METADATA = 64 * 1024L
    private const val MAX_IMAGE = 8 * 1024 * 1024L
    private const val MAX_TOTAL = 20 * 1024 * 1024L
    private val required = setOf("manifest.json", "catalog.json", "catalog.sig", "images/ap/ap.bin", "images/cp/cp.bin")
    data class Image(val size: Long, val sha256: String)
    data class Metadata(val board: String, val version: String, val securityCounter: Long,
                        val layoutSha256: String, val catalogSha256: String,
                        val ap: Image, val cp: Image)
    fun inspect(file: File): Metadata = ZipFile(file).use { zip ->
        val entries = mutableListOf<java.util.zip.ZipEntry>()
        val enumeration = zip.entries()
        while (enumeration.hasMoreElements()) {
            require(entries.size < 5) { "too many entries" }
            entries += enumeration.nextElement()
        }
        require(entries.size == 5 && entries.map { it.name }.toSet().size == 5)
        require(entries.map { it.name }.toSet() == required)
        require(entries.none { it.name.startsWith('/') || it.name.contains("..") || it.isDirectory })
        require(entries.sumOf { require(it.size >= 0); it.size } <= MAX_TOTAL)
        val catalog = bytes(zip, "catalog.json", MAX_METADATA)
        bytes(zip, "manifest.json", MAX_METADATA) // bounded format member, required even if catalog is authoritative
        require(bytes(zip, "catalog.sig", MAX_METADATA).isNotEmpty())
        val json = JsonReader(StringReader(catalog.toString(Charsets.UTF_8))).use { reader ->
            reader.strictness = Strictness.STRICT; readObject(reader).also { require(reader.peek() == JsonToken.END_DOCUMENT) }
        }
        require(string(json, "format") == "bk7258.ota/2")
        val version = string(json, "version")
        require(version.matches(Regex("[0-9]+\\.[0-9]+\\.[0-9]+\\+[1-9][0-9]*")))
        val counter = uint(json, "security_counter")
        val target = obj(json, "target")
        val ap = image(zip, obj(json, "ap"), "images/ap/ap.bin")
        val cp = image(zip, obj(json, "cp"), "images/cp/cp.bin")
        val layout = string(obj(json, "layout"), "sha256")
        require(layout.matches(Regex("[0-9a-fA-F]{64}")))
        Metadata(string(target, "physical_board"), version, counter, layout,
            sha256(catalog), ap, cp)
    }
    private fun image(zip: ZipFile, item: Map<String, Any>, expected: String): Image {
        require(string(item, "uri") == expected)
        val entry = requireNotNull(zip.getEntry(expected))
        require(entry.size == uint(item, "size") && entry.size in 1..MAX_IMAGE)
        require(string(item, "sha256").matches(Regex("[0-9a-fA-F]{64}")))
        var total = 0L
        val digest = zip.getInputStream(entry).use { input ->
            val md = MessageDigest.getInstance("SHA-256"); val buffer = ByteArray(8192)
            while (true) { val n = input.read(buffer); if (n < 0) break; total += n; require(total <= MAX_IMAGE); md.update(buffer, 0, n) }
            hex(md.digest())
        }
        require(total == entry.size)
        require(digest.equals(string(item, "sha256"), true))
        return Image(entry.size, digest)
    }
    private fun bytes(zip: ZipFile, name: String, limit: Long): ByteArray {
        val entry = requireNotNull(zip.getEntry(name)); require(entry.size in 1..limit)
        return zip.getInputStream(entry).use { input ->
            val out = java.io.ByteArrayOutputStream(entry.size.toInt())
            val buffer = ByteArray(4096)
            while (true) { val n = input.read(buffer); if (n < 0) break; require(out.size() + n <= limit); out.write(buffer, 0, n) }
            out.toByteArray().also { require(it.size.toLong() == entry.size) }
        }
    }
    private fun sha256(bytes: ByteArray) = hex(MessageDigest.getInstance("SHA-256").digest(bytes))
    private fun string(o: Map<String, Any>, key: String) = o[key] as? String ?: throw IllegalArgumentException("$key")
    private fun obj(o: Map<String, Any>, key: String) = o[key] as? Map<String, Any> ?: throw IllegalArgumentException("$key")
    private fun uint(o: Map<String, Any>, key: String): Long {
        val value = o[key] as? Number ?: throw IllegalArgumentException(key)
        return value.toLong().also { require(it in 1..UInt.MAX_VALUE.toLong()) }
    }
    private fun readObject(r: JsonReader, depth: Int = 0): Map<String, Any> {
        require(depth <= 4) { "catalog nesting exceeds limit" }
        val result = linkedMapOf<String, Any>(); r.beginObject()
        while (r.hasNext()) { val name = r.nextName(); require(!result.containsKey(name)); result[name] = readValue(r, depth) }
        r.endObject(); return result
    }
    private fun readValue(r: JsonReader, depth: Int): Any = when (r.peek()) {
        JsonToken.BEGIN_OBJECT -> readObject(r, depth + 1)
        JsonToken.STRING -> r.nextString()
        JsonToken.NUMBER -> r.nextString().also { require(it.matches(Regex("[1-9][0-9]*"))) }.toLong()
        else -> throw IllegalArgumentException("non-primitive JSON value")
    }
    private fun hex(bytes: ByteArray) = bytes.joinToString("") { "%02x".format(it) }
}
