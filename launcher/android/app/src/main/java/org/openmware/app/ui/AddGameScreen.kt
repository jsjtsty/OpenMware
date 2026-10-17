package org.openmware.app.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Error
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.Key
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import org.openmware.app.LauncherViewModel
import org.openmware.app.R

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AddGameScreen(vm: LauncherViewModel) {
    val add = vm.add
    val pickFolder = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri -> if (uri != null) vm.folderPicked(uri) }
    val pickKeys = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> if (uri != null) vm.keysFilePicked(uri) }
    val scan = add.scan
    val enoughSpace = scan == null || vm.repository.freeBytes() > scan.totalBytes + (64L shl 20)
    val canAdd = scan != null && scan.isGame && enoughSpace && !add.importing

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.add_title)) },
                navigationIcon = {
                    IconButton(onClick = { vm.back() }, enabled = !add.importing) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back))
                    }
                },
            )
        },
    ) { padding ->
        Column(
            Modifier.fillMaxSize().padding(padding).verticalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Step(stringResource(R.string.add_step_folder)) {
                Text(stringResource(R.string.add_folder_hint), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                OutlinedButton(onClick = { pickFolder.launch(null) }, enabled = !add.importing) {
                    Icon(Icons.Filled.FolderOpen, null)
                    Spacer(Modifier.size(8.dp))
                    Text(stringResource(if (scan == null) R.string.add_choose_folder else R.string.add_change_folder))
                }
                if (scan != null) {
                    if (scan.isGame) {
                        StatusLine(true, stringResource(R.string.add_found, scan.archives.size, formatBytes(scan.totalBytes)))
                        if (!enoughSpace) StatusLine(false, stringResource(R.string.add_no_space, formatBytes(scan.totalBytes)))
                    } else {
                        StatusLine(false, stringResource(R.string.add_not_a_game))
                    }
                }
            }

            Step(stringResource(R.string.add_step_name)) {
                OutlinedTextField(
                    value = add.name,
                    onValueChange = { add.name = it },
                    label = { Text(stringResource(R.string.add_name_label)) },
                    singleLine = true,
                    enabled = !add.importing,
                    modifier = Modifier.fillMaxWidth(),
                )
            }

            Step(stringResource(R.string.add_step_keys)) {
                OutlinedButton(onClick = { pickKeys.launch(arrayOf("*/*")) }, enabled = !add.importing) {
                    Icon(Icons.Filled.Key, null)
                    Spacer(Modifier.size(8.dp))
                    Text(stringResource(R.string.add_keys_import))
                }
                OutlinedTextField(
                    value = add.keysText,
                    onValueChange = { add.keysText = it },
                    label = { Text(stringResource(R.string.add_keys_paste)) },
                    minLines = 3,
                    enabled = !add.importing,
                    modifier = Modifier.fillMaxWidth(),
                )
                if (add.keysText.isNotBlank()) {
                    val n = add.keyCount
                    StatusLine(n > 0, if (n > 0) stringResource(R.string.add_keys_ok, n) else stringResource(R.string.add_keys_invalid))
                }
            }

            add.error?.let { StatusLine(false, stringResource(R.string.add_failed, it)) }

            Button(onClick = { vm.startImport() }, enabled = canAdd, modifier = Modifier.fillMaxWidth().height(52.dp)) {
                Text(stringResource(R.string.add_import))
            }
            Spacer(Modifier.height(24.dp))
        }
    }

    if (add.importing) {
        AlertDialog(
            onDismissRequest = {},
            title = { Text(stringResource(R.string.add_copying)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    LinearProgressIndicator(progress = { if (add.total > 0) add.copied.toFloat() / add.total else 0f }, modifier = Modifier.fillMaxWidth())
                    Text(stringResource(R.string.add_copy_progress, formatBytes(add.copied), formatBytes(add.total)), style = MaterialTheme.typography.bodySmall)
                }
            },
            confirmButton = {},
            dismissButton = { TextButton(onClick = { vm.cancelImport() }) { Text(stringResource(R.string.cancel)) } },
        )
    }
}

@Composable
private fun Step(title: String, content: @Composable () -> Unit) {
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerLow)) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            content()
        }
    }
}

@Composable
fun StatusLine(ok: Boolean, text: String) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Icon(
            if (ok) Icons.Filled.CheckCircle else Icons.Filled.Error,
            null,
            tint = if (ok) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error,
            modifier = Modifier.size(20.dp),
        )
        Text(text, style = MaterialTheme.typography.bodyMedium)
    }
}
