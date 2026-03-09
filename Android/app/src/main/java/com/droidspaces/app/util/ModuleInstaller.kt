package com.droidspaces.app.util

import android.content.Context
import android.util.Log
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

sealed class ModuleInstallationStep {
    data class RemovingOldModule(val path: String) : ModuleInstallationStep()
    data class ExtractingAssets(val path: String) : ModuleInstallationStep()
    data class CopyingModule(val path: String) : ModuleInstallationStep()
    data class SettingPermissions(val path: String) : ModuleInstallationStep()
    data class Verifying(val path: String) : ModuleInstallationStep()
    object Success : ModuleInstallationStep()
    data class Error(val message: String) : ModuleInstallationStep()
}

object ModuleInstaller {
    private const val MAGISK_MODULE_PATH = "/data/adb/modules/droidspaces"
    private const val MODULE_PROP_PATH = "$MAGISK_MODULE_PATH/module.prop"

    /**
     * Install Magisk module from assets to /data/adb/modules/droidspaces
     * Simple approach: remove old, extract assets to temp, copy everything
     */
    suspend fun install(
        context: Context,
        onProgress: (ModuleInstallationStep) -> Unit
    ): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            val tempDir = File(context.cacheDir, "boot-module-temp")
            val assetManager = context.assets

            // Step 1: Remove old module directory
            onProgress(ModuleInstallationStep.RemovingOldModule(MAGISK_MODULE_PATH))
            Shell.cmd("rm -rf '$MAGISK_MODULE_PATH' 2>&1").exec()

            // Step 2: Extract all assets from boot-module to temp directory
            onProgress(ModuleInstallationStep.ExtractingAssets(tempDir.absolutePath))
            tempDir.deleteRecursively()
            tempDir.mkdirs()

            try {
                val assetFiles = assetManager.list("boot-module") ?: emptyArray()
                for (fileName in assetFiles) {
                    val assetPath = "boot-module/$fileName"
                    val tempFile = File(tempDir, fileName)
                    assetManager.open(assetPath).use { inputStream ->
                        FileOutputStream(tempFile).use { output ->
                            inputStream.copyTo(output)
                        }
                    }
                }
            } catch (e: Exception) {
                tempDir.deleteRecursively()
                return@withContext Result.failure(
                    Exception("Failed to extract assets: ${e.message}")
                )
            }

            // Step 3: Create module directory and copy everything
            onProgress(ModuleInstallationStep.CopyingModule(MAGISK_MODULE_PATH))
            val mkdirResult = Shell.cmd("mkdir -p '$MAGISK_MODULE_PATH' 2>&1").exec()
            if (!mkdirResult.isSuccess) {
                tempDir.deleteRecursively()
                return@withContext Result.failure(
                    Exception("Failed to create module directory: ${mkdirResult.err.joinToString()}")
                )
            }

            val copyResult = Shell.cmd("cp -arf '${tempDir.absolutePath}'/* '$MAGISK_MODULE_PATH/' 2>&1").exec()
            tempDir.deleteRecursively()
            if (!copyResult.isSuccess) {
                return@withContext Result.failure(
                    Exception("Failed to copy module files: ${copyResult.err.joinToString()}")
                )
            }

            // Step 4: Set permissions
            onProgress(ModuleInstallationStep.SettingPermissions(MAGISK_MODULE_PATH))
            val chmodScriptsResult = Shell.cmd("chmod 755 '$MAGISK_MODULE_PATH'/*.sh 2>&1 && chmod 644 '$MAGISK_MODULE_PATH'/*.prop 2>&1").exec()
            if (!chmodScriptsResult.isSuccess) {
                return@withContext Result.failure(
                    Exception("Failed to set permissions: ${chmodScriptsResult.err.joinToString()}")
                )
            }

            // Step 5: Create system/bin directory and symlink (non-critical, warnings only)
            val systemBinDir = "$MAGISK_MODULE_PATH/system/bin"
            val symlinkPath = "$systemBinDir/droidspaces"
            val mkdirBinResult = Shell.cmd("mkdir -p '$systemBinDir' 2>&1").exec()
            if (!mkdirBinResult.isSuccess) {
                Log.w("ModuleInstaller", "Warning: Failed to create system/bin directory: ${mkdirBinResult.err.joinToString()}")
            } else {
                // Create symlink from system/bin/droidspaces to the actual binary
                val binaryPath = Constants.DROIDSPACES_BINARY_PATH
                // Remove existing symlink/file if it exists
                Shell.cmd("rm -f '$symlinkPath' 2>&1").exec()
                // Create symlink
                val symlinkResult = Shell.cmd("ln -sf '$binaryPath' '$symlinkPath' 2>&1").exec()
                if (!symlinkResult.isSuccess) {
                    Log.w("ModuleInstaller", "Warning: Failed to create symlink from $symlinkPath to $binaryPath: ${symlinkResult.err.joinToString()}")
                } else {
                    // Ensure symlink is executable (though it should inherit from target)
                    val chmodSymlinkResult = Shell.cmd("chmod 755 '$symlinkPath' 2>&1").exec()
                    if (!chmodSymlinkResult.isSuccess) {
                        Log.w("ModuleInstaller", "Warning: Failed to set symlink permissions: ${chmodSymlinkResult.err.joinToString()}")
                    } else {
                        Log.i("ModuleInstaller", "Successfully created symlink: $symlinkPath -> $binaryPath")
                    }
                }
            }

            // Step 6: Verify installation
            onProgress(ModuleInstallationStep.Verifying(MAGISK_MODULE_PATH))
            val verifyDirResult = Shell.cmd("test -d '$MAGISK_MODULE_PATH' 2>&1").exec()
            val verifyPropResult = Shell.cmd("test -f '$MODULE_PROP_PATH' 2>&1").exec()

            if (!verifyDirResult.isSuccess) {
                return@withContext Result.failure(
                    Exception("Module directory verification failed")
                )
            }

            if (!verifyPropResult.isSuccess) {
                return@withContext Result.failure(
                    Exception("module.prop verification failed")
                )
            }

            // Symlink verification is non-critical - just log a warning if it doesn't exist
            val verifySymlinkResult = Shell.cmd("test -L '$symlinkPath' 2>&1").exec()
            if (!verifySymlinkResult.isSuccess) {
                Log.w("ModuleInstaller", "Warning: Symlink verification failed - symlink may not be available in system PATH")
            }

            onProgress(ModuleInstallationStep.Success)
            Result.success(Unit)
        } catch (e: Exception) {
            Result.failure(e)
        }
    }
}
