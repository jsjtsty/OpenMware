package org.openmware.app

import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.view.WindowManager
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import org.libsdl.app.SDLActivity
import org.openmware.app.data.Game

/**
 * Hosts the engine. SDL loads libSDL3.so and libmain.so (apps/openmware.cpp's main); the game to play travels in the
 * intent and becomes the command line: GAME_DIR --keys FILE --data-dir DIR.
 */
class GameActivity : SDLActivity() {
    private var gameId: String? = null
    private var startedAt = 0L

    override fun getLibraries(): Array<String> = arrayOf("SDL3", "main")

    override fun getArguments(): Array<String> {
        val i = intent
        gameId = i.getStringExtra(EXTRA_ID)
        startedAt = SystemClock.elapsedRealtime()
        return arrayOf(
            i.getStringExtra(EXTRA_GAME_DIR) ?: "",
            "--keys", i.getStringExtra(EXTRA_KEYS) ?: "",
            "--data-dir", i.getStringExtra(EXTRA_DATA_DIR) ?: "",
        )
    }

    // SDL only hides the system bars for a fullscreen window; the game window is a normal one, so do it here (a swipe from the
    // edge shows them for a moment).
    private fun hideSystemBars() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P)
            window.attributes = window.attributes.apply { layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES }
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsetsCompat.Type.systemBars())
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        hideSystemBars()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }

    override fun onDestroy() {
        super.onDestroy()
        gameId?.let { (application as App).repository.addPlayTime(it, SystemClock.elapsedRealtime() - startedAt) }
        gameId = null
    }

    companion object {
        private const val EXTRA_ID = "id"
        private const val EXTRA_GAME_DIR = "game_dir"
        private const val EXTRA_KEYS = "keys"
        private const val EXTRA_DATA_DIR = "data_dir"

        fun start(context: Context, game: Game, repo: org.openmware.app.data.GameRepository) {
            val paths = repo.paths(game)
            repo.markPlayStarted(game)
            context.startActivity(
                Intent(context, GameActivity::class.java)
                    .putExtra(EXTRA_ID, game.id)
                    .putExtra(EXTRA_GAME_DIR, paths.gameDir.absolutePath)
                    .putExtra(EXTRA_KEYS, paths.keysFile.absolutePath)
                    .putExtra(EXTRA_DATA_DIR, paths.dataDir.absolutePath)
            )
        }
    }
}
