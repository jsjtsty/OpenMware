package org.openmware.app.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.SportsEsports
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SuggestionChip
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.LargeTopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import org.openmware.app.LauncherViewModel
import org.openmware.app.R
import org.openmware.app.Screen
import org.openmware.app.data.Game
import org.openmware.app.data.GameStatus

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LibraryScreen(vm: LauncherViewModel) {
    val games by vm.games.collectAsStateWithLifecycle()
    val scroll = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()
    Scaffold(
        modifier = Modifier.nestedScroll(scroll.nestedScrollConnection),
        topBar = {
            LargeTopAppBar(
                title = { Text(stringResource(R.string.library_title)) },
                actions = {
                    IconButton(onClick = { vm.open(Screen.About) }) {
                        Icon(Icons.Filled.Info, contentDescription = stringResource(R.string.menu_about))
                    }
                },
                scrollBehavior = scroll,
            )
        },
        floatingActionButton = {
            if (games.isNotEmpty()) {
                ExtendedFloatingActionButton(
                    onClick = { vm.open(Screen.Add) },
                    icon = { Icon(Icons.Filled.Add, null) },
                    text = { Text(stringResource(R.string.add_game)) },
                )
            }
        },
    ) { padding ->
        if (games.isEmpty()) {
            EmptyState(Modifier.padding(padding), onAdd = { vm.open(Screen.Add) })
        } else {
            LazyVerticalGrid(
                columns = GridCells.Adaptive(160.dp),
                modifier = Modifier.fillMaxSize().padding(padding),
                contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 96.dp),
                horizontalArrangement = Arrangement.spacedBy(16.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                items(games, key = { it.id }) { game -> GameCard(vm, game) }
            }
        }
    }
}

@Composable
private fun EmptyState(modifier: Modifier, onAdd: () -> Unit) {
    Column(
        modifier.fillMaxSize().padding(32.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Icon(Icons.Filled.SportsEsports, null, Modifier.size(72.dp), tint = MaterialTheme.colorScheme.primary)
        Spacer(Modifier.height(16.dp))
        Text(stringResource(R.string.empty_title), style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(8.dp))
        Text(
            stringResource(R.string.empty_body),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
        )
        Spacer(Modifier.height(24.dp))
        Button(onClick = onAdd) {
            Icon(Icons.Filled.Add, null)
            Spacer(Modifier.size(8.dp))
            Text(stringResource(R.string.add_game))
        }
    }
}

@Composable
private fun GameCard(vm: LauncherViewModel, game: Game) {
    val status = remember(game) { vm.repository.paths(game).status() }
    Card(
        onClick = { vm.open(Screen.Detail(game.id)) },
        shape = RoundedCornerShape(20.dp),
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp),
    ) {
        Box(Modifier.fillMaxWidth().aspectRatio(3f / 4f)) {
            CoverFill(game)
            Box(Modifier.align(Alignment.TopEnd).padding(8.dp)) {
                SuggestionChip(
                    onClick = {},
                    label = {
                        Text(
                            stringResource(
                                when (status) {
                                    GameStatus.Ready -> R.string.status_ready
                                    GameStatus.KeysMissing -> R.string.status_keys_missing
                                    GameStatus.Incomplete -> R.string.status_incomplete
                                }
                            ),
                            style = MaterialTheme.typography.labelSmall,
                        )
                    },
                )
            }
        }
        Column(Modifier.padding(12.dp)) {
            Text(game.name, style = MaterialTheme.typography.titleMedium, maxLines = 2, overflow = TextOverflow.Ellipsis)
            Text(
                if (game.lastPlayedAt > 0) stringResource(R.string.last_played, formatRelative(game.lastPlayedAt)) else stringResource(R.string.never_played),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
            )
        }
    }
}
