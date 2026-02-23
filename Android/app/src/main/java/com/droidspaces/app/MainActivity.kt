package com.droidspaces.app

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.core.view.WindowCompat
import com.droidspaces.app.ui.navigation.DroidspacesNavigation
import com.droidspaces.app.ui.theme.DroidspacesTheme
import com.droidspaces.app.ui.theme.rememberThemeState

class MainActivity : AppCompatActivity() {
    // Use primitive boolean instead of boxed Boolean - saves memory
    // Initialize to false - UI renders immediately, no waiting
    private var isLoading by mutableStateOf(false)

    override fun onCreate(savedInstanceState: Bundle?) {
        // Install splash screen before super.onCreate for faster display
        val splashScreen = installSplashScreen()
        WindowCompat.setDecorFitsSystemWindows(window, false)
        super.onCreate(savedInstanceState)

        // Set condition immediately - UI will hide splash when ready
        // Start with false to show UI immediately (content is ready)
        splashScreen.setKeepOnScreenCondition { isLoading }

        // Render UI immediately - no blocking operations
        setContent {
            ThemeWrapper {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    DroidspacesNavigation(
                        onContentReady = { isLoading = false }
                    )
                }
            }
        }
    }
}

@Composable
private fun ThemeWrapper(content: @Composable () -> Unit) {
    // Use reactive theme state that updates instantly when preferences change
    val themeState = rememberThemeState()

    DroidspacesTheme(
        darkTheme = themeState.darkTheme,
        dynamicColor = themeState.useDynamicColor,
        amoledMode = themeState.amoledMode
    ) {
        content()
    }
}
