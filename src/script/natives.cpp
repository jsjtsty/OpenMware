// Registers the real (non-stub) native classes.
#include "script/host.h"

namespace omw {

void register_action1d(ScriptHost& host);
void register_app_class(ScriptHost& host);
void register_sprite_class(ScriptHost& host);
void register_file_class(ScriptHost& host);
void register_text_class(ScriptHost& host);
void register_sound_class(ScriptHost& host);

void register_real_classes(ScriptHost& host) {
	register_action1d(host);
	register_app_class(host);
	register_sprite_class(host);
	register_file_class(host);
	register_text_class(host);
	register_sound_class(host);
}

} // namespace omw
