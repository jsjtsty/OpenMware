package org.openmware.app.ui

import android.content.Intent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.DeleteForever
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Key
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.FileProvider
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlinx.coroutines.launch
import org.openmware.app.GameActivity
import org.openmware.app.LauncherViewModel
import org.openmware.app.R
import org.openmware.app.data.Game
import org.openmware.app.data.GameStatus
import org.openmware.app.data.Keys

private enum class Confirm { None, Rename, ClearSaves, Remove }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DetailScreen(vm: LauncherViewModel, id: String) {
    val games by vm.games.collectAsStateWithLifecycle()
    val game = games.firstOrNull { it.id == id }
    if (game == null) { // removed
        LaunchedEffect(Unit) { vm.back() }
        return
    }
    val context = LocalContext.current
    val repo = vm.repository
    val scope = rememberCoroutineScope()
    var dialog by remember { mutableStateOf(Confirm.None) }
    var refresh by remember { mutableStateOf(0) } // bumps after actions that change files without changing the index
    val paths = remember(game, refresh, games) { repo.paths(game) }
    val status = remember(game, refresh, games) { paths.status() }
    val gameBytes = remember(game, refresh) { paths.gameBytes() }
    val saveBytes = remember(game, refresh) { paths.saveBytes() }
    val keyCount = remember(game, refresh) { Keys.count(paths.keysFile) }
    val pickKeys = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            vm.replaceKeys(game, uri)
            refresh++
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(game.name, maxLines = 1) },
                navigationIcon = { IconButton(onClick = { vm.back() }) { Icon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back)) } },
                actions = { IconButton(onClick = { dialog = Confirm.Rename }) { Icon(Icons.Filled.Edit, stringResource(R.string.detail_rename)) } },
                colors = TopAppBarDefaults.topAppBarColors(),
            )
        },
    ) { padding ->
        Column(Modifier.fillMaxSize().padding(padding).verticalScroll(rememberScrollState())) {
            Box(Modifier.fillMaxWidth().padding(horizontal = 16.dp).height(200.dp)) {
                Cover(game, Modifier.fillMaxSize().clip(MaterialTheme.shapes.extraLarge), letterSize = 72.sp)
            }
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Button(
                    onClick = { GameActivity.start(context, game, repo) },
                    enabled = status == GameStatus.Ready,
                    modifier = Modifier.fillMaxWidth().height(56.dp),
                ) {
                    Icon(Icons.Filled.PlayArrow, null)
                    Spacer(Modifier.size(8.dp))
                    Text(stringResource(R.string.play), style = MaterialTheme.typography.titleMedium)
                }
                when (status) {
                    GameStatus.KeysMissing -> StatusLine(false, stringResource(R.string.cannot_play_keys))
                    GameStatus.Incomplete -> StatusLine(false, stringResource(R.string.cannot_play_incomplete))
                    GameStatus.Ready -> {}
                }
            }
            HorizontalDivider()
            Info(R.string.detail_size, formatBytes(gameBytes))
            Info(R.string.detail_archives, paths.archives().size.toString())
            Info(R.string.detail_play_time, if (game.playMillis > 0) formatDuration(game.playMillis) else stringResource(R.string.never_played))
            Info(R.string.detail_save_size, if (saveBytes > 0) formatBytes(saveBytes) else stringResource(R.string.detail_no_saves))
            Info(R.string.detail_keys, if (keyCount > 0) stringResource(R.string.add_keys_ok, keyCount) else stringResource(R.string.add_keys_invalid))
            HorizontalDivider()
            Action(Icons.Filled.Key, R.string.detail_keys_replace) { pickKeys.launch(arrayOf("*/*")) }
            Action(Icons.Filled.Share, R.string.detail_export_saves, enabled = saveBytes > 0) {
                scope.launch {
                    val zip = repo.exportSaves(game)
                    val uri = FileProvider.getUriForFile(context, "${context.packageName}.files", zip)
                    val send = Intent(Intent.ACTION_SEND).setType("application/zip").putExtra(Intent.EXTRA_STREAM, uri)
                        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    context.startActivity(Intent.createChooser(send, null))
                }
            }
            Action(Icons.Filled.DeleteForever, R.string.detail_clear_saves, enabled = saveBytes > 0, danger = true) { dialog = Confirm.ClearSaves }
            Action(Icons.Filled.DeleteForever, R.string.detail_remove, danger = true) { dialog = Confirm.Remove }
            Spacer(Modifier.height(32.dp))
        }
    }

    when (dialog) {
        Confirm.None -> {}
        Confirm.Rename -> {
            var text by remember { mutableStateOf(game.name) }
            AlertDialog(
                onDismissRequest = { dialog = Confirm.None },
                title = { Text(stringResource(R.string.detail_rename)) },
                text = { OutlinedTextField(text, { text = it }, singleLine = true, modifier = Modifier.fillMaxWidth()) },
                confirmButton = { TextButton(onClick = { repo.rename(game, text); dialog = Confirm.None }) { Text(stringResource(R.string.save)) } },
                dismissButton = { TextButton(onClick = { dialog = Confirm.None }) { Text(stringResource(R.string.cancel)) } },
            )
        }
        Confirm.ClearSaves -> ConfirmDialog(stringResource(R.string.confirm_clear_saves, game.name), onNo = { dialog = Confirm.None }) {
            repo.clearSaves(game); refresh++; dialog = Confirm.None
        }
        Confirm.Remove -> ConfirmDialog(stringResource(R.string.confirm_remove, game.name), onNo = { dialog = Confirm.None }) {
            dialog = Confirm.None; repo.remove(game); vm.back()
        }
    }
}

@Composable
private fun Info(label: Int, value: String) {
    ListItem(headlineContent = { Text(stringResource(label)) }, trailingContent = { Text(value, color = MaterialTheme.colorScheme.onSurfaceVariant) })
}

@Composable
private fun Action(icon: androidx.compose.ui.graphics.vector.ImageVector, label: Int, enabled: Boolean = true, danger: Boolean = false, onClick: () -> Unit) {
    TextButton(
        onClick = onClick,
        enabled = enabled,
        modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp),
        colors = if (danger) ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error) else ButtonDefaults.textButtonColors(),
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Icon(icon, null)
            Spacer(Modifier.size(12.dp))
            Text(stringResource(label), style = MaterialTheme.typography.bodyLarge)
        }
    }
}

@Composable
private fun ConfirmDialog(text: String, onNo: () -> Unit, onYes: () -> Unit) {
    AlertDialog(
        onDismissRequest = onNo,
        text = { Text(text) },
        confirmButton = { TextButton(onClick = onYes) { Text(stringResource(R.string.ok), color = MaterialTheme.colorScheme.error) } },
        dismissButton = { TextButton(onClick = onNo) { Text(stringResource(R.string.cancel)) } },
    )
}
