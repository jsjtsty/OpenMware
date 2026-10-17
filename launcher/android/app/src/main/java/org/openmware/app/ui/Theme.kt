package org.openmware.app.ui

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

private val Light = lightColorScheme(
    primary = Color(0xFF5B4B8A), onPrimary = Color.White, primaryContainer = Color(0xFFE5DEFF), onPrimaryContainer = Color(0xFF17005A),
    secondary = Color(0xFF8C4A6A), secondaryContainer = Color(0xFFFFD8E8), onSecondaryContainer = Color(0xFF38071F),
    background = Color(0xFFFBF7FF), surface = Color(0xFFFBF7FF),
)
private val Dark = darkColorScheme(
    primary = Color(0xFFC9BEFF), onPrimary = Color(0xFF2E1E5B), primaryContainer = Color(0xFF443472), onPrimaryContainer = Color(0xFFE5DEFF),
    secondary = Color(0xFFFFB0D0), secondaryContainer = Color(0xFF6F334F), onSecondaryContainer = Color(0xFFFFD8E8),
    background = Color(0xFF141318), surface = Color(0xFF141318),
)

@Composable
fun OpenMwareTheme(content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme()
    val scheme = when {
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ->
            if (dark) dynamicDarkColorScheme(LocalContext.current) else dynamicLightColorScheme(LocalContext.current)
        dark -> Dark
        else -> Light
    }
    MaterialTheme(colorScheme = scheme, content = content)
}
