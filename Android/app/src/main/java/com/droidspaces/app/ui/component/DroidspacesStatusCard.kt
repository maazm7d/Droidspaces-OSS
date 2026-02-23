package com.droidspaces.app.ui.component

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.util.SystemInfoManager
import com.droidspaces.app.R

enum class DroidspacesStatus {
    Working,
    UpdateAvailable,
    NotInstalled,
    Unsupported,
    Corrupted,
    ModuleMissing
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun DroidspacesStatusCard(
    status: DroidspacesStatus,
    version: String? = null,
    isChecking: Boolean = false,
    isRootAvailable: Boolean = true,
    onClick: () -> Unit = {}
) {
    val context = LocalContext.current

    // Load cached values immediately (synchronous, instant display)
    var rootProviderVersion by remember {
        mutableStateOf<String?>(
            if (status == DroidspacesStatus.Working || status == DroidspacesStatus.UpdateAvailable) {
                SystemInfoManager.getCachedRootProviderVersion(context) ?: context.getString(R.string.unknown)
            } else null
        )
    }

    var droidspacesVersion by remember {
        mutableStateOf<String?>(
            if (status == DroidspacesStatus.Working || status == DroidspacesStatus.UpdateAvailable) {
                SystemInfoManager.getCachedDroidspacesVersion(context) ?: version
            } else version
        )
    }

    // Check actual values in background and update with animation if changed
    // Use refresh method to bypass cache after backend installation/update
    LaunchedEffect(status) {
        if (status == DroidspacesStatus.Working || status == DroidspacesStatus.UpdateAvailable) {
            val actualRootVersion = SystemInfoManager.getRootProviderVersion(context)
            // Only update if it's different (triggers animation)
            if (actualRootVersion != rootProviderVersion) {
                rootProviderVersion = actualRootVersion
            }

            // Force refresh droidspaces version to get latest after backend updates
            val actualDroidspacesVersion = SystemInfoManager.refreshDroidspacesVersion(context)
            // Only update if it's different (triggers animation)
            if (actualDroidspacesVersion != null && actualDroidspacesVersion != droidspacesVersion) {
                droidspacesVersion = actualDroidspacesVersion
            }
        }
    }

    // Avoid Pair allocation - compute directly
    // Match KernelSU's error styling: red error container for all error states
    val containerColor = when {
        !isRootAvailable -> MaterialTheme.colorScheme.errorContainer
        status == DroidspacesStatus.Working -> MaterialTheme.colorScheme.secondaryContainer
        status == DroidspacesStatus.UpdateAvailable -> MaterialTheme.colorScheme.errorContainer
        status == DroidspacesStatus.NotInstalled -> MaterialTheme.colorScheme.errorContainer // Red like KernelSU
        status == DroidspacesStatus.Unsupported -> MaterialTheme.colorScheme.errorContainer
        status == DroidspacesStatus.Corrupted -> MaterialTheme.colorScheme.errorContainer
        status == DroidspacesStatus.ModuleMissing -> MaterialTheme.colorScheme.errorContainer
        else -> MaterialTheme.colorScheme.secondaryContainer
    }

    val icon = when {
        !isRootAvailable -> Icons.Default.Error
        status == DroidspacesStatus.Working -> Icons.Default.Verified
        status == DroidspacesStatus.UpdateAvailable -> Icons.Default.Update
        status == DroidspacesStatus.NotInstalled -> Icons.Default.Warning
        status == DroidspacesStatus.Unsupported -> Icons.Default.Warning
        status == DroidspacesStatus.Corrupted -> Icons.Default.Cancel
        status == DroidspacesStatus.ModuleMissing -> Icons.Default.Warning
        else -> Icons.Default.CheckCircle
    }

    val cardShape = RoundedCornerShape(20.dp)
    val interactionSource = remember { MutableInteractionSource() }

    ElevatedCard(
        colors = CardDefaults.elevatedCardColors(
            containerColor = containerColor
        ),
        shape = cardShape,
        elevation = CardDefaults.elevatedCardElevation(defaultElevation = 1.dp),
        modifier = Modifier
            .fillMaxWidth()
            .clip(cardShape)
            .then(
                // Make clickable if onClick is provided OR if status requires re-installation/update
                if (onClick != {} || status == DroidspacesStatus.NotInstalled ||
                    status == DroidspacesStatus.Corrupted || status == DroidspacesStatus.UpdateAvailable ||
                    status == DroidspacesStatus.ModuleMissing) {
                    Modifier.combinedClickable(
                        interactionSource = interactionSource,
                        indication = rememberRipple(bounded = true),
                        onClick = onClick
                    )
                } else Modifier
            )
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(20.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                modifier = Modifier.size(38.dp),
                imageVector = icon,
                contentDescription = null,
                tint = when {
                    !isRootAvailable -> MaterialTheme.colorScheme.onErrorContainer
                    status == DroidspacesStatus.Working -> MaterialTheme.colorScheme.primary
                    status == DroidspacesStatus.UpdateAvailable -> MaterialTheme.colorScheme.onErrorContainer
                    status == DroidspacesStatus.NotInstalled -> MaterialTheme.colorScheme.onErrorContainer // Red icon like KernelSU
                    status == DroidspacesStatus.ModuleMissing -> MaterialTheme.colorScheme.onErrorContainer
                    else -> MaterialTheme.colorScheme.onErrorContainer
                }
            )
            Spacer(modifier = Modifier.width(16.dp))
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                // Status text based on actual status
                Text(
                    text = when {
                        !isRootAvailable -> context.getString(R.string.root_unavailable)
                        isChecking -> context.getString(R.string.droidspaces_checking)
                        status == DroidspacesStatus.Working -> context.getString(R.string.droidspaces_working)
                        status == DroidspacesStatus.UpdateAvailable -> context.getString(R.string.droidspaces_update_available)
                        status == DroidspacesStatus.NotInstalled -> context.getString(R.string.droidspaces_not_installed)
                        status == DroidspacesStatus.Corrupted -> context.getString(R.string.droidspaces_corrupted)
                        status == DroidspacesStatus.Unsupported -> context.getString(R.string.droidspaces_unsupported)
                        status == DroidspacesStatus.ModuleMissing -> context.getString(R.string.droidspaces_module_missing)
                        else -> context.getString(R.string.droidspaces_unknown)
                    },
                    style = MaterialTheme.typography.titleMedium,
                    color = when {
                        !isRootAvailable -> MaterialTheme.colorScheme.onErrorContainer
                        status == DroidspacesStatus.Working -> MaterialTheme.colorScheme.onSecondaryContainer
                        status == DroidspacesStatus.UpdateAvailable -> MaterialTheme.colorScheme.onErrorContainer
                        status == DroidspacesStatus.NotInstalled -> MaterialTheme.colorScheme.onErrorContainer // Red text like KernelSU
                        status == DroidspacesStatus.Corrupted -> MaterialTheme.colorScheme.onErrorContainer
                        status == DroidspacesStatus.Unsupported -> MaterialTheme.colorScheme.onErrorContainer
                        status == DroidspacesStatus.ModuleMissing -> MaterialTheme.colorScheme.onErrorContainer
                        else -> MaterialTheme.colorScheme.onSecondaryContainer
                    },
                    fontWeight = FontWeight.SemiBold
                )
                // Check root availability first - if root is unavailable, always show the grant message
                if (!isRootAvailable) {
                    Text(
                        text = context.getString(R.string.grant_root_message),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.8f)
                    )
                } else if (status == DroidspacesStatus.Working || status == DroidspacesStatus.UpdateAvailable) {
                    Text(
                        text = context.getString(R.string.version_label, droidspacesVersion ?: context.getString(R.string.unknown)),
                        style = MaterialTheme.typography.bodyMedium,
                        color = when (status) {
                            DroidspacesStatus.UpdateAvailable -> MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.7f)
                            else -> MaterialTheme.colorScheme.onSecondaryContainer.copy(alpha = 0.7f)
                        }
                    )
                    // Root provider version - instant display, no animations
                    Text(
                        text = context.getString(R.string.root_provider_label, rootProviderVersion ?: context.getString(R.string.unknown)),
                        style = MaterialTheme.typography.bodyMedium,
                        color = when (status) {
                            DroidspacesStatus.UpdateAvailable -> MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.7f)
                            else -> MaterialTheme.colorScheme.onSecondaryContainer.copy(alpha = 0.7f)
                        }
                    )
                } else {
                    Text(
                        text = when {
                            status == DroidspacesStatus.UpdateAvailable -> context.getString(R.string.update_available_message)
                            status == DroidspacesStatus.NotInstalled -> context.getString(R.string.tap_to_install)
                            status == DroidspacesStatus.Unsupported -> context.getString(R.string.device_not_supported)
                            status == DroidspacesStatus.Corrupted -> context.getString(R.string.tap_to_reinstall)
                            status == DroidspacesStatus.ModuleMissing -> context.getString(R.string.tap_to_install_module)
                            else -> ""
                        },
                        style = MaterialTheme.typography.bodyMedium,
                        color = when {
                            status == DroidspacesStatus.UpdateAvailable -> MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.8f)
                            status == DroidspacesStatus.NotInstalled -> MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.8f) // Red text like KernelSU
                            status == DroidspacesStatus.Corrupted -> MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.8f)
                            status == DroidspacesStatus.Unsupported -> MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.8f)
                            status == DroidspacesStatus.ModuleMissing -> MaterialTheme.colorScheme.onErrorContainer.copy(alpha = 0.8f)
                            else -> MaterialTheme.colorScheme.onSecondaryContainer.copy(alpha = 0.7f)
                        }
                    )
                }
            }
        }
    }
}

