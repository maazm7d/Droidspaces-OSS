package com.droidspaces.app.ui.component

import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.spring
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.util.ContainerInfo

/**
 * Container card for Panel tab - simple card showing container name.
 * Tapping opens ContainerDetailsScreen.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RunningContainerCard(
    container: ContainerInfo,
    onEnter: () -> Unit = {},
    modifier: Modifier = Modifier
) {
    val cardShape = RoundedCornerShape(20.dp)
    val interactionSource = remember { MutableInteractionSource() }

    Card(
        modifier = modifier
            .fillMaxWidth()
            .clip(cardShape)
            .clickable(
                onClick = onEnter,
                indication = rememberRipple(bounded = true),
                interactionSource = interactionSource
            ),
        shape = cardShape,
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .animateContentSize(animationSpec = spring(stiffness = 300f, dampingRatio = 0.8f))
                .padding(horizontal = 13.dp, vertical = 13.dp)
        ) {
            // Container name only
            Text(
                text = container.name,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold
            )
        }
    }
}

