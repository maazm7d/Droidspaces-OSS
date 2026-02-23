package com.droidspaces.app.ui.viewmodel

import android.app.Application
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.droidspaces.app.util.DroidspacesBackendStatus
import com.droidspaces.app.util.DroidspacesChecker
import com.droidspaces.app.util.PreferencesManager
import com.droidspaces.app.util.RootChecker
import com.droidspaces.app.util.RootStatus
import com.droidspaces.app.util.SystemInfoManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Application-wide state ViewModel that persists across navigation.
 *
 * This ViewModel solves the "settings back button glitch" by:
 * 1. Persisting backend status across navigation (no re-check on back press)
 * 2. Using proper Compose state management (mutableStateOf inside ViewModel)
 * 3. Single source of truth for app-wide state
 *
 * Performance optimizations:
 * - Single check on app cold start
 * - No redundant checks on navigation
 * - Efficient coroutine usage with cancellation support
 * - Cache status in preferences for instant display on restart
 */
class AppStateViewModel(application: Application) : AndroidViewModel(application) {
    private val prefsManager = PreferencesManager.getInstance(application)

    // Backend status - persists across navigation
    // Explicit type annotation required for sealed class
    // Initialize with cached status to prevent UI glitches on boot
    var backendStatus: DroidspacesBackendStatus by mutableStateOf(
        prefsManager.cachedBackendStatus?.let { cached ->
            when (cached) {
                "UpdateAvailable" -> DroidspacesBackendStatus.UpdateAvailable
                "NotInstalled" -> DroidspacesBackendStatus.NotInstalled
                "Corrupted" -> DroidspacesBackendStatus.Corrupted
                "ModuleMissing" -> DroidspacesBackendStatus.ModuleMissing
                "Checking" -> DroidspacesBackendStatus.Checking
                "" -> DroidspacesBackendStatus.Available
                else -> DroidspacesBackendStatus.Checking
            }
        } ?: DroidspacesBackendStatus.Checking
    )
        private set

    // Track if initial check has been completed (persists across navigation)
    var hasCompletedInitialCheck by mutableStateOf(prefsManager.cachedBackendStatus != null)
        private set

    // Track if a refresh is in progress
    var isRefreshing by mutableStateOf(false)
        private set

    // Root access status - initialize with cached status
    var rootStatus: RootStatus by mutableStateOf(
        if (prefsManager.rootAvailable) RootStatus.Granted else RootStatus.Denied
    )
        private set

    // Current refresh job for cancellation
    private var refreshJob: Job? = null

    fun checkBackendStatus(force: Boolean = false) {
        if (hasCompletedInitialCheck && !force) return

        refreshJob?.cancel()

        refreshJob = viewModelScope.launch {
            val previousStatus = backendStatus
            isRefreshing = true

            if (previousStatus == DroidspacesBackendStatus.Checking) {
                backendStatus = DroidspacesBackendStatus.Checking
            }

            try {
                val status = withContext(Dispatchers.IO) {
                    DroidspacesChecker.checkBackendStatus()
                }

                val finalStatus = if (status == DroidspacesBackendStatus.Available) {
                    val updateAvailable = withContext(Dispatchers.IO) {
                        DroidspacesChecker.checkUpdateAvailable(getApplication())
                    }
                    if (updateAvailable) DroidspacesBackendStatus.UpdateAvailable else DroidspacesBackendStatus.Available
                } else {
                    status
                }

                if (finalStatus != previousStatus) {
                    backendStatus = finalStatus
                }

                hasCompletedInitialCheck = true

                // Only cache non-Checking statuses to prevent UI glitches
                if (finalStatus !is DroidspacesBackendStatus.Checking) {
                    prefsManager.cachedBackendStatus = when (finalStatus) {
                        DroidspacesBackendStatus.UpdateAvailable -> "UpdateAvailable"
                        DroidspacesBackendStatus.NotInstalled -> "NotInstalled"
                        DroidspacesBackendStatus.Corrupted -> "Corrupted"
                        DroidspacesBackendStatus.ModuleMissing -> "ModuleMissing"
                        DroidspacesBackendStatus.Available -> ""
                        DroidspacesBackendStatus.Checking -> "" // Should not happen, but handle gracefully
                    }
                }
            } catch (e: Exception) {
                if (previousStatus != DroidspacesBackendStatus.NotInstalled) {
                    backendStatus = DroidspacesBackendStatus.NotInstalled
                }
                hasCompletedInitialCheck = true
            } finally {
                isRefreshing = false
            }
        }
    }

    /**
     * Force a refresh of backend status.
     * Use this for pull-to-refresh or after installation.
     */
    fun forceRefresh() {
        checkBackendStatus(force = true)
    }

    /**
     * Reset state for post-installation refresh.
     * This forces a new check on next composition.
     */
    fun resetForPostInstallation() {
        hasCompletedInitialCheck = false
        backendStatus = DroidspacesBackendStatus.Checking
        // Clear version cache to force refresh after backend installation/update
        SystemInfoManager.resetCache()
    }

    /**
     * Check if backend is available (working or update available).
     */
    val isBackendAvailable: Boolean
        get() = backendStatus == DroidspacesBackendStatus.Available ||
                backendStatus == DroidspacesBackendStatus.UpdateAvailable

    /**
     * Check root access status.
     */
    fun checkRootStatus() {
        viewModelScope.launch {
            rootStatus = withContext(Dispatchers.IO) {
                RootChecker.checkRootAccess()
            }
            prefsManager.rootAvailable = rootStatus == RootStatus.Granted
        }
    }

    /**
     * Check if root is available.
     */
    val isRootAvailable: Boolean
        get() = rootStatus == RootStatus.Granted

    init {
        // Check root status on initialization
        checkRootStatus()
    }
}

