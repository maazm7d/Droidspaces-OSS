package com.droidspaces.app.util

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Logger interface for container operations (inspired by LSPatch Logger pattern).
 * Allows real-time log streaming to UI with zero overhead.
 */
abstract class ContainerLogger {
    abstract suspend fun d(msg: String)
    abstract suspend fun i(msg: String)
    abstract suspend fun e(msg: String)
    abstract suspend fun w(msg: String)

    var verbose: Boolean = false
}

/**
 * ViewModel logger implementation that captures logs for UI display.
 * Optimized for real-time streaming - ensures all log updates are on Main thread
 * for instant UI responsiveness. Uses suspend functions to allow efficient
 * thread switching without creating coroutines for each message.
 */
class ViewModelLogger(
    private val onLog: suspend (Int, String) -> Unit
) : ContainerLogger() {
    override suspend fun d(msg: String) {
        if (verbose) {
            Log.d("ContainerLogger", msg)
            // Switch to main thread for UI update
            withContext(Dispatchers.Main.immediate) {
            onLog(Log.DEBUG, msg)
            }
        }
    }

    override suspend fun i(msg: String) {
        Log.i("ContainerLogger", msg)
        // Switch to main thread for UI update
        withContext(Dispatchers.Main.immediate) {
        onLog(Log.INFO, msg)
        }
    }

    override suspend fun e(msg: String) {
        Log.e("ContainerLogger", msg)
        // Switch to main thread for UI update
        withContext(Dispatchers.Main.immediate) {
        onLog(Log.ERROR, msg)
        }
    }

    override suspend fun w(msg: String) {
        Log.w("ContainerLogger", msg)
        // Switch to main thread for UI update
        withContext(Dispatchers.Main.immediate) {
        onLog(Log.WARN, msg)
        }
    }
}

