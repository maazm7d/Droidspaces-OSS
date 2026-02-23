package com.droidspaces.app.receiver

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log
import com.droidspaces.app.util.ContainerManager
import com.droidspaces.app.util.ContainerCommandBuilder
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/**
 * BroadcastReceiver that starts containers with run_at_boot=1 on device boot.
 */
class BootReceiver : BroadcastReceiver() {
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private val TAG = "BootReceiver"

    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action == Intent.ACTION_BOOT_COMPLETED) {
            Log.i(TAG, "Boot completed, checking for containers to auto-start...")

            scope.launch {
                try {
                    // Get all containers
                    val containers = ContainerManager.listContainers()

                    // Filter containers with run_at_boot=1
                    val containersToStart = containers.filter { it.runAtBoot }

                    if (containersToStart.isEmpty()) {
                        Log.i(TAG, "No containers configured to start at boot")
                        return@launch
                    }

                    Log.i(TAG, "Found ${containersToStart.size} container(s) to start at boot")

                    // Start each container
                    containersToStart.forEach { container ->
                        try {
                            Log.i(TAG, "Starting container at boot: ${container.name}")
                            val startCommand = ContainerCommandBuilder.buildStartCommand(container)
                            val result = Shell.cmd("$startCommand 2>&1").exec()

                            if (result.isSuccess) {
                                Log.i(TAG, "Successfully started container: ${container.name}")
                            } else {
                                Log.e(TAG, "Failed to start container: ${container.name}, exit code: ${result.code}")
                                result.err.forEach { Log.e(TAG, "Error: $it") }
                            }
                        } catch (e: Exception) {
                            Log.e(TAG, "Exception starting container: ${container.name}", e)
                        }
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "Error during boot auto-start", e)
                }
            }
        }
    }
}

