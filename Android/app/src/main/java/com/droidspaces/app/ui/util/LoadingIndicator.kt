package com.droidspaces.app.ui.util

import androidx.compose.foundation.layout.*
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * Standardized loading indicator sizes.
 */
enum class LoadingSize(val size: Dp, val strokeWidth: Dp) {
    Small(16.dp, 2.dp),
    Medium(24.dp, 3.dp),
    Large(48.dp, 4.dp)
}

/**
 * Standardized loading indicator component.
 */
@Composable
fun LoadingIndicator(
    size: LoadingSize = LoadingSize.Medium,
    modifier: Modifier = Modifier,
    color: androidx.compose.ui.graphics.Color? = null
) {
    CircularProgressIndicator(
        modifier = modifier.size(size.size),
        strokeWidth = size.strokeWidth,
        color = color ?: MaterialTheme.colorScheme.primary
    )
}

/**
 * Small loading indicator with custom modifier (for inline use).
 */
@Composable
fun LoadingIndicator(
    modifier: Modifier,
    color: androidx.compose.ui.graphics.Color? = null
) {
    CircularProgressIndicator(
        modifier = modifier,
        strokeWidth = LoadingSize.Small.strokeWidth,
        color = color ?: MaterialTheme.colorScheme.primary
    )
}

/**
 * Full-screen loading indicator with optional message.
 */
@Composable
fun FullScreenLoading(
    message: String? = null,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier.fillMaxSize(),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            LoadingIndicator(size = LoadingSize.Large)
            message?.let {
                Text(
                    text = it,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    textAlign = TextAlign.Center
                )
            }
        }
    }
}

