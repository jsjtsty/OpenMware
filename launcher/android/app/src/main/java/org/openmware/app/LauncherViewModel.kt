package org.openmware.app

import android.app.Application
import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import org.openmware.app.data.FolderScan
import org.openmware.app.data.Game
import org.openmware.app.data.Keys

sealed interface Screen {
    data object Library : Screen
    data object Add : Screen
    data object About : Screen
    data class Detail(val id: String) : Screen
}

/** State of the "add a game" flow. */
class AddState {
    var scan by mutableStateOf<FolderScan?>(null)
    var name by mutableStateOf("")
    var keysText by mutableStateOf("")
    var copied by mutableStateOf(0L)
    var total by mutableStateOf(0L)
    var importing by mutableStateOf(false)
    var error by mutableStateOf<String?>(null)
    val keyCount: Int get() = Keys.parse(keysText).size
}

class LauncherViewModel(app: Application) : AndroidViewModel(app) {
    val repository = (app as App).repository
    val games = repository.games

    var screen by mutableStateOf<Screen>(Screen.Library)
        private set
    val add = AddState()
    private var importJob: Job? = null

    fun open(s: Screen) {
        if (s == Screen.Add) resetAdd()
        screen = s
    }

    /** Returns false when already at the top. */
    fun back(): Boolean {
        if (screen == Screen.Library) return false
        if (screen == Screen.Add && add.importing) return true // busy: stay
        screen = if (screen is Screen.Detail || screen == Screen.Add || screen == Screen.About) Screen.Library else screen
        return true
    }

    fun gameById(id: String): Game? = games.value.firstOrNull { it.id == id }

    // --- add flow ------------------------------------------------------------------------------
    private fun resetAdd() {
        add.scan = null
        add.name = ""
        add.keysText = ""
        add.copied = 0
        add.total = 0
        add.importing = false
        add.error = null
    }

    fun folderPicked(uri: Uri) {
        val scan = repository.scan(uri) ?: return
        add.scan = scan
        if (add.name.isBlank()) add.name = scan.displayName
        add.error = null
    }

    fun keysFilePicked(uri: Uri) {
        add.keysText = runCatching { repository.readText(uri) }.getOrDefault("")
    }

    fun startImport() {
        val scan = add.scan ?: return
        add.importing = true
        add.error = null
        add.total = scan.totalBytes
        importJob = viewModelScope.launch {
            try {
                val game = repository.add(scan, add.name, add.keysText) { done, total ->
                    add.copied = done
                    add.total = total
                }
                add.importing = false
                screen = Screen.Detail(game.id)
            } catch (e: CancellationException) {
                add.importing = false
                throw e
            } catch (e: Throwable) {
                add.importing = false
                add.error = e.message ?: e.javaClass.simpleName
            }
        }
    }

    fun cancelImport() {
        importJob?.cancel()
    }

    // --- detail actions ------------------------------------------------------------------------
    fun replaceKeys(game: Game, uri: Uri): Int = repository.setKeys(game, runCatching { repository.readText(uri) }.getOrDefault(""))
}
