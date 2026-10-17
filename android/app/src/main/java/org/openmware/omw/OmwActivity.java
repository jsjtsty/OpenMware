package org.openmware.omw;

import org.libsdl.app.SDLActivity;

/** Hosts the engine: SDL loads libSDL3.so, then libmain.so, whose SDL_main is apps/openmware.cpp's main. */
public class OmwActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }
}
