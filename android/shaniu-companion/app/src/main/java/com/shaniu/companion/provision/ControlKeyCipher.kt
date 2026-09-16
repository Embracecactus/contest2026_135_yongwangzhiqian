// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.security.KeyStore
import java.util.Base64
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Encrypts a control key at rest; device and transaction are authenticated.
 * Android uses a non-exportable Keystore key, without assuming hardware backing.
 */
internal class ControlKeyCipher(private val key: () -> SecretKey) {
    fun seal(scope: String, plaintext: ByteArray): String {
        require(plaintext.size == 32 && plaintext.any { it != 0.toByte() })
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, key())
        cipher.updateAAD(scope.toByteArray(Charsets.UTF_8))
        val encrypted = cipher.doFinal(plaintext)
        return try {
            check(cipher.iv.size == 12)
            Base64.getEncoder().encodeToString(cipher.iv + encrypted)
        } finally { encrypted.fill(0) }
    }

    fun <T> use(scope: String, record: String, block: (ByteArray) -> T): T {
        require(record.length == 80)
        val bytes = Base64.getDecoder().decode(record)
        try {
            require(bytes.size == 60)
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, bytes, 0, 12))
            cipher.updateAAD(scope.toByteArray(Charsets.UTF_8))
            val plaintext = cipher.doFinal(bytes, 12, 48)
            try {
                require(plaintext.size == 32 && plaintext.any { it != 0.toByte() })
                return block(plaintext)
            } finally { plaintext.fill(0) }
        } finally { bytes.fill(0) }
    }

    companion object {
        private const val ALIAS = "shaniu.direct-control.v1"
        private val lock = Any()
        fun android(alias: String = ALIAS) = ControlKeyCipher {
            synchronized(lock) {
                val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
                (store.getKey(alias, null) as? SecretKey) ?: KeyGenerator.getInstance(
                    KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore",
                ).apply {
                    init(KeyGenParameterSpec.Builder(alias,
                        KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                        .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                        .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                        .setKeySize(256).build())
                }.generateKey()
            }
        }
    }
}
