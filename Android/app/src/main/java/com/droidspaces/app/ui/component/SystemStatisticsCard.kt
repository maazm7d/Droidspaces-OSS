package com.droidspaces.app.ui.component

import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.spring
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R

/**
 * System statistics card showing Android system usage (CPU, RAM, Uptime, Temperature).
 * Always visible in Panel tab.
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun SystemStatisticsCard(
    cpuPercent: Double = 0.0,
    ramPercent: Double = 0.0,
    uptime: String = "0s",
    temperature: String = "N/A",
    onClick: () -> Unit = {},
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val cardShape = RoundedCornerShape(20.dp)
    val interactionSource = remember { MutableInteractionSource() }

    Card(
        modifier = modifier
            .fillMaxWidth()
            .clip(cardShape)
            .combinedClickable(
                interactionSource = interactionSource,
                indication = rememberRipple(bounded = true),
                onClick = onClick
            ),
        shape = cardShape,
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .animateContentSize(animationSpec = spring(stiffness = 300f, dampingRatio = 0.8f))
                .padding(vertical = 20.dp, horizontal = 16.dp)
                .height(133.dp) // 5% smaller than 140.dp
        ) {
            // Title at top - center aligned, 1% margin from top
            Text(
                text = context.getString(R.string.system_statistics),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.95f),
                modifier = Modifier
                    .align(Alignment.TopCenter)
                    .padding(top = 1.dp) // 1% margin from top
            )

            // Usage stats row - circles/text center-aligned, labels at 1% from bottom
            Row(
                modifier = Modifier.fillMaxSize(),
                horizontalArrangement = Arrangement.SpaceEvenly,
                verticalAlignment = Alignment.CenterVertically
            ) {
                // CPU circle with label
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxHeight(),
                    contentAlignment = Alignment.Center
                ) {
                    // Center-aligned circle
                    PercentCircle(percent = cpuPercent)

                    // Label at 1% from bottom
                    Text(
                        text = context.getString(R.string.cpu),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                        modifier = Modifier.align(Alignment.BottomCenter)
                            .padding(bottom = 1.dp) // 1% of 140.dp ≈ 1.dp
                    )
                }

                // RAM circle with label
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxHeight(),
                    contentAlignment = Alignment.Center
                ) {
                    // Center-aligned circle
                    PercentCircle(percent = ramPercent)

                    // Label at 1% from bottom
                    Text(
                        text = context.getString(R.string.ram),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                        modifier = Modifier.align(Alignment.BottomCenter)
                            .padding(bottom = 1.dp) // 1% of 140.dp ≈ 1.dp
                    )
                }

                // Uptime with label
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxHeight(),
                    contentAlignment = Alignment.Center
                ) {
                    // Center-aligned text value
                    Text(
                        text = uptime,
                        style = MaterialTheme.typography.bodyMedium,
                        fontWeight = FontWeight.Medium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.9f)
                    )

                    // Label at 1% from bottom
                    Text(
                        text = context.getString(R.string.uptime),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                        modifier = Modifier.align(Alignment.BottomCenter)
                            .padding(bottom = 1.dp) // 1% of 140.dp ≈ 1.dp
                    )
                }

                // Temperature with label
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxHeight(),
                    contentAlignment = Alignment.Center
                ) {
                    // Center-aligned text value
                    Text(
                        text = temperature,
                        style = MaterialTheme.typography.bodyMedium,
                        fontWeight = FontWeight.Medium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.9f)
                    )

                    // Label at 1% from bottom
                    Text(
                        text = context.getString(R.string.temp),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                        modifier = Modifier.align(Alignment.BottomCenter)
                            .padding(bottom = 1.dp) // 1% of 140.dp ≈ 1.dp
                    )
                }
            }
        }
    }
}

