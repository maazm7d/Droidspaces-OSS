package com.droidspaces.app.util

import com.topjohnwu.superuser.Shell
import com.topjohnwu.superuser.ShellUtils
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

enum class RootStatus {
    Checking,
    Granted,
    Denied
}

object RootChecker {
    /**
     * Optimized root check with minimal allocations and fast path for already-granted root.
     */
    suspend fun checkRootAccess(): RootStatus = withContext(Dispatchers.IO) {
        return@withContext try {
            // Fast path: check if root is already granted (cached check, no allocation)
            val isRootGranted = Shell.isAppGrantedRoot() == true

            if (isRootGranted) {
                // Use fastCmdResult for verification (minimal overhead)
                if (ShellUtils.fastCmdResult("id")) {
                    RootStatus.Granted
                } else {
                    RootStatus.Denied
                }
            } else {
                // Try to request root (will show dialog)
                try {
                    val result = Shell.cmd("id").exec()
                    if (result.isSuccess && Shell.isAppGrantedRoot() == true) {
                        RootStatus.Granted
                    } else {
                        RootStatus.Denied
                    }
                } catch (e: Exception) {
                RootStatus.Denied
                }
            }
        } catch (e: Exception) {
            RootStatus.Denied
        }
    }

    fun checkRootAccessSync(): RootStatus {
        return try {
            val isRootAvailable = Shell.isAppGrantedRoot() == true
            if (isRootAvailable) {
                val verifyResult = ShellUtils.fastCmdResult("id")
                if (verifyResult) {
                    RootStatus.Granted
                } else {
                    RootStatus.Denied
                }
            } else {
                RootStatus.Denied
            }
        } catch (e: Exception) {
            RootStatus.Denied
        }
    }
}

