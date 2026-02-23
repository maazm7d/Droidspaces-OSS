package com.droidspaces.app.ui.component

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/**
 * A root-aware file and directory picker dialog that allows browsing from the root (/).
 * Uses libsu (Shell) to bypass Android's scoped storage restrictions for administrative tasks.
 */
@Composable
fun FilePickerDialog(
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit,
    title: String = "Select Host Path",
    showFiles: Boolean = true
) {
    var currentPath by remember { mutableStateOf("/") }
    var items by remember { mutableStateOf<List<FileItem>>(emptyList()) }
    var isLoading by remember { mutableStateOf(true) }

    LaunchedEffect(currentPath) {
        isLoading = true
        items = fetchItems(currentPath, showFiles)
        isLoading = false
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { 
            Column {
                Text(title)
                Text(
                    text = currentPath,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.secondary,
                    overflow = TextOverflow.Ellipsis,
                    maxLines = 1,
                    modifier = Modifier.padding(top = 4.dp)
                )
            }
        },
        text = {
            Box(modifier = Modifier.height(400.dp).fillMaxWidth()) {
                if (isLoading) {
                    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                        CircularProgressIndicator()
                    }
                } else {
                    LazyColumn(modifier = Modifier.fillMaxSize()) {
                        if (currentPath != "/") {
                            item {
                                FileItemRow(name = "..", isDirectory = true) {
                                    val parent = File(currentPath).parent ?: "/"
                                    currentPath = parent
                                }
                            }
                        }
                        
                        items(items) { item ->
                            FileItemRow(name = item.name, isDirectory = item.isDirectory) {
                                if (item.isDirectory) {
                                    currentPath = if (currentPath == "/") "/${item.name}" else "$currentPath/${item.name}"
                                } else {
                                    onConfirm(if (currentPath == "/") "/${item.name}" else "$currentPath/${item.name}")
                                }
                            }
                        }
                        
                        if (items.isEmpty() && currentPath != "/") {
                            item {
                                Text(
                                    text = "Empty directory",
                                    modifier = Modifier.padding(16.dp),
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = MaterialTheme.colorScheme.outline
                                )
                            }
                        }
                    }
                }
            }
        },
        confirmButton = {
            Button(onClick = { onConfirm(currentPath) }) {
                Text("Select Current Folder")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancel")
            }
        }
    )
}

data class FileItem(val name: String, val isDirectory: Boolean)

@Composable
private fun FileItemRow(name: String, isDirectory: Boolean, onClick: () -> Unit) {
    Surface(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier
                .padding(vertical = 12.dp, horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = if (isDirectory) Icons.Default.Folder else Icons.Default.Description,
                contentDescription = null,
                modifier = Modifier.size(24.dp),
                tint = if (isDirectory) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.secondary
            )
            Spacer(modifier = Modifier.width(16.dp))
            Text(
                text = name,
                style = MaterialTheme.typography.bodyLarge,
                overflow = TextOverflow.Ellipsis,
                maxLines = 1
            )
        }
    }
}

private suspend fun fetchItems(path: String, showFiles: Boolean): List<FileItem> = withContext(Dispatchers.IO) {
    // We use ls -F to identify directories safely across different Android versions/busybox configs
    // The trailing slash identifies directories.
    val result = Shell.cmd("ls -F \"$path\" 2>/dev/null").exec()
    if (!result.isSuccess) return@withContext emptyList()
    
    result.out.mapNotNull { line ->
        if (line.isEmpty()) return@mapNotNull null
        
        // Some ls implementations might prefix with something, but usually it's just the name
        val rawName = line.trim()
        val isDirectory = rawName.endsWith("/")
        
        // Remove trailing indicators
        val cleanName = if (isDirectory) {
            rawName.dropLast(1)
        } else {
            // Remove common ls -F suffixes (* = exec, @ = link, etc.)
            val last = rawName.last()
            if (last == '*' || last == '@' || last == '=' || last == '|' || last == '>') {
                rawName.dropLast(1)
            } else {
                rawName
            }
        }
        
        if (cleanName.isEmpty()) return@mapNotNull null
        if (!showFiles && !isDirectory) return@mapNotNull null
        
        FileItem(cleanName, isDirectory)
    }.sortedWith(compareBy({ !it.isDirectory }, { it.name }))
}
