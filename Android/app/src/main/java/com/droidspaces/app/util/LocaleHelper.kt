package com.droidspaces.app.util

import android.content.Context
import android.content.res.Configuration
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.os.LocaleListCompat
import java.util.*

/**
 * Modern LocaleHelper using AppCompatDelegate.setApplicationLocales() API.
 *
 * This implementation works on ALL Android versions (API 24+) and ALL devices
 * including Samsung One UI 8+. It uses the modern AppCompat API which:
 * - Automatically saves locale preferences (with autoStoreLocales=true)
 * - Automatically recreates activities
 * - Updates all string resources
 * - Works identically across all Android versions
 */
object LocaleHelper {

    /**
     * Get available languages based on supported translations.
     * Scans res/values-XX folders to build language list.
     */
    @Suppress("UNUSED_PARAMETER")
    fun getAvailableLanguages(context: Context): List<Language> {
        val languages = mutableListOf<Language>()

        // Supported languages based on values-XX folders
        languages.add(Language("en", "English", "English"))
        languages.add(Language("es", "Spanish", "Español"))
        languages.add(Language("pt-rBR", "Portuguese (Brazil)", "Português (Brasil)"))
        languages.add(Language("ru", "Russian", "Русский"))
        languages.add(Language("fil", "Filipino", "Filipino"))
        languages.add(Language("hi", "Hindi", "हिन्दी"))
        languages.add(Language("ilo-rPH", "Ilocano (Philippines)", "Ilocano"))
        languages.add(Language("cbk-rPH", "Chavacano (Philippines)", "Chavacano"))
        languages.add(Language("ceb-rPH", "Cebuano (Philippines)", "Binisaya"))
        languages.add(Language("pag-rPH", "Pangasinan (Philippines)", "Pangasinan"))
        languages.add(Language("de", "German", "Deutsch"))
        languages.add(Language("fr", "French", "Français"))

        return languages
    }

    /**
     * Get current language code in the format used by Language.code.
     * Returns language code (e.g., "en", "es", "pt-rBR") or "system" for system default.
     */
    fun getCurrentLanguageCode(): String {
        val locales = AppCompatDelegate.getApplicationLocales()
        if (locales.isEmpty) {
            return "system"
        }

        val locale = locales.get(0) ?: return "system"

        // Format: "language" or "language-rREGION" to match Language.code format
        return if (locale.country.isNotEmpty()) {
            "${locale.language}-r${locale.country}"
        } else {
            locale.language
        }
    }

    /**
     * Get current language code (legacy method name).
     * @deprecated Use getCurrentLanguageCode() instead
     */
    @Deprecated("Use getCurrentLanguageCode() instead", ReplaceWith("getCurrentLanguageCode()"))
    fun getCurrentLanguage(): String {
        return getCurrentLanguageCode()
    }

    /**
     * Get current app locale for display purposes.
     * Returns Locale object or null for system default.
     */
    @Suppress("UNUSED_PARAMETER")
    fun getCurrentAppLocale(context: Context): Locale? {
        val locales = AppCompatDelegate.getApplicationLocales()
        return if (locales.isEmpty) {
            null // System default
        } else {
            locales.get(0)
        }
    }

    /**
     * Change application language.
     *
     * This is the critical function that uses AppCompatDelegate.setApplicationLocales().
     * It automatically:
     * - Saves the locale preference (with autoStoreLocales=true)
     * - Recreates the activity
     * - Updates all string resources
     * - Works on ALL Android versions (API 24+)
     *
     * @param languageCode Language code (e.g., "en", "es", "pt-rBR") or "system" for system default
     */
    fun changeLanguage(languageCode: String) {
        // Get current language code to avoid unnecessary recreation
        val currentCode = getCurrentLanguageCode()

        // Only change if different
        if (currentCode != languageCode) {
            val appLocale = if (languageCode == "system") {
                LocaleListCompat.getEmptyLocaleList()
            } else {
                // Convert our format (e.g., "pt-rBR") to BCP 47 format (e.g., "pt-BR")
                val bcp47Tag = languageCode.replace("-r", "-")
                LocaleListCompat.forLanguageTags(bcp47Tag)
            }

            // This automatically:
            // - Saves the locale preference (with autoStoreLocales=true)
            // - Recreates the activity
            // - Updates all string resources
            // - Works on ALL Android versions (API 24+)
            AppCompatDelegate.setApplicationLocales(appLocale)
        }
    }

    /**
     * Get supported locales from resources.
     * Returns list of Locale objects including system default (Locale.ROOT).
     */
    fun getSupportedLocales(context: Context): List<Locale> {
        val locales = mutableListOf<Locale>()

        // Add system default first
        locales.add(Locale.ROOT) // Represents "System Default"

        // Known supported locales
        val knownLocales = listOf(
            "en",      // English
            "es",      // Spanish
            "pt-rBR",  // Portuguese (Brazil)
            "ru",      // Russian
            "fil",     // Tagalog (Filipino)
            "hi",      // Hindi
            "ilo-rPH",  // Ilocano (Philippines)
            "cbk-rPH", // Chavacano (Philippines)
            "ceb-rPH", // Cebuano (Philippines)
            "pag-rPH", // Pangasinan (Philippines)
            "de",      // German
            "fr"      // French
        )

        knownLocales.forEach { localeTag ->
            try {
                val locale = when {
                    localeTag.contains("-r") -> {
                        val parts = localeTag.split("-r")
                        Locale.Builder()
                            .setLanguage(parts[0])
                            .setRegion(parts[1])
                            .build()
                    }
                    else -> Locale.Builder()
                        .setLanguage(localeTag)
                        .build()
                }

                // Test if this locale has translated resources
                val config = Configuration(context.resources.configuration)
                config.setLocale(locale)
                val localizedContext = context.createConfigurationContext(config)

                // Verify locale is supported by checking if we can get a translated string
                try {
                    val testString = localizedContext.getString(com.droidspaces.app.R.string.app_name)
                    if (locale.language == "en" || testString.isNotEmpty()) {
                        locales.add(locale)
                    }
                } catch (_: Exception) {
                    // Skip unsupported locales
                }
            } catch (_: Exception) {
                // Skip invalid locales
            }
        }

        // Sort by display name (excluding system default)
        val sortedLocales = locales.drop(1).sortedBy { it.getDisplayName(it) }
        return mutableListOf<Locale>().apply {
            add(locales.first()) // System default first
            addAll(sortedLocales)
        }
    }

    /**
     * Legacy compatibility: Check if should use system language settings.
     * With AppCompatDelegate.setApplicationLocales(), this is no longer needed,
     * but kept for backward compatibility.
     */
    @Deprecated("No longer needed with AppCompatDelegate.setApplicationLocales()")
    val useSystemLanguageSettings: Boolean
        get() = false

    /**
     * Legacy compatibility: Launch system app locale settings.
     * With AppCompatDelegate.setApplicationLocales(), this is no longer needed,
     * but kept for backward compatibility.
     */
    @Deprecated("No longer needed with AppCompatDelegate.setApplicationLocales()")
    @Suppress("UNUSED_PARAMETER")
    fun launchSystemLanguageSettings(context: Context) {
        // No-op: AppCompatDelegate handles everything
    }

    /**
     * Legacy compatibility: Set application locale.
     * Now uses changeLanguage() internally.
     */
    @Deprecated("Use changeLanguage() instead", ReplaceWith("changeLanguage(localeTag)"))
    fun setApplicationLocale(localeTag: String) {
        changeLanguage(localeTag)
    }

    /**
     * Legacy compatibility: Restart activity.
     * No longer needed - AppCompatDelegate.setApplicationLocales() handles this automatically.
     */
    @Deprecated("No longer needed - AppCompatDelegate.setApplicationLocales() handles this automatically")
    @Suppress("UNUSED_PARAMETER")
    fun restartActivity(context: Context) {
        // No-op: AppCompatDelegate handles activity recreation automatically
    }

    /**
     * Legacy compatibility: Apply language to context.
     * No longer needed with AppCompatDelegate.setApplicationLocales().
     */
    @Deprecated("No longer needed with AppCompatDelegate.setApplicationLocales()")
    fun applyLanguage(context: Context): Context {
        // No-op: AppCompatDelegate handles everything automatically
        return context
    }
}
