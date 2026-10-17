package org.openmware.app.data

import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.util.UUID
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/** What the user picked in the folder chooser, before anything is copied. */
class FolderScan(val uri: Uri, val displayName: String, val archives: List<DocumentFile>) {
    val totalBytes: Long get() = archives.sumOf { it.length() }
    val isGame: Boolean get() = archives.any { it.name.equals("script.npk", ignoreCase = true) }
}

class GameRepository(private val context: Context) {
    private val root = File(context.filesDir, "games").apply { mkdirs() }
    private val index = File(context.filesDir, "library.json")
    private val _games = MutableStateFlow(load())
    val games: StateFlow<List<Game>> = _games

    fun paths(game: Game) = GamePaths(File(root, game.id))

    // --- index ---------------------------------------------------------------------------------
    private fun load(): List<Game> = try {
        val arr = JSONArray(index.readText())
        (0 until arr.length()).map {
            val o = arr.getJSONObject(it)
            Game(o.getString("id"), o.getString("name"), o.optLong("addedAt"), o.optLong("lastPlayedAt"), o.optLong("playMillis"))
        }.filter { File(root, it.id).isDirectory }
    } catch (_: Exception) {
        emptyList()
    }

    private fun persist(list: List<Game>) {
        val arr = JSONArray()
        list.forEach {
            arr.put(JSONObject().put("id", it.id).put("name", it.name).put("addedAt", it.addedAt)
                .put("lastPlayedAt", it.lastPlayedAt).put("playMillis", it.playMillis))
        }
        val tmp = File(index.parentFile, "library.json.tmp")
        tmp.writeText(arr.toString())
        tmp.renameTo(index)
        _games.value = list
    }

    private fun update(id: String, change: (Game) -> Game) = persist(_games.value.map { if (it.id == id) change(it) else it })

    // --- adding --------------------------------------------------------------------------------
    /** Looks into a folder chosen with the system picker (only its top level; the game keeps everything in .npk files). */
    fun scan(tree: Uri): FolderScan? {
        val dir = DocumentFile.fromTreeUri(context, tree) ?: return null
        val archives = dir.listFiles().filter { it.isFile && (it.name ?: "").endsWith(".npk", ignoreCase = true) }
        return FolderScan(tree, dir.name ?: "Game", archives)
    }

    fun freeBytes(): Long = context.filesDir.usableSpace

    /**
     * Copies the archives into a new library entry. `onProgress(done, total)` is called as bytes are copied.
     * Cancelling the calling coroutine removes the partial copy.
     */
    suspend fun add(scan: FolderScan, name: String, keysText: String, onProgress: (Long, Long) -> Unit): Game = withContext(Dispatchers.IO) {
        val id = UUID.randomUUID().toString().substring(0, 8)
        val paths = GamePaths(File(root, id))
        try {
            paths.gameDir.mkdirs()
            paths.dataDir.mkdirs()
            val total = scan.totalBytes
            var done = 0L
            val buffer = ByteArray(1 shl 20)
            for (doc in scan.archives) {
                val target = File(paths.gameDir, doc.name ?: continue)
                context.contentResolver.openInputStream(doc.uri)?.use { input ->
                    target.outputStream().use { output ->
                        while (true) {
                            currentCoroutineContext().ensureActive()
                            val n = input.read(buffer)
                            if (n < 0) break
                            output.write(buffer, 0, n)
                            done += n
                            onProgress(done, total)
                        }
                    }
                } ?: throw IOException("cannot read ${doc.name}")
            }
            val keys = Keys.parse(keysText)
            if (keys.isNotEmpty()) paths.keysFile.writeText(keys.joinToString("\n", postfix = "\n"))
            val game = Game(id, name.ifBlank { scan.displayName }, System.currentTimeMillis())
            persist(_games.value + game)
            game
        } catch (e: Throwable) {
            paths.root.deleteRecursively()
            throw e
        }
    }

    fun readText(uri: Uri): String = context.contentResolver.openInputStream(uri)?.bufferedReader()?.use { it.readText() } ?: ""

    // --- editing -------------------------------------------------------------------------------
    fun rename(game: Game, name: String) = update(game.id) { it.copy(name = name.ifBlank { it.name }) }

    fun setKeys(game: Game, text: String): Int {
        val keys = Keys.parse(text)
        if (keys.isNotEmpty()) paths(game).keysFile.writeText(keys.joinToString("\n", postfix = "\n"))
        persist(_games.value)
        return keys.size
    }

    fun remove(game: Game) {
        paths(game).root.deleteRecursively()
        persist(_games.value.filterNot { it.id == game.id })
    }

    fun clearSaves(game: Game) {
        paths(game).dataDir.deleteRecursively()
        paths(game).dataDir.mkdirs()
        persist(_games.value)
    }

    /** Zips the save folder into the cache and returns it (to be shared through the FileProvider). */
    suspend fun exportSaves(game: Game): File = withContext(Dispatchers.IO) {
        val out = File(context.cacheDir, "exports").apply { mkdirs() }.resolve("${game.name.replace(Regex("[^\\w.-]+"), "_")}-save.zip")
        val base = paths(game).dataDir
        ZipOutputStream(out.outputStream().buffered()).use { zip ->
            base.walkTopDown().filter { it.isFile }.forEach { f ->
                zip.putNextEntry(ZipEntry(f.relativeTo(base).invariantSeparatorsPath))
                f.inputStream().use { it.copyTo(zip) }
                zip.closeEntry()
            }
        }
        out
    }

    fun markPlayStarted(game: Game) = update(game.id) { it.copy(lastPlayedAt = System.currentTimeMillis()) }

    fun addPlayTime(id: String, millis: Long) = update(id) { it.copy(playMillis = it.playMillis + millis) }

    /** Reloads the index from disk (the game activity changes it from its own lifecycle). */
    fun refresh() {
        _games.value = load()
    }
}
