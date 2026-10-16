// Raw stream access to the native SQFile behind a CNutFile, used by the VM snapshot (save/load) code.
#pragma once

#include <cstddef>

namespace omw {

struct FileObj;
bool file_obj_write(FileObj* f, const void* p, size_t n);
bool file_obj_read(FileObj* f, void* p, size_t n);
long file_obj_tell(FileObj* f);
bool file_obj_seek(FileObj* f, long pos);
bool file_obj_seek_end(FileObj* f); // writers only

} // namespace omw
