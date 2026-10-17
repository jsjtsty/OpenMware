package org.openmware.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import org.openmware.app.ui.AboutScreen
import org.openmware.app.ui.AddGameScreen
import org.openmware.app.ui.DetailScreen
import org.openmware.app.ui.LibraryScreen
import org.openmware.app.ui.OpenMwareTheme

class MainActivity : ComponentActivity() {
    private val vm: LauncherViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        setContent {
            OpenMwareTheme {
                Surface(Modifier, color = MaterialTheme.colorScheme.background) {
                    BackHandler(enabled = vm.screen != Screen.Library) { vm.back() }
                    AnimatedContent(
                        targetState = vm.screen,
                        transitionSpec = { fadeIn() togetherWith fadeOut() },
                        label = "screen",
                    ) { screen ->
                        when (screen) {
                            Screen.Library -> LibraryScreen(vm)
                            Screen.Add -> AddGameScreen(vm)
                            Screen.About -> AboutScreen(vm)
                            is Screen.Detail -> DetailScreen(vm, screen.id)
                        }
                    }
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        vm.repository.refresh() // play time was added by the game activity
    }
}
