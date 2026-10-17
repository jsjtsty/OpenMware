package org.openmware.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.sp
import org.openmware.app.data.Game

/** A generated cover (the engine ships no artwork): a soft gradient from the game's hue, with its first character. */
@Composable
fun Cover(game: Game, modifier: Modifier = Modifier, letterSize: TextUnit = 44.sp) {
    val hue = game.coverSeed.toFloat()
    val a = Color.hsv(hue, 0.45f, 0.85f)
    val b = Color.hsv((hue + 40f) % 360f, 0.55f, 0.55f)
    Box(modifier.background(Brush.linearGradient(listOf(a, b))), contentAlignment = Alignment.Center) {
        Text(
            game.name.trim().take(1).uppercase(),
            color = Color.White.copy(alpha = 0.92f),
            fontSize = letterSize,
            fontWeight = FontWeight.Bold,
            style = MaterialTheme.typography.displayMedium.copy(fontSize = letterSize),
        )
    }
}

@Composable
fun CoverFill(game: Game) = Cover(game, Modifier.fillMaxSize())
