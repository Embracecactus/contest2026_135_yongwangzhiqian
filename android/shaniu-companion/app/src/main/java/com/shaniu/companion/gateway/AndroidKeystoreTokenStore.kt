// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.gateway

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.io.IOException
import java.nio.charset.StandardCharsets
import java.security.GeneralSecurityException
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * Encrypts the Gateway bearer token at rest with a non-exportable Android
 * Keystore AES key. This does not claim StrongBox or hardware-backed storage.
 */
class AndroidKeystoreTokenStore(context: Context) : GatewayAccessTokenStore {
    private val preferences = context.applicationContext.getSharedPreferences(
        PREFERENCES_NAME,
        Context.MODE_PRIVATE,
    )

    @Synchronized
    override fun storeAccessToken(accessToken: String) {
        storeScoped(accessToken, "")
    }

    fun providerFor(origin: String, deviceId: String): GatewayAccessTokenProvider {
        val scope = "$origin\n$deviceId"
        return GatewayAccessTokenProvider { readScoped(scope) }
    }

    fun storeFor(origin: String, deviceId: String, accessToken: String) {
        storeScoped(accessToken, "$origin\n$deviceId")
    }

    @Synchronized
    private fun storeScoped(accessToken: String, scope: String) {
        GatewayTokenPolicy.requireValid(accessToken)
        val plaintext = accessToken.toByteArray(StandardCharsets.UTF_8)
        var ciphertext: ByteArray? = null
        try {
            val cipher = Cipher.getInstance(CIPHER_TRANSFORMATION)
            cipher.init(Cipher.ENCRYPT_MODE, getOrCreateKey())
            cipher.updateAAD(scope.toByteArray(StandardCharsets.UTF_8))
            ciphertext = cipher.doFinal(plaintext)
            val committed = preferences.edit()
                .putString(KEY_IV, Base64.encodeToString(cipher.iv, Base64.NO_WRAP))
                .putString(KEY_CIPHERTEXT, Base64.encodeToString(ciphertext, Base64.NO_WRAP))
                .putString(KEY_SCOPE, scope)
                .commit()
            if (!committed) throw IOException(CREDENTIAL_ERROR)
        } catch (error: GeneralSecurityException) {
            throw IOException(CREDENTIAL_ERROR, error)
        } finally {
            plaintext.fill(0)
            ciphertext?.fill(0)
        }
    }

    @Synchronized
    override fun readAccessToken(): String? {
        return readScoped("")
    }

    @Synchronized
    private fun readScoped(scope: String): String? {
        // Legacy unscoped credentials require explicit resupply. Never send a
        // saved bearer to a newly typed origin or a different device.
        if (preferences.getString(KEY_SCOPE, null) != scope) return null
        val encodedIv = preferences.getString(KEY_IV, null)
        val encodedCiphertext = preferences.getString(KEY_CIPHERTEXT, null)
        if (encodedIv == null && encodedCiphertext == null) return null
        if (encodedIv == null || encodedCiphertext == null) {
            throw IOException(CREDENTIAL_ERROR)
        }

        var iv: ByteArray? = null
        var ciphertext: ByteArray? = null
        var plaintext: ByteArray? = null
        try {
            iv = Base64.decode(encodedIv, Base64.NO_WRAP)
            ciphertext = Base64.decode(encodedCiphertext, Base64.NO_WRAP)
            val key = getExistingKey() ?: throw IOException(CREDENTIAL_ERROR)
            val cipher = Cipher.getInstance(CIPHER_TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(GCM_TAG_BITS, iv))
            cipher.updateAAD(scope.toByteArray(StandardCharsets.UTF_8))
            plaintext = cipher.doFinal(ciphertext)
            return GatewayTokenPolicy.requireValid(String(plaintext, StandardCharsets.UTF_8))
        } catch (error: GeneralSecurityException) {
            throw IOException(CREDENTIAL_ERROR, error)
        } catch (error: IllegalArgumentException) {
            throw IOException(CREDENTIAL_ERROR, error)
        } finally {
            iv?.fill(0)
            ciphertext?.fill(0)
            plaintext?.fill(0)
        }
    }

    @Synchronized
    override fun clearAccessToken() {
        var failure: Exception? = null
        try {
            val keyStore = loadKeyStore()
            if (keyStore.containsAlias(KEY_ALIAS)) keyStore.deleteEntry(KEY_ALIAS)
        } catch (error: GeneralSecurityException) {
            failure = error
        } catch (error: IOException) {
            failure = error
        }
        if (!preferences.edit().clear().commit() && failure == null) {
            failure = IOException(CREDENTIAL_ERROR)
        }
        if (failure != null) throw IOException(CREDENTIAL_ERROR, failure)
    }

    private fun getExistingKey(): SecretKey? {
        val key = loadKeyStore().getKey(KEY_ALIAS, null) ?: return null
        return key as? SecretKey ?: throw GeneralSecurityException("Unexpected key type")
    }

    private fun getOrCreateKey(): SecretKey = getExistingKey() ?: KeyGenerator.getInstance(
        KeyProperties.KEY_ALGORITHM_AES,
        ANDROID_KEY_STORE,
    ).run {
        init(
            KeyGenParameterSpec.Builder(
                KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
            )
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(AES_KEY_BITS)
                .setRandomizedEncryptionRequired(true)
                .build(),
        )
        generateKey()
    }

    private fun loadKeyStore(): KeyStore = KeyStore.getInstance(ANDROID_KEY_STORE).apply {
        load(null)
    }

    companion object {
        private const val ANDROID_KEY_STORE = "AndroidKeyStore"
        private const val KEY_ALIAS = "com.shaniu.companion.gateway.token.aes.v1"
        private const val PREFERENCES_NAME = "shaniu_gateway_credentials_v1"
        private const val KEY_IV = "token_iv"
        private const val KEY_CIPHERTEXT = "token_ciphertext"
        private const val KEY_SCOPE = "token_scope"
        private const val CIPHER_TRANSFORMATION = "AES/GCM/NoPadding"
        private const val AES_KEY_BITS = 256
        private const val GCM_TAG_BITS = 128
        private const val CREDENTIAL_ERROR = "Gateway credentials are unavailable"
    }
}
