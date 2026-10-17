/* libmware: the OpenMware engine as a library, for the launchers (SwiftUI on iOS/macOS, Compose on Android).
 * Strings are UTF-8. Call from the main thread. */
#ifndef MWARE_H
#define MWARE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define MW_API __declspec(dllexport)
#else
#define MW_API __attribute__((visibility("default")))
#endif

typedef struct mw_options {
	const char* game_dir;    /* folder with the game's .npk archives */
	const char* keys_file;   /* text file with the NPK keys (one hex key per line) */
	const char* data_dir;    /* where save/ and temp/ live; created if missing; the working directory while playing */
	const char* shaders_dir; /* folder with the precompiled shaders (.metallib / .msl / .spv / .dxil) */
	const char* window_title; /* may be NULL */
} mw_options;

/* Plays the game in its own window and returns when it is closed. 0 on success; otherwise non-zero and, when `error`
 * is not NULL, a message (truncated to `error_size`). */
MW_API int mw_run(const mw_options* options, char* error, size_t error_size);

typedef struct mw_probe_result {
	int ok;           /* 1 when the archives open with the keys and the boot script is present */
	int archives;     /* number of archives found */
	char message[256]; /* why not ok, or an empty string */
} mw_probe_result;

/* Checks a game folder and keys file without starting the game. Returns result->ok. */
MW_API int mw_probe(const char* game_dir, const char* keys_file, mw_probe_result* result);

MW_API const char* mw_version(void);

#ifdef __cplusplus
}
#endif

#endif
