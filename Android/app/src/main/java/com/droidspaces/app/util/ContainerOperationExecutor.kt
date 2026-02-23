package com.droidspaces.app.util

import android.util.Log
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.ensureActive

/**
 * Executes container operations (start/stop/restart) using logger callback pattern.
 * Optimized for real-time log streaming with zero overhead.
 *
 * Note: libsu's Shell.cmd().exec() waits for command completion, so true real-time
 * streaming isn't possible. However, we optimize by processing output efficiently
 * and ensuring logs are added to the UI state immediately.
 */
object ContainerOperationExecutor {
    /**
     * Execute a container command and log output using logger callback.
     * Optimized for performance - processes output efficiently and logs immediately.
     * The logger callback should handle thread safety (ViewModelLogger uses Main dispatcher).
     */
    suspend fun executeCommand(
        command: String,
        operation: String, // "start", "stop", "restart", "check"
        logger: ContainerLogger,
        skipHeader: Boolean = false // Skip "Starting..." messages for operations like "check"
    ) = withContext(Dispatchers.IO) {
        try {
            if (!skipHeader) {
                logger.i("Starting ${operation} operation...")
                logger.i("Command: $command")
                logger.i("")
            }

            // Execute command - capture both stdout and stderr
            // Use 2>&1 to redirect stderr to stdout for unified output
            val result = Shell.cmd("$command 2>&1").exec()

            // Process output lines efficiently - log immediately for real-time feel
            // Pre-compile regex patterns for better performance
            val errorPattern = Regex("""(?i)(error|failed|fail)""")

            // Track if last line was empty to avoid double newlines
            var lastLineWasEmpty = false

            if (result.out.isNotEmpty()) {
                result.out.forEach { line ->
                    // Check cancellation to avoid unnecessary work
                    ensureActive()

                    // Preserve empty lines - they are important for formatting
                    // Only trim for error detection, but log the original line
                    val trimmed = line.trim()
                    if (trimmed.isEmpty()) {
                        // Empty line - log as-is to preserve formatting
                        logger.i("")
                        lastLineWasEmpty = true
                    } else {
                        // Determine log level based on content (error keywords)
                        // Use regex for efficient pattern matching
                        if (errorPattern.containsMatchIn(trimmed)) {
                            logger.e(line) // Log original line to preserve formatting
                        } else {
                            logger.i(line) // Log original line to preserve formatting
                        }
                        lastLineWasEmpty = false
                    }
                }
            } else if (result.err.isNotEmpty()) {
                // If no stdout but has stderr, log stderr as errors
                result.err.forEach { line ->
                    ensureActive()
                    val trimmed = line.trim()
                    if (trimmed.isEmpty()) {
                        // Empty line - log as-is to preserve formatting
                        logger.e("")
                        lastLineWasEmpty = true
                    } else {
                        logger.e(line) // Log original line to preserve formatting
                        lastLineWasEmpty = false
                    }
                }
            }

            // Log result status - only add newline if last output line wasn't empty
            // Always show result status, but skip header messages for operations like "check"
            if (!lastLineWasEmpty) {
                logger.i("")
            }
            if (result.isSuccess) {
                logger.i("Command executed (exit code: ${result.code})")
            } else {
                logger.e("Command failed (exit code: ${result.code})")
            }

            result.isSuccess
        } catch (e: Exception) {
            logger.e("Exception executing command: ${e.message}")
            logger.e(e.stackTraceToString())
            false
        }
    }

    /**
     * Check if a command execution was successful.
     */
    suspend fun checkCommandSuccess(command: String): Boolean = withContext(Dispatchers.IO) {
        try {
            val result = Shell.cmd("$command 2>&1").exec()
            result.isSuccess
        } catch (e: Exception) {
            false
        }
    }
}

