#pragma once
#include <QString>

// Packs a folder into a zip file (stored, not compressed: save data is small). Returns false when anything could not be written.
bool zipFolder(const QString& folder, const QString& zipPath);
