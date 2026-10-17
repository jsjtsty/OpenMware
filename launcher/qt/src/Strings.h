#pragma once
#include <QString>

// Interface text in English and Simplified Chinese; the language follows the system locale. T("key") looks up a string,
// use .arg() for the placeholders.
QString T(const char* key);
bool isChinese();
QString formatBytes(qint64 bytes);
QString formatDuration(double seconds);
