package com.droidspaces.app.ui.viewmodel

import android.app.Application
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.droidspaces.app.util.AndroidSystemStatsCollector
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

/**
 * ViewModel for tracking real-time Android system statistics.
 * Updates stats periodically from Android /proc files.
 */
class SystemStatsViewModel(application: Application) : AndroidViewModel(application) {

    companion object {
        private const val TAG = "SystemStatsViewModel"
        private const val UPDATE_INTERVAL_MS = 2000L // Update every 2 seconds
    }

    // Current system usage stats
    var systemUsage by mutableStateOf(AndroidSystemStatsCollector.SystemUsage())
        private set

    // Update job - allows cancellation
    private var updateJob: Job? = null

    /**
     * Start monitoring system statistics.
     */
    fun startMonitoring() {
        // Cancel existing job
        updateJob?.cancel()

        // Start new monitoring loop
        updateJob = viewModelScope.launch(Dispatchers.IO) {
            while (true) {
                try {
                    val usage = AndroidSystemStatsCollector.collectUsage()
                    systemUsage = usage
                } catch (e: Exception) {
                    Log.e(TAG, "Failed to collect system usage", e)
                }

                // Wait before next update
                delay(UPDATE_INTERVAL_MS)
            }
        }
    }

    /**
     * Stop monitoring system statistics.
     */
    fun stopMonitoring() {
        updateJob?.cancel()
        updateJob = null
    }

    override fun onCleared() {
        super.onCleared()
        stopMonitoring()
    }
}

