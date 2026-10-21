// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ImportedFileOwnerTest {
    @Test fun closeDeletesACompletedImportBeforeItsUiCallbackCanClaimIt() {
        val directory = Files.createTempDirectory("import-owner-").toFile()
        val file = File(directory, "completed.bkpack").apply { writeText("completed") }
        try {
            val owner = ImportedFileOwner()
            assertTrue(owner.own(file))

            // Model Activity destruction after Handler.post and before the
            // completion runnable receives ownership.
            owner.close()

            assertFalse(file.exists())
            assertFalse(owner.claim(file))
        } finally {
            file.delete()
            directory.delete()
        }
    }

    @Test fun workerCompletionAfterCloseCannotLeaveANewlyWrittenFile() {
        val directory = Files.createTempDirectory("import-owner-").toFile()
        val file = File(directory, "writing.bkpack").apply { writeText("completed") }
        try {
            val owner = ImportedFileOwner()
            // Model destruction while the worker writes. Ownership is taken
            // only after parsing succeeds, so close rejects and deletes it.
            owner.close()
            assertFalse(owner.own(file))
            assertFalse(file.exists())
        } finally {
            file.delete()
            directory.delete()
        }
    }

    @Test fun claimedImportSurvivesOwnerCloseForItsSelectedFileOwner() {
        val directory = Files.createTempDirectory("import-owner-").toFile()
        val file = File(directory, "selected.bkep").apply { writeText("selected") }
        try {
            val owner = ImportedFileOwner()
            assertTrue(owner.own(file))
            assertTrue(owner.claim(file))
            owner.close()
            assertTrue(file.exists())
        } finally {
            file.delete()
            directory.delete()
        }
    }
}
