package com.droidspaces.app.ui.screen

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.List
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.showError
import com.droidspaces.app.ui.util.showSuccess
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.ui.component.PullToRefreshWrapper
import com.droidspaces.app.util.ContainerProcessManager
import kotlinx.coroutines.launch
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProcessListScreen(
    containerName: String,
    onNavigateBack: () -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }

    var processes by remember { mutableStateOf<List<ContainerProcessManager.ProcessInfo>>(emptyList()) }
    var isLoading by remember { mutableStateOf(false) }

    fun refreshProcesses() {
        scope.launch {
            isLoading = true
            processes = ContainerProcessManager.getProcessList(containerName)
            isLoading = false
        }
    }

    LaunchedEffect(containerName) {
        refreshProcesses()
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(context.getString(R.string.processes_title, containerName)) },
                navigationIcon = {
                    IconButton(onClick = onNavigateBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = context.getString(R.string.back))
                    }
                }
            )
        },
        snackbarHost = { SnackbarHost(snackbarHostState) }
    ) { padding ->
        PullToRefreshWrapper(onRefresh = { refreshProcesses() }) {
            if (isLoading && processes.isEmpty()) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(padding),
                    contentAlignment = Alignment.Center
                ) {
                    LoadingIndicator()
                }
            } else if (processes.isEmpty()) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(padding),
                    contentAlignment = Alignment.Center
                ) {
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Icon(
                            Icons.AutoMirrored.Filled.List,
                            contentDescription = null,
                            modifier = Modifier.size(48.dp),
                            tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
                        )
                        Text(
                            context.getString(R.string.no_processes_found),
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            } else {
                LazyColumn(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(padding),
                    contentPadding = PaddingValues(16.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    items(processes, key = { it.pid }) { process ->
                        ProcessItem(
                            process = process,
                            onKill = {
                                scope.launch {
                                    val success = ContainerProcessManager.killProcess(containerName, process.pid)
                                    if (success) {
                                        scope.showSuccess(snackbarHostState, context.getString(R.string.process_killed_success, process.pid))
                                        refreshProcesses()
                                    } else {
                                        scope.showError(snackbarHostState, context.getString(R.string.failed_to_kill_process, process.pid))
                                    }
                                }
                            }
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun ProcessItem(
    process: ContainerProcessManager.ProcessInfo,
    onKill: () -> Unit
) {
    val context = LocalContext.current
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = context.getString(R.string.pid_label, process.pid),
                        style = MaterialTheme.typography.bodyMedium,
                        fontWeight = FontWeight.Bold
                    )
                    process.user?.let {
                        Text(
                            text = context.getString(R.string.user_in_parentheses, it),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                Text(
                    text = process.command,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 2
                )
            }

            Column(
                horizontalAlignment = Alignment.End,
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                process.cpu?.let {
                    Text(
                        text = context.getString(R.string.cpu_percent, it.toString()),
                        style = MaterialTheme.typography.bodySmall,
                        fontWeight = FontWeight.Medium
                    )
                }
                process.mem?.let {
                    Text(
                        text = context.getString(R.string.mem_percent, it.toString()),
                        style = MaterialTheme.typography.bodySmall,
                        fontWeight = FontWeight.Medium
                    )
                }
                IconButton(
                    onClick = onKill,
                    modifier = Modifier.size(32.dp)
                ) {
                    Icon(
                        Icons.Default.Delete,
                        contentDescription = context.getString(R.string.kill_process),
                        tint = MaterialTheme.colorScheme.error,
                        modifier = Modifier.size(18.dp)
                    )
                }
            }
        }
    }
}

