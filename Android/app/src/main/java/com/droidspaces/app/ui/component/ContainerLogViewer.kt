package com.droidspaces.app.ui.component

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import android.content.ClipData
import android.content.ClipboardManager
import android.widget.Toast
import com.droidspaces.app.R

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContainerLogViewer(
    containerName: String,
    logs: List<Pair<Int, String>>,
    onDismiss: () -> Unit,
    onClear: () -> Unit = {},
    isBlocking: Boolean = false // When true, can't dismiss (but close button still visible)
) {
    val context = LocalContext.current
    var currentLogs by remember(logs) { mutableStateOf(logs) }

    // Update logs when prop changes
    LaunchedEffect(logs) {
        currentLogs = logs
    }

    Dialog(
        onDismissRequest = if (isBlocking) { {} } else { onDismiss },
        properties = DialogProperties(
            dismissOnBackPress = !isBlocking,
            dismissOnClickOutside = !isBlocking,
            usePlatformDefaultWidth = false // Allow full width control
        )
    ) {
        Card(
            modifier = Modifier
                .fillMaxWidth() // Maximum width for terminal space
                .fillMaxHeight(0.75f) // 75% of screen height
                .padding(horizontal = 16.dp), // Balanced horizontal padding
            shape = RoundedCornerShape(28.dp), // Increased corner radius for fancy look
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surface
            ),
            border = null, // Remove black border
            elevation = CardDefaults.cardElevation(defaultElevation = 0.dp) // Remove elevation shadow
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(16.dp)
            ) {
                // Top bar: Title and Close button (always visible, title truncates)
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(bottom = 12.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    // Title with max width constraint to prevent pushing buttons off screen
                    Text(
                        text = context.getString(R.string.logs_title, containerName),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                        modifier = Modifier
                            .weight(1f, fill = false)
                            .padding(end = 12.dp),
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )

                    // Close button - Primary dismiss action, always top-right
                    val closeShape = RoundedCornerShape(12.dp)
                    Surface(
                        modifier = Modifier
                            .size(40.dp)
                            .clip(closeShape)
                            .clickable(
                                enabled = !isBlocking,
                                onClick = onDismiss,
                                indication = rememberRipple(bounded = true),
                                interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
                            ),
                        shape = closeShape,
                        color = MaterialTheme.colorScheme.surfaceContainerHighest,
                        tonalElevation = 2.dp
                    ) {
                        Box(
                            modifier = Modifier.fillMaxSize(),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(
                                imageVector = Icons.Default.Close,
                                contentDescription = context.getString(R.string.close),
                                modifier = Modifier.size(20.dp),
                                tint = if (!isBlocking) {
                                    MaterialTheme.colorScheme.onSurfaceVariant
                                } else {
                                    MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f)
                                }
                            )
                        }
                    }
                }

                // Action buttons bar: Clear and Copy (secondary actions)
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(bottom = 12.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    // Clear button
                    val clearShape = RoundedCornerShape(12.dp)
                    Surface(
                        modifier = Modifier
                            .height(40.dp)
                            .weight(1f)
                            .clip(clearShape)
                            .clickable(
                                enabled = currentLogs.isNotEmpty() && !isBlocking,
                                onClick = {
                                    currentLogs = emptyList()
                                    onClear()
                                },
                                indication = rememberRipple(bounded = true),
                                interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
                            ),
                        shape = clearShape,
                        color = MaterialTheme.colorScheme.surfaceContainerHighest,
                        tonalElevation = 1.dp
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxSize()
                                .padding(horizontal = 16.dp),
                            horizontalArrangement = Arrangement.Center,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Icon(
                                imageVector = Icons.Default.Delete,
                                contentDescription = context.getString(R.string.clear_logs),
                                modifier = Modifier.size(18.dp),
                                tint = if (currentLogs.isNotEmpty() && !isBlocking) {
                                    MaterialTheme.colorScheme.onSurfaceVariant
                                } else {
                                    MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f)
                                }
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = context.getString(R.string.clear_logs),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.Medium,
                                color = if (currentLogs.isNotEmpty() && !isBlocking) {
                                    MaterialTheme.colorScheme.onSurfaceVariant
                                } else {
                                    MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f)
                                }
                            )
                        }
                    }

                    // Copy button
                    val copyShape = RoundedCornerShape(12.dp)
                    Surface(
                        modifier = Modifier
                            .height(40.dp)
                            .weight(1f)
                            .clip(copyShape)
                            .clickable(
                                enabled = currentLogs.isNotEmpty(),
                                onClick = {
                                    val logText = currentLogs.joinToString("\n") { it.second }
                                    val clipboard = context.getSystemService(ClipboardManager::class.java)
                                    val clip = ClipData.newPlainText(context.getString(R.string.container_logs), logText)
                                    clipboard.setPrimaryClip(clip)
                                    Toast.makeText(context, R.string.logs_copied, Toast.LENGTH_SHORT).show()
                                },
                                indication = rememberRipple(bounded = true),
                                interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
                            ),
                        shape = copyShape,
                        color = MaterialTheme.colorScheme.surfaceContainerHighest,
                        tonalElevation = 1.dp
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxSize()
                                .padding(horizontal = 16.dp),
                            horizontalArrangement = Arrangement.Center,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Icon(
                                imageVector = Icons.Default.ContentCopy,
                                contentDescription = context.getString(R.string.copy_logs),
                                modifier = Modifier.size(18.dp),
                                tint = if (currentLogs.isNotEmpty()) {
                                    MaterialTheme.colorScheme.primary
                                } else {
                                    MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f)
                                }
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = context.getString(R.string.copy_logs),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.Medium,
                                color = if (currentLogs.isNotEmpty()) {
                                    MaterialTheme.colorScheme.primary
                                } else {
                                    MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f)
                                }
                            )
                        }
                    }
                }

                // Fancy Terminal Console (same as installation screen)
                TerminalConsole(
                    logs = currentLogs,
                    isProcessing = isBlocking, // Show shimmer animation when processing
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f)
                )
            }
        }
    }
}

