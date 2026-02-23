package com.droidspaces.app.ui.component

import androidx.activity.ComponentActivity
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext

@Composable
fun StatusBarStyle(
    darkMode: Boolean = isSystemInDarkTheme(),
    statusBarScrim: Color = Color.Transparent,
    navigationBarScrim: Color = Color.Transparent,
) {
    val context = LocalContext.current
    val activity = context as? ComponentActivity ?: return

    SideEffect {
        activity.enableEdgeToEdge(
            statusBarStyle = androidx.activity.SystemBarStyle.auto(
                statusBarScrim.toArgb(),
                statusBarScrim.toArgb(),
            ) { darkMode },
            navigationBarStyle = when {
                darkMode -> androidx.activity.SystemBarStyle.dark(
                    navigationBarScrim.toArgb(),
                )
                else -> androidx.activity.SystemBarStyle.light(
                    navigationBarScrim.toArgb(),
                    navigationBarScrim.toArgb(),
                )
            },
        )
    }
}

