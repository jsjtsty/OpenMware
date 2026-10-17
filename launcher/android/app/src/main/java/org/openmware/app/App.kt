package org.openmware.app

import android.app.Application
import org.openmware.app.data.GameRepository

/** Holds the one library instance shared by the launcher and the game activity (same process). */
class App : Application() {
    lateinit var repository: GameRepository
        private set

    override fun onCreate() {
        super.onCreate()
        repository = GameRepository(this)
    }
}
