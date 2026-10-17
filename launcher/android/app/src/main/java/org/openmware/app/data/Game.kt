package org.openmware.app.data

import java.io.File

/** One game of the library. Its files live in filesDir/games/<id>/ : game/ (the .npk archives), keys.txt, data/ (saves). */
data class Game(
    val id: String,
    val name: String,
    val addedAt: Long,
    val lastPlayedAt: Long = 0L,
    val playMillis: Long = 0L,
) {
    /** Hue for the generated cover, stable per game. */
    val coverSeed: Int get() = (id.hashCode() and 0x7fffffff) % 360
}

enum class GameStatus { Ready, KeysMissing, Incomplete }

/** Paths and checks for a game; the repository decides where the root is. */
class GamePaths(val root: File) {
    val gameDir = File(root, "game")
    val keysFile = File(root, "keys.txt")
    val dataDir = File(root, "data")
    val saveDir get() = File(dataDir, "save")

    fun archives(): List<File> = gameDir.listFiles { f -> f.isFile && f.name.endsWith(".npk", ignoreCase = true) }?.sortedBy { it.name } ?: emptyList()

    fun gameBytes(): Long = archives().sumOf { it.length() }

    fun saveBytes(): Long = dirSize(dataDir)

    fun status(): GameStatus = when {
        archives().none { it.name.equals("script.npk", ignoreCase = true) } -> GameStatus.Incomplete
        Keys.count(keysFile) == 0 -> GameStatus.KeysMissing
        else -> GameStatus.Ready
    }

    private fun dirSize(dir: File): Long = dir.walkTopDown().filter { it.isFile }.sumOf { it.length() }
}

/** The NPK keys file: one 256-bit key (64 hex digits) per line. */
object Keys {
    private val line = Regex("^[0-9a-fA-F]{64}$")

    fun parse(text: String): List<String> = text.lineSequence().map { it.trim() }.filter { line.matches(it) }.map { it.lowercase() }.toList()

    fun count(file: File): Int = if (file.isFile) parse(file.readText()).size else 0
}
