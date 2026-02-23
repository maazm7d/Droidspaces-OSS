package com.droidspaces.app.ui.screen

import androidx.compose.animation.core.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.droidspaces.app.util.BinaryInstaller
import com.droidspaces.app.util.InstallationStep
import com.droidspaces.app.util.ModuleInstaller
import com.droidspaces.app.util.ModuleInstallationStep
import com.droidspaces.app.util.DroidspacesChecker
import com.droidspaces.app.util.DroidspacesBackendStatus
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import com.droidspaces.app.R

@Composable
fun InstallationScreen(
    onInstallationComplete: () -> Unit
) {
    val context = LocalContext.current

    var currentStep by remember { mutableStateOf<InstallationStep?>(null) }
    var currentModuleStep by remember { mutableStateOf<ModuleInstallationStep?>(null) }
    var isInstalling by remember { mutableStateOf(false) }
    var isSuccess by remember { mutableStateOf(false) }
    var errorMessage by remember { mutableStateOf<String?>(null) }
    var isInstallingModule by remember { mutableStateOf(false) }

    // Check backend status and determine what to install
    LaunchedEffect(Unit) {
        if (!isInstalling && !isSuccess) {
            val backendStatus = withContext(Dispatchers.IO) {
                DroidspacesChecker.checkBackendStatus()
            }

            isInstalling = true

            when (backendStatus) {
                is DroidspacesBackendStatus.ModuleMissing -> {
                    // Install module only
                    isInstallingModule = true
                    val result = ModuleInstaller.install(context) { step ->
                        currentModuleStep = step
                    }
                    result.fold(
                        onSuccess = {
                            isSuccess = true
                            isInstalling = false
                            isInstallingModule = false
                        },
                        onFailure = { error ->
                            errorMessage = error.message ?: context.getString(R.string.module_installation_failed)
                            isInstalling = false
                            isInstallingModule = false
                        }
                    )
                }
                else -> {
                    // Reinstall: remove module, install binaries, then install module
                    isInstallingModule = false

                    // Step 1: Remove old module
                    currentStep = InstallationStep.CreatingDirectories("/data/adb/modules/droidspaces")
                    Shell.cmd("rm -rf '/data/adb/modules/droidspaces' 2>&1").exec()

                    // Step 2: Install binaries
                    val binaryResult = BinaryInstaller.install(context) { step ->
                        currentStep = step
                    }
                    binaryResult.fold(
                        onSuccess = {
                            // Step 3: Install module
                            isInstallingModule = true
                            val moduleResult = ModuleInstaller.install(context) { step ->
                                currentModuleStep = step
                            }
                            moduleResult.fold(
                                onSuccess = {
                                    isSuccess = true
                                    isInstalling = false
                                    isInstallingModule = false
                                },
                                onFailure = { error ->
                                    errorMessage = error.message ?: context.getString(R.string.module_installation_failed)
                                    isInstalling = false
                                    isInstallingModule = false
                                }
                            )
                        },
                        onFailure = { error ->
                            errorMessage = error.message ?: context.getString(R.string.binary_installation_failed)
                            isInstalling = false
                        }
                    )
                }
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        // Main icon with animation
        InstallationIcon(
            isSuccess = isSuccess,
            hasError = errorMessage != null
        )

        Spacer(modifier = Modifier.height(32.dp))

        // Title
        Text(
            text = when {
                isSuccess -> context.getString(R.string.installation_complete)
                errorMessage != null -> context.getString(R.string.installation_failed)
                isInstallingModule -> context.getString(R.string.installing_module)
                else -> context.getString(R.string.installing_droidspaces)
            },
            style = MaterialTheme.typography.headlineMedium,
            fontWeight = FontWeight.Bold,
            textAlign = TextAlign.Center
        )

        Spacer(modifier = Modifier.height(16.dp))

        // Status messages in a card (MMRL style)
        ElevatedCard(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.elevatedCardColors(
                containerColor = MaterialTheme.colorScheme.surfaceVariant
            ),
            shape = RoundedCornerShape(20.dp),
            elevation = CardDefaults.elevatedCardElevation(defaultElevation = 1.dp)
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(20.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                when {
                    isSuccess -> {
                        Text(
                            text = if (isInstallingModule) {
                                context.getString(R.string.module_installed_success)
                            } else {
                                context.getString(R.string.backend_installed_success)
                            },
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                            textAlign = TextAlign.Center
                        )
                    }
                    errorMessage != null -> {
                        Text(
                            text = errorMessage ?: context.getString(R.string.unknown_error),
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.error,
                            textAlign = TextAlign.Center
                        )
                    }
                    else -> {
                        // Show current step
                        if (isInstallingModule) {
                            when (currentModuleStep) {
                                is ModuleInstallationStep.RemovingOldModule -> {
                                    StepText(context.getString(R.string.removing_old_module))
                                }
                                is ModuleInstallationStep.ExtractingAssets -> {
                                    StepText(context.getString(R.string.extracting_module_files))
                                }
                                is ModuleInstallationStep.CopyingModule -> {
                                    StepText(context.getString(R.string.installing_module_step))
                                }
                                is ModuleInstallationStep.SettingPermissions -> {
                                    StepText(context.getString(R.string.setting_permissions))
                                }
                                is ModuleInstallationStep.Verifying -> {
                                    StepText(context.getString(R.string.verifying_installation))
                                }
                                else -> {
                                    StepText(context.getString(R.string.preparing_module_installation))
                                }
                            }
                        } else {
                            when (val step = currentStep) {
                                is InstallationStep.DetectingArchitecture -> {
                                    StepText(context.getString(R.string.detected_architecture, step.arch))
                                }
                                is InstallationStep.CreatingDirectories -> {
                                    StepText(context.getString(R.string.creating_directories))
                                }
                                is InstallationStep.CopyingBinary -> {
                                    StepText(context.getString(R.string.installing_binary, step.binary))
                                }
                                is InstallationStep.SettingPermissions -> {
                                    StepText(context.getString(R.string.granting_permissions))
                                }
                                is InstallationStep.Verifying -> {
                                    StepText(context.getString(R.string.verifying_installation))
                                }
                                else -> {
                                    StepText(context.getString(R.string.preparing_installation))
                                }
                            }
                        }
                    }
                }
            }
        }

        // Success button (only show when successful)
        if (isSuccess) {
            Spacer(modifier = Modifier.height(32.dp))
            Button(
                onClick = onInstallationComplete,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(56.dp),
                shape = RoundedCornerShape(16.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = MaterialTheme.colorScheme.primary
                )
            ) {
                Icon(
                    imageVector = Icons.Default.CheckCircle,
                    contentDescription = null,
                    modifier = Modifier.size(24.dp)
                )
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = context.getString(R.string.continue_button),
                    style = MaterialTheme.typography.labelLarge
                )
            }
        }
    }
}

@Composable
private fun InstallationIcon(
    isSuccess: Boolean,
    hasError: Boolean
) {
    val infiniteTransition = rememberInfiniteTransition(label = "download_animation")

    when {
        isSuccess -> {
            // Success icon - checkmark
            Icon(
                imageVector = Icons.Default.CheckCircle,
                contentDescription = null,
                modifier = Modifier.size(80.dp),
                tint = MaterialTheme.colorScheme.primary
            )
        }
        hasError -> {
            // Error icon
            Icon(
                imageVector = Icons.Default.Error,
                contentDescription = null,
                modifier = Modifier.size(80.dp),
                tint = MaterialTheme.colorScheme.error
            )
        }
        else -> {
            // Download icon with pulsing animation
            val alpha by infiniteTransition.animateFloat(
                initialValue = 0.3f,
                targetValue = 1f,
                animationSpec = infiniteRepeatable(
                    animation = tween(1000, easing = FastOutSlowInEasing),
                    repeatMode = RepeatMode.Reverse
                ),
                label = "pulse_alpha"
            )

            Icon(
                imageVector = Icons.Default.Download,
                contentDescription = null,
                modifier = Modifier
                    .size(80.dp)
                    .alpha(alpha),
                tint = MaterialTheme.colorScheme.primary
            )
        }
    }
}

@Composable
private fun StepText(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.bodyLarge,
        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
        textAlign = TextAlign.Center
    )
}

