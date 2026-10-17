#include "Strings.h"

#include <QHash>
#include <QLocale>

namespace {

struct Entry {
	const char* key;
	const char* en;
	const char* zh;
};

const Entry kStrings[] = {
	{"app_name", "OpenMware", "OpenMware"},
	{"library_title", "Library", "游戏库"},
	{"add_game", "Add Game", "添加游戏"},
	{"play", "Play", "开始游戏"},
	{"stop", "Stop", "结束游戏"},
	{"about", "About", "关于"},
	{"playing", "Playing", "游戏中"},
	{"last_played", "Last played %1", "上次游玩 %1"},
	{"never_played", "Never played", "尚未游玩"},
	{"status_ready", "Ready", "可以游玩"},
	{"status_keys_missing", "Keys needed", "需要密钥"},
	{"status_incomplete", "Incomplete", "文件不完整"},
	{"empty_title", "No games yet", "还没有游戏"},
	{"empty_body", "OpenMware is an engine and does not include any game. Add the folder of your own copy of a supported game.",
	 "OpenMware 只是一个引擎，不含任何游戏。请添加你自己持有的受支持游戏的文件夹。"},
	{"select_hint", "Select a game on the left.", "在左边选择一个游戏。"},

	{"add_title", "Add a Game", "添加游戏"},
	{"add_folder", "Game folder", "游戏文件夹"},
	{"add_folder_hint", "Pick the folder that contains script.npk, cg.npk and the other .npk files.",
	 "选择包含 script.npk、cg.npk 等 .npk 文件的文件夹。"},
	{"add_browse", "Browse…", "浏览…"},
	{"add_found", "%1 archives, %2", "%1 个档案，%2"},
	{"add_not_a_game", "No game archives (script.npk) were found in that folder.", "该文件夹里没有找到游戏档案（script.npk）。"},
	{"add_copy", "Copy into OpenMware", "复制到 OpenMware"},
	{"add_copy_on", "The archives are copied into the library folder, so the original can be moved or deleted. Needs the same amount of free space.",
	 "档案会被复制到游戏库目录，原文件夹之后可以移动或删除。需要同样大小的可用空间。"},
	{"add_copy_off", "The game stays where it is and is opened from there each time. Nothing is duplicated.",
	 "游戏留在原处，每次从那里读取，不会占用额外空间。"},
	{"add_no_space", "Not enough free space: %1 needed.", "可用空间不足：需要 %1。"},
	{"add_name", "Name", "名称"},
	{"add_keys", "Decryption keys", "解密密钥"},
	{"add_keys_import", "Import Keys File…", "导入密钥文件…"},
	{"add_keys_paste", "Or paste the keys (one 64-digit hex key per line)", "或直接粘贴密钥（每行一个 64 位十六进制密钥）"},
	{"add_keys_ok", "%1 key(s) found", "找到 %1 个密钥"},
	{"add_keys_invalid", "No valid key found", "没有找到有效的密钥"},
	{"add_failed", "Could not add the game: %1", "无法添加游戏：%1"},
	{"add_import", "Add", "添加"},
	{"add_copying", "Copying game files…", "正在复制游戏文件…"},
	{"cancel", "Cancel", "取消"},
	{"choose_folder", "Choose the game folder", "选择游戏文件夹"},
	{"choose_keys", "Choose the keys file", "选择密钥文件"},
	{"text_files", "Text files (*.txt);;All files (*)", "文本文件 (*.txt);;所有文件 (*)"},

	{"cannot_play_keys", "Import the keys first.", "请先导入密钥。"},
	{"cannot_play_incomplete", "The game files are incomplete or the folder is unavailable.", "游戏文件不完整，或文件夹已不可用。"},
	{"detail_size", "Size", "大小"},
	{"detail_archives", "Archives", "档案"},
	{"detail_storage", "Files", "文件位置"},
	{"storage_copied", "Copied into the library", "已复制到游戏库"},
	{"storage_linked", "Linked: %1", "链接到：%1"},
	{"detail_play_time", "Play time", "游玩时长"},
	{"detail_save_size", "Save data", "存档"},
	{"detail_no_saves", "None", "无"},
	{"detail_keys", "Keys", "密钥"},
	{"detail_keys_count", "%1 key(s)", "%1 个"},
	{"detail_verify", "Check Files and Keys", "检查游戏文件与密钥"},
	{"detail_keys_replace", "Replace Keys…", "更换密钥…"},
	{"detail_export_saves", "Export Save Data…", "导出存档…"},
	{"detail_reveal_game", "Show Game Folder", "打开游戏文件夹"},
	{"detail_reveal_saves", "Show Save Folder", "打开存档文件夹"},
	{"detail_log", "Show Last Run Log", "查看上次运行日志"},
	{"detail_clear_saves", "Delete Save Data…", "删除存档…"},
	{"detail_remove", "Remove Game…", "移除游戏…"},
	{"detail_rename", "Rename", "重命名"},
	{"confirm_clear_saves", "Delete all save data of “%1”? This cannot be undone.", "删除“%1”的全部存档？此操作无法撤销。"},
	{"confirm_remove", "Remove “%1” from the library? Copied files and save data are deleted; a linked folder is left untouched.",
	 "从游戏库移除“%1”？复制进来的文件和存档会被删除；链接的原文件夹不受影响。"},
	{"verify_ok", "All good: %1 archives opened with the keys.", "一切正常：已用密钥成功打开 %1 个档案。"},
	{"verify_failed", "Check failed: %1", "检查失败：%1"},
	{"keys_replaced", "%1 key(s) saved.", "已保存 %1 个密钥。"},
	{"save_zip", "Save the save data as", "保存存档压缩包"},
	{"saves_exported", "Save data exported.", "存档已导出。"},
	{"saves_export_failed", "Could not write the archive.", "无法写入压缩包。"},
	{"error_title", "Something went wrong", "出错了"},
	{"error_player_missing", "The player (%1) was not found next to the launcher.", "启动器旁边找不到播放器（%1）。"},
	{"error_player_start", "The player could not be started: %1", "无法启动播放器：%1"},
	{"error_player_exit", "The game stopped unexpectedly (code %1).", "游戏意外退出（代码 %1）。"},
	{"error_folder_unavailable", "The game folder is not available any more. Remove the game and add it again.",
	 "游戏文件夹已不可用。请移除该游戏后重新添加。"},
	{"minutes_short", "%1 min", "%1 分钟"},
	{"hours_short", "%1 h %2 min", "%1 小时 %2 分钟"},
	{"quit_while_playing", "A game is running. Quit the launcher and close the game?", "游戏正在运行。退出启动器并关闭游戏吗？"},

	{"about_title", "About OpenMware", "关于 OpenMware"},
	{"about_version", "Launcher and engine version %1", "启动器与引擎版本 %1"},
	{"about_body", "OpenMware is an open-source engine that runs the original data of supported visual novels. It contains no game data. Use it only with a copy you own.",
	 "OpenMware 是一个开源引擎，用来运行受支持视觉小说的原始数据。它不含任何游戏数据，请仅配合你自己持有的正版使用。"},
	{"about_licenses", "Open-source licences", "开源许可"},
	{"about_licenses_body", "SDL3 (zlib), zstd (BSD), libwebp (BSD), stb (public domain), Squirrel (MIT) and the Qt toolkit.",
	 "SDL3 (zlib)、zstd (BSD)、libwebp (BSD)、stb（公有领域）、Squirrel (MIT) 以及 Qt 工具包。"},
	{"about_ffmpeg", "FFmpeg (LGPL 2.1 or later) and Qt (LGPL 3) are included as separate shared libraries next to the program and can be replaced with your own builds. Their licence texts are in the licenses folder.",
	 "FFmpeg (LGPL 2.1 及以上) 和 Qt (LGPL 3) 以独立动态库的形式放在程序旁边，可以换成你自己编译的版本。许可文本在 licenses 文件夹里。"},
	{"open_licenses", "Open Licenses Folder", "打开许可文件夹"},
	{"close", "Close", "关闭"},
};

} // namespace

bool isChinese() {
	static const bool zh = QLocale::system().language() == QLocale::Chinese;
	return zh;
}

QString T(const char* key) {
	static const QHash<QByteArray, const Entry*> table = [] {
		QHash<QByteArray, const Entry*> t;
		for (const Entry& e : kStrings) t.insert(e.key, &e);
		return t;
	}();
	const auto it = table.constFind(key);
	if (it == table.constEnd()) return QString::fromUtf8(key);
	return QString::fromUtf8(isChinese() ? (*it)->zh : (*it)->en);
}

QString formatBytes(qint64 bytes) {
	const char* units[] = {"B", "KB", "MB", "GB", "TB"};
	double v = static_cast<double>(bytes);
	int u = 0;
	while (v >= 1024.0 && u < 4) { v /= 1024.0; u++; }
	return u == 0 ? QString::number(bytes) + QStringLiteral(" B")
	              : QString::number(v, 'f', v >= 100 ? 0 : 1) + QStringLiteral(" ") + QString::fromLatin1(units[u]);
}

QString formatDuration(double seconds) {
	const int minutes = static_cast<int>(seconds / 60.0);
	if (minutes < 60) return T("minutes_short").arg(minutes);
	return T("hours_short").arg(minutes / 60).arg(minutes % 60);
}
