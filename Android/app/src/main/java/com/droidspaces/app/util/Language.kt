package com.droidspaces.app.util

/**
 * Language data class representing a supported language in the app.
 *
 * @param code Language code (e.g., "en", "es", "pt-rBR")
 * @param displayName Display name in English (e.g., "English", "Spanish")
 * @param nativeName Native name of the language (e.g., "English", "Español")
 */
data class Language(
    val code: String,
    val displayName: String,
    val nativeName: String
)
