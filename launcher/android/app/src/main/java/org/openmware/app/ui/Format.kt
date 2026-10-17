package org.openmware.app.ui

import android.text.format.DateUtils
import android.text.format.Formatter
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import org.openmware.app.R

@Composable
fun formatBytes(bytes: Long): String = Formatter.formatShortFileSize(LocalContext.current, bytes)

@Composable
fun formatRelative(time: Long): String =
    DateUtils.getRelativeTimeSpanString(time, System.currentTimeMillis(), DateUtils.MINUTE_IN_MILLIS).toString()

@Composable
fun formatDuration(millis: Long): String {
    val minutes = (millis / 60000).toInt()
    return if (minutes < 60) stringResource(R.string.minutes_short, minutes) else stringResource(R.string.hours_short, minutes / 60, minutes % 60)
}
