// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

/**
 * Parses one bounded HTTP request for a verified member of the temporary OTA
 * package. Transport and ZIP I/O deliberately remain outside this utility.
 */
internal object OtaHttpRange {
    private const val MAX_HEADER_BYTES = 8192

    data class Request(
        val entry: String,
        val start: Long,
        val count: Long,
        val total: Long,
        val status: Int,
        val responseHeader: ByteArray
    )

    fun parse(header: ByteArray, prefix: String, sizes: Map<String, Long>): Request {
        require(header.isNotEmpty() && header.size <= MAX_HEADER_BYTES)
        require(prefix.matches(Regex("/u/[A-Za-z0-9_-]{1,64}/")))
        require(sizes.isNotEmpty() && sizes.values.all { it >= 0 })
        validateAsciiCrlf(header)
        require(header.endsWithCrlfCrlf())

        val text = header.toString(Charsets.US_ASCII)
        val lines = text.dropLast(4).split("\r\n")
        require(lines.isNotEmpty() && lines.none { it.isEmpty() })
        val path = parseRequestLine(lines.first())
        require(path.startsWith(prefix))
        require(!path.containsAny("?%#\\") && !path.contains(".."))

        val entry = path.removePrefix(prefix)
        val total = requireNotNull(sizes[entry]) { "unapproved ZIP entry" }
        require(entry.isNotEmpty())

        var range: String? = null
        var contentLengthSeen = false
        for (line in lines.drop(1)) {
            val separator = line.indexOf(':')
            require(separator > 0)
            val name = line.substring(0, separator)
            require(name.matches(Regex("[!#$%&'*+.^_`|~0-9A-Za-z-]+")))
            val value = line.substring(separator + 1).trim()

            when {
                name.equals("Range", ignoreCase = true) -> {
                    require(range == null) { "duplicate Range" }
                    range = value
                }
                name.equals("Transfer-Encoding", ignoreCase = true) -> {
                    throw IllegalArgumentException("Transfer-Encoding is forbidden")
                }
                name.equals("Content-Length", ignoreCase = true) -> {
                    require(!contentLengthSeen) { "duplicate Content-Length" }
                    require(value == "0") { "request body is forbidden" }
                    contentLengthSeen = true
                }
            }
        }

        val selection = range?.let { selectRange(it, total) }
        val start = selection?.first ?: 0L
        val count = selection?.second ?: total
        val status = if (selection == null) 200 else 206
        return Request(entry, start, count, total, status,
            responseHeader(status, start, count, total))
    }

    private fun parseRequestLine(line: String): String {
        val parts = line.split(' ')
        require(parts.size == 3 && parts[0] == "GET" &&
                (parts[2] == "HTTP/1.1" || parts[2] == "HTTP/1.0"))
        return parts[1]
    }

    private fun selectRange(value: String, total: Long): Pair<Long, Long> {
        require(value.startsWith("bytes="))
        val spec = value.removePrefix("bytes=")
        require(spec.count { it == '-' } == 1 && !spec.startsWith('-'))
        val separator = spec.indexOf('-')
        val start = decimal(spec.substring(0, separator))
        require(start < total)
        val endText = spec.substring(separator + 1)
        val end = if (endText.isEmpty()) total - 1L else decimal(endText)
        require(end >= start && end < total)
        return start to (end - start + 1L)
    }

    private fun decimal(value: String): Long {
        require(value.isNotEmpty())
        var result = 0L
        for (character in value) {
            require(character in '0'..'9')
            val digit = (character - '0').toLong()
            require(result <= (Long.MAX_VALUE - digit) / 10L)
            result = result * 10L + digit
        }
        return result
    }

    private fun responseHeader(status: Int, start: Long, count: Long, total: Long): ByteArray {
        val statusText = if (status == 200) "200 OK" else "206 Partial Content"
        val range = if (status == 206) "Content-Range: bytes $start-${start + count - 1L}/$total\r\n" else ""
        return ("HTTP/1.1 $statusText\r\n" +
                "Content-Type: application/octet-stream\r\n" +
                "Content-Length: $count\r\n" +
                range +
                "Connection: close\r\n\r\n").toByteArray(Charsets.US_ASCII)
    }

    private fun validateAsciiCrlf(header: ByteArray) {
        for (index in header.indices) {
            val value = header[index].toInt() and 0xff
            require(value in 0x20..0x7e || value == '\r'.code || value == '\n'.code)
            if (value == '\r'.code) require(index + 1 < header.size && header[index + 1] == '\n'.code.toByte())
            if (value == '\n'.code) require(index > 0 && header[index - 1] == '\r'.code.toByte())
        }
    }

    private fun ByteArray.endsWithCrlfCrlf(): Boolean = size >= 4 &&
        this[size - 4] == '\r'.code.toByte() && this[size - 3] == '\n'.code.toByte() &&
        this[size - 2] == '\r'.code.toByte() && this[size - 1] == '\n'.code.toByte()

    private fun String.containsAny(characters: String): Boolean = any { it in characters }
}
