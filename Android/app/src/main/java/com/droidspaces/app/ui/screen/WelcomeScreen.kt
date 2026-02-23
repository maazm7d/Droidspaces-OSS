package com.droidspaces.app.ui.screen

import androidx.compose.animation.core.animateFloatAsState
import kotlinx.coroutines.delay
import com.droidspaces.app.util.AnimationUtils
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Storage
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun WelcomeScreen(
    onNavigateToRootCheck: () -> Unit
) {
    val context = LocalContext.current
    // Premium staggered fade-in animations with spring physics - elegant and smooth
    // Artificial delays make the animation sequence feel natural and polished
    var iconVisible by remember { mutableStateOf(false) }
    var titleVisible by remember { mutableStateOf(false) }
    var subtitleVisible by remember { mutableStateOf(false) }
    var buttonVisible by remember { mutableStateOf(false) }

    // Start animations with staggered delays for premium feel
    LaunchedEffect(Unit) {
        delay(50) // Initial pause for elegant entry
        iconVisible = true
        delay(100) // Pause between elements
        titleVisible = true
        delay(100) // Longer pause for title emphasis
        subtitleVisible = true
        delay(100) // Smooth transition to button
        buttonVisible = true
    }

    val iconAlpha by animateFloatAsState(
        targetValue = if (iconVisible) 1f else 0f,
        animationSpec = AnimationUtils.fadeInSpec(),
        label = "iconAlpha"
    )
    val titleAlpha by animateFloatAsState(
        targetValue = if (titleVisible) 1f else 0f,
        animationSpec = AnimationUtils.fadeInSpec(),
        label = "titleAlpha"
    )
    val subtitleAlpha by animateFloatAsState(
        targetValue = if (subtitleVisible) 1f else 0f,
        animationSpec = AnimationUtils.fadeInSpec(),
        label = "subtitleAlpha"
    )
    val buttonAlpha by animateFloatAsState(
        targetValue = if (buttonVisible) 1f else 0f,
        animationSpec = AnimationUtils.fadeInSpec(),
        label = "buttonAlpha"
    )

    Scaffold { innerPadding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(24.dp),
            contentAlignment = Alignment.Center
            ) {
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(32.dp),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    // Icon - fades in first
                    Icon(
                        imageVector = Icons.Default.Storage,
                        contentDescription = null,
                        modifier = Modifier
                            .size(80.dp)
                            .alpha(iconAlpha),
                        tint = MaterialTheme.colorScheme.primary
                    )

                    // Title - fades in second
                    Text(
                        text = context.getString(R.string.welcome_title),
                        style = MaterialTheme.typography.headlineLarge,
                        fontWeight = FontWeight.Bold,
                        textAlign = TextAlign.Center,
                        modifier = Modifier.alpha(titleAlpha)
                    )

                    // Subtitle - fades in third
                    Text(
                        text = context.getString(R.string.welcome_subtitle),
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        textAlign = TextAlign.Center,
                        modifier = Modifier.alpha(subtitleAlpha)
                    )

                    Spacer(modifier = Modifier.height(16.dp))

                    // Get Started Button - fades in last
                    Button(
                        onClick = onNavigateToRootCheck,
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(56.dp)
                            .alpha(buttonAlpha),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = MaterialTheme.colorScheme.primary
                        )
                    ) {
                        Text(
                            text = context.getString(R.string.get_started),
                            style = MaterialTheme.typography.labelLarge,
                            fontSize = 16.sp
                        )
                }
            }
        }
    }
}

