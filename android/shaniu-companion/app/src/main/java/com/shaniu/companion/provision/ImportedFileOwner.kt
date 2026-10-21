// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.io.File

/**
 * Holds a temporary import while a worker result waits for a main-thread
 * callback.  The callback must claim the file before assigning it to UI state;
 * closing the owner releases anything that has not been claimed.
 */
internal class ImportedFileOwner {
    private val files = mutableSetOf<File>()
    private var closed = false

    @Synchronized
    fun own(file: File): Boolean {
        if (closed) {
            file.delete()
            return false
        }
        files += file
        return true
    }

    @Synchronized
    fun claim(file: File): Boolean = files.remove(file)

    @Synchronized
    fun discard(file: File) {
        files -= file
        file.delete()
    }

    @Synchronized
    fun close() {
        closed = true
        files.forEach { it.delete() }
        files.clear()
    }
}
