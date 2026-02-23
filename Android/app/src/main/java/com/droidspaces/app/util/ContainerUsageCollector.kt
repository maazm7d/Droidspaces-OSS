package com.droidspaces.app.util

import android.util.Log
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.regex.Pattern

/**
 * Collects real-time usage statistics from running containers.
 * Similar to Server Box's approach - executes commands inside containers and parses output.
 */
object ContainerUsageCollector {
    private const val TAG = "ContainerUsageCollector"

    /**
     * Container usage statistics.
     */
    data class ContainerUsage(
        val cpuPercent: Double = 0.0,
        val ramPercent: Double = 0.0,
        val networkSpeed: String = "0 KB/s",
        val diskSpeed: String = "0 KB/s"
    )

    /**
     * Collect usage statistics for a running container.
     * Executes commands inside the container using 'droidspaces run'.
     */
    suspend fun collectUsage(containerName: String): ContainerUsage = withContext(Dispatchers.IO) {
        try {
            val cpuPercent = getCpuUsage(containerName)
            val ramPercent = getRamUsage(containerName)
            val networkSpeed = getNetworkSpeed(containerName)
            val diskSpeed = getDiskSpeed(containerName)

            ContainerUsage(
                cpuPercent = cpuPercent,
                ramPercent = ramPercent,
                networkSpeed = networkSpeed,
                diskSpeed = diskSpeed
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to collect usage for $containerName", e)
            ContainerUsage() // Return zeros on error
        }
    }

    /**
     * Get CPU usage percentage from container.
     * Uses 'top' or 'cat /proc/stat' inside container.
     */
    private suspend fun getCpuUsage(containerName: String): Double = withContext(Dispatchers.IO) {
        try {
            // Execute 'top -bn1' inside container to get CPU usage
            val result = Shell.cmd(
                "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run 'top -bn1 | grep \"Cpu(s)\" | head -1'"
            ).exec()

            if (result.isSuccess && result.out.isNotEmpty()) {
                val output = result.out.joinToString(" ")
                // Parse CPU percentage from output like "Cpu(s): 12.5%us,  2.3%sy,  0.0%ni, 85.0%id"
                val pattern = Pattern.compile("(\\d+\\.?\\d*)%us")
                val matcher = pattern.matcher(output)
                if (matcher.find()) {
                    val userPercent = matcher.group(1)?.toDoubleOrNull() ?: 0.0
                    // Add system percentage if available
                    val sysPattern = Pattern.compile("(\\d+\\.?\\d*)%sy")
                    val sysMatcher = sysPattern.matcher(output)
                    val sysPercent = if (sysMatcher.find()) sysMatcher.group(1)?.toDoubleOrNull() ?: 0.0 else 0.0
                    return@withContext (userPercent + sysPercent).coerceIn(0.0, 100.0)
                }
            }

            // Fallback: Try /proc/stat method
            val statResult = Shell.cmd(
                "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run 'cat /proc/stat | head -1'"
            ).exec()

            if (statResult.isSuccess && statResult.out.isNotEmpty()) {
                // Parse /proc/stat format: cpu  1234 567 890 12345 67 89 12 34
                val parts = statResult.out[0].trim().split("\\s+".toRegex())
                if (parts.size >= 8) {
                    val user = parts[1].toLongOrNull() ?: 0L
                    val nice = parts[2].toLongOrNull() ?: 0L
                    val system = parts[3].toLongOrNull() ?: 0L
                    val idle = parts[4].toLongOrNull() ?: 0L
                    val total = user + nice + system + idle
                    if (total > 0) {
                        val used = user + nice + system
                        return@withContext ((used.toDouble() / total.toDouble()) * 100.0).coerceIn(0.0, 100.0)
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error getting CPU usage", e)
        }
        0.0
    }

    /**
     * Get RAM usage percentage from container.
     * Uses 'free' or 'cat /proc/meminfo' inside container.
     */
    private suspend fun getRamUsage(containerName: String): Double = withContext(Dispatchers.IO) {
        try {
            // Execute 'free' inside container
            val result = Shell.cmd(
                "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run 'free | grep Mem'"
            ).exec()

            if (result.isSuccess && result.out.isNotEmpty()) {
                val output = result.out[0].trim()
                // Parse: "Mem:  8192000  5242880  2949120        0   102400  4194304"
                val parts = output.split("\\s+".toRegex())
                if (parts.size >= 3) {
                    val total = parts[1].toLongOrNull() ?: 0L
                    val used = parts[2].toLongOrNull() ?: 0L
                    if (total > 0) {
                        return@withContext ((used.toDouble() / total.toDouble()) * 100.0).coerceIn(0.0, 100.0)
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error getting RAM usage", e)
        }
        0.0
    }

    /**
     * Get network speed from container.
     * Uses 'cat /proc/net/dev' inside container.
     */
    private suspend fun getNetworkSpeed(containerName: String): String = withContext(Dispatchers.IO) {
        try {
            // Get network stats - this is a simplified version
            // In production, you'd track previous values to calculate speed
            val result = Shell.cmd(
                "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run 'cat /proc/net/dev | grep -E \"eth0|enp|wlan\" | head -1'"
            ).exec()

            if (result.isSuccess && result.out.isNotEmpty()) {
                val output = result.out[0].trim()
                // Parse network bytes (simplified - would need time-based calculation for speed)
                val parts = output.split("\\s+".toRegex())
                if (parts.size >= 10) {
                    val bytes = parts[1].toLongOrNull() ?: 0L
                    return@withContext formatBytes(bytes)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error getting network speed", e)
        }
        "0 KB/s"
    }

    /**
     * Get disk I/O speed from container.
     * Uses 'iostat' or 'cat /proc/diskstats' inside container.
     */
    private suspend fun getDiskSpeed(containerName: String): String = withContext(Dispatchers.IO) {
        try {
            // Get disk stats - simplified version
            val result = Shell.cmd(
                "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run 'cat /proc/diskstats | grep -E \"sda|nvme\" | head -1'"
            ).exec()

            if (result.isSuccess && result.out.isNotEmpty()) {
                val output = result.out[0].trim()
                // Parse disk sectors read (simplified - would need time-based calculation)
                val parts = output.split("\\s+".toRegex())
                if (parts.size >= 6) {
                    val sectorsRead = parts[5].toLongOrNull() ?: 0L
                    val bytes = sectorsRead * 512 // Convert sectors to bytes
                    return@withContext formatBytes(bytes)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error getting disk speed", e)
        }
        "0 KB/s"
    }

    /**
     * Format bytes to human-readable string.
     */
    private fun formatBytes(bytes: Long): String {
        val kb = bytes / 1024.0
        val mb = kb / 1024.0
        val gb = mb / 1024.0

        return when {
            gb >= 1.0 -> String.format("%.1f GB/s", gb)
            mb >= 1.0 -> String.format("%.1f MB/s", mb)
            kb >= 1.0 -> String.format("%.1f KB/s", kb)
            else -> "$bytes B/s"
        }
    }
}

