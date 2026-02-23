package com.droidspaces.app.ui.screen

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import com.droidspaces.app.ui.component.PullToRefreshWrapper
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.activity.compose.BackHandler
import androidx.lifecycle.viewmodel.compose.viewModel
import com.droidspaces.app.ui.component.DroidspacesStatus
import com.droidspaces.app.ui.component.DroidspacesStatusCard
import com.droidspaces.app.ui.component.SystemInfoCard
import com.droidspaces.app.util.DroidspacesBackendStatus
import com.droidspaces.app.util.PreferencesManager
import com.droidspaces.app.util.SystemInfoManager
import com.droidspaces.app.ui.viewmodel.AppStateViewModel
import com.droidspaces.app.ui.viewmodel.ContainerViewModel
import kotlinx.coroutines.launch
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R

enum class TabItem(val titleResId: Int, val icon: androidx.compose.ui.graphics.vector.ImageVector) {
    Home(R.string.home_title, Icons.Default.Home),
    Containers(R.string.containers, Icons.Default.Storage),
    ControlPanel(R.string.panel, Icons.Default.Dashboard)
}

/**
 * Main tab screen with optimized state management.
 *
 * Key improvements:
 * 1. Uses AppStateViewModel for backend status (persists across navigation)
 * 2. No redundant state variables (refreshCounter, shouldTriggerAnimatedRefresh removed)
 * 3. Single coroutine scope for all operations
 * 4. Proper recomposition boundaries
 *
 * This fixes the "settings back button glitch" by NOT re-checking backend on navigation.
 * Backend is only checked on:
 * 1. Cold app start
 * 2. Pull-to-refresh
 * 3. Post-installation (when returning from installation flow)
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainTabScreen(
    containerViewModel: ContainerViewModel,
    skipInitialRefresh: Boolean = false,
    onNavigateToSettings: () -> Unit = {},
    onNavigateToInstallation: () -> Unit = {},
    onNavigateToContainerInstallation: (android.net.Uri) -> Unit = {},
    onNavigateToEditContainer: (String) -> Unit = {},
    onNavigateToContainerDetails: (String) -> Unit = {}
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    // ViewModels - persist across navigation (activity-scoped)
    val appStateViewModel: AppStateViewModel = viewModel()
    // containerViewModel is now passed as parameter to ensure sharing


    // Tab selection - survives configuration changes
    var selectedTab by rememberSaveable { mutableStateOf(TabItem.Home) }

    // Handle back press - return to Home tab if not already there
    BackHandler(enabled = selectedTab != TabItem.Home) {
        selectedTab = TabItem.Home
    }

    // Track if we've already triggered initial load in this session
    // This prevents re-triggering on navigation back from Settings
    var hasTriggeredInitialLoad by rememberSaveable { mutableStateOf(false) }

    // Initial setup - only runs ONCE per app session (not on navigation back)
    // The key is a combination that only changes on cold start or post-installation
    LaunchedEffect(skipInitialRefresh, hasTriggeredInitialLoad) {
        if (!hasTriggeredInitialLoad) {
            hasTriggeredInitialLoad = true

            if (!skipInitialRefresh) {
                // Post-installation: reset and force refresh
                appStateViewModel.resetForPostInstallation()
            }

            // Always force check on initial boot to detect updates after app installation
            // This ensures we check for new binaries even if cached status exists
            appStateViewModel.checkBackendStatus(force = true)

            // Fetch containers if backend is available
            if (appStateViewModel.isBackendAvailable) {
                containerViewModel.fetchContainerList()
            }
        }
    }

    // Fetch containers when backend BECOMES available (state change only)
    // This handles the case where backend check completes after initial load
    val previousBackendAvailable = remember { mutableStateOf(appStateViewModel.isBackendAvailable) }
    LaunchedEffect(appStateViewModel.isBackendAvailable) {
        // Only trigger if backend just BECAME available (was false, now true)
        if (appStateViewModel.isBackendAvailable && !previousBackendAvailable.value) {
            containerViewModel.fetchContainerList()
        }
        previousBackendAvailable.value = appStateViewModel.isBackendAvailable
    }

    // Map backend status to UI status - remember previous status to prevent glitches
    val currentBackendStatus = appStateViewModel.backendStatus
    val prefsManager = remember { PreferencesManager.getInstance(context) }

    // Initialize stable status from cached backend status to prevent initial boot glitch
    val stableDroidspacesStatus = remember {
        mutableStateOf<DroidspacesStatus?>(
            prefsManager.cachedBackendStatus?.let { cached ->
                when (cached) {
                    "UpdateAvailable" -> DroidspacesStatus.UpdateAvailable
                    "NotInstalled" -> DroidspacesStatus.NotInstalled
                    "Corrupted" -> DroidspacesStatus.Corrupted
                    "ModuleMissing" -> DroidspacesStatus.ModuleMissing
                    "" -> DroidspacesStatus.Working
                    else -> null
                }
            }
        )
    }

    // Track previous root status to detect when root becomes unavailable
    var previousRootAvailable by remember { mutableStateOf(appStateViewModel.isRootAvailable) }

    // Update stable status when backend status changes (but skip Checking to prevent flicker)
    LaunchedEffect(currentBackendStatus, appStateViewModel.isRootAvailable) {
        // Skip updates during Checking state to prevent flicker during refresh
        if (currentBackendStatus is DroidspacesBackendStatus.Checking) {
            return@LaunchedEffect
        }

        val newStatus = when (currentBackendStatus) {
            is DroidspacesBackendStatus.Available -> DroidspacesStatus.Working
            is DroidspacesBackendStatus.UpdateAvailable -> DroidspacesStatus.UpdateAvailable
            is DroidspacesBackendStatus.NotInstalled -> DroidspacesStatus.NotInstalled
            is DroidspacesBackendStatus.Corrupted -> DroidspacesStatus.Corrupted
            is DroidspacesBackendStatus.ModuleMissing -> DroidspacesStatus.ModuleMissing
            is DroidspacesBackendStatus.Checking -> return@LaunchedEffect // Already handled above
        }

        // Always update stable status when we have a non-Checking status
        // This allows updates from error to working (e.g., after installation)
        stableDroidspacesStatus.value = newStatus

        previousRootAvailable = appStateViewModel.isRootAvailable
    }

    // Use stable status if available, otherwise compute from current status
    val droidspacesStatus: DroidspacesStatus = stableDroidspacesStatus.value ?: when (currentBackendStatus) {
        is DroidspacesBackendStatus.Checking -> DroidspacesStatus.Working
        is DroidspacesBackendStatus.Available -> DroidspacesStatus.Working
        is DroidspacesBackendStatus.UpdateAvailable -> DroidspacesStatus.UpdateAvailable
        is DroidspacesBackendStatus.NotInstalled -> DroidspacesStatus.NotInstalled
        is DroidspacesBackendStatus.Corrupted -> DroidspacesStatus.Corrupted
        is DroidspacesBackendStatus.ModuleMissing -> DroidspacesStatus.ModuleMissing
    }

    // UI state from ViewModels
    val isBackendAvailable = appStateViewModel.isBackendAvailable
    // Only show checking on initial load when we don't have cached status
    val isChecking = !appStateViewModel.hasCompletedInitialCheck && stableDroidspacesStatus.value == null
    val containerCount = containerViewModel.containerCount
    val runningCount = containerViewModel.runningCount

    /**
     * Combined refresh function for pull-to-refresh.
     * Refreshes both backend status and container list.
     */
    suspend fun performRefresh() {
        // Check root status first (in case user denied root access)
        appStateViewModel.checkRootStatus()
        // Then refresh backend status
        appStateViewModel.forceRefresh()
        // Force refresh droidspaces version to get latest after backend updates
        if (appStateViewModel.isBackendAvailable) {
            SystemInfoManager.refreshDroidspacesVersion(context)
            containerViewModel.fetchContainerList()
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Default.Storage,
                            contentDescription = null,
                            modifier = Modifier
                                .padding(end = 8.dp)
                                .size(24.dp)
                        )
                        Text(
                            text = when (selectedTab) {
                                TabItem.Home -> context.getString(R.string.droidspaces_title)
                                TabItem.Containers -> context.getString(R.string.containers)
                                TabItem.ControlPanel -> context.getString(R.string.panel)
                            },
                            style = MaterialTheme.typography.titleLarge,
                            fontWeight = FontWeight.Black
                        )
                    }
                },
                actions = {
                    IconButton(onClick = onNavigateToSettings) {
                        Icon(imageVector = Icons.Default.Settings, contentDescription = context.getString(R.string.settings))
                    }
                },
                windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal)
            )
        },
        bottomBar = {
            NavigationBar {
                TabItem.entries.forEach { tab ->
                    NavigationBarItem(
                        icon = { Icon(tab.icon, contentDescription = null) },
                        label = { Text(context.getString(tab.titleResId)) },
                        selected = selectedTab == tab,
                        onClick = { selectedTab = tab }
                    )
                }
            }
        },
        contentWindowInsets = WindowInsets(0)
    ) { innerPadding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
        ) {
            when (selectedTab) {
                TabItem.Home -> {
                    HomeTabContent(
                        droidspacesStatus = droidspacesStatus,
                        isChecking = isChecking,
                        isRootAvailable = appStateViewModel.isRootAvailable,
                        onNavigateToInstallation = onNavigateToInstallation,
                        onNavigateToContainers = { selectedTab = TabItem.Containers },
                        onNavigateToControlPanel = { selectedTab = TabItem.ControlPanel },
                        containerCount = containerCount,
                        runningCount = runningCount,
                        onRefresh = { scope.launch { performRefresh() } }
                    )
                }

                TabItem.Containers -> {
                    ContainersTabContent(
                        isBackendAvailable = isBackendAvailable,
                        isRootAvailable = appStateViewModel.isRootAvailable,
                        onNavigateToInstallation = onNavigateToContainerInstallation,
                        onNavigateToEditContainer = onNavigateToEditContainer,
                        containerViewModel = containerViewModel,
                        onRefresh = { scope.launch { performRefresh() } }
                    )
                }

                TabItem.ControlPanel -> {
                    ControlPanelTabContent(
                        isBackendAvailable = isBackendAvailable,
                        isRootAvailable = appStateViewModel.isRootAvailable,
                        containerViewModel = containerViewModel,
                        onRefresh = { scope.launch { performRefresh() } },
                        onNavigateToContainerDetails = onNavigateToContainerDetails
                    )
                }
            }
        }
    }
}

@Composable
private fun HomeTabContent(
    droidspacesStatus: DroidspacesStatus,
    isChecking: Boolean,
    isRootAvailable: Boolean,
    onNavigateToInstallation: () -> Unit,
    onNavigateToContainers: () -> Unit,
    onNavigateToControlPanel: () -> Unit,
    containerCount: Int,
    runningCount: Int,
    onRefresh: () -> Unit
) {
    val context = LocalContext.current
    // Track refresh trigger for SystemInfoCard
    var refreshTrigger by remember { mutableStateOf(0) }

    PullToRefreshWrapper(
        onRefresh = {
            onRefresh()
            refreshTrigger++
        }
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp)
                .padding(top = 16.dp, bottom = 16.dp)
        ) {
            DroidspacesStatusCard(
                status = droidspacesStatus,
                version = null,
                isChecking = isChecking,
                isRootAvailable = isRootAvailable,
                onClick = {
                    if (!isRootAvailable) {
                        // Disabled for non-root users
                        return@DroidspacesStatusCard
                    }
                    if (droidspacesStatus == DroidspacesStatus.NotInstalled ||
                        droidspacesStatus == DroidspacesStatus.Corrupted ||
                        droidspacesStatus == DroidspacesStatus.UpdateAvailable ||
                        droidspacesStatus == DroidspacesStatus.ModuleMissing
                    ) {
                        onNavigateToInstallation()
                    }
                }
            )

            Spacer(modifier = Modifier.height(16.dp))

            // Only show container and running count cards if root is available
            if (isRootAvailable) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    // Container count card
                    Card(
                        onClick = onNavigateToContainers,
                        modifier = Modifier
                            .weight(1f)
                            .padding(vertical = 8.dp),
                        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
                        shape = RoundedCornerShape(20.dp)
                    ) {
                        Column(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(20.dp),
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            Text(
                                text = containerCount.toString(),
                                style = MaterialTheme.typography.headlineLarge,
                                fontWeight = FontWeight.Bold
                            )
                            Text(
                                text = context.getString(R.string.containers),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.outline
                            )
                        }
                    }

                    // Running count card
                    Card(
                        onClick = onNavigateToControlPanel,
                        modifier = Modifier
                            .weight(1f)
                            .padding(vertical = 8.dp),
                        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
                        shape = RoundedCornerShape(20.dp)
                    ) {
                        Column(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(20.dp),
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            Text(
                                text = runningCount.toString(),
                                style = MaterialTheme.typography.headlineLarge,
                                fontWeight = FontWeight.Bold
                            )
                            Text(
                                text = context.getString(R.string.running),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.outline
                            )
                        }
                    }
                }

                Spacer(modifier = Modifier.height(6.dp))
            }

            SystemInfoCard(refreshTrigger = refreshTrigger)
            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}

@Composable
private fun ContainersTabContent(
    isBackendAvailable: Boolean,
    isRootAvailable: Boolean,
    onNavigateToInstallation: (android.net.Uri) -> Unit,
    onNavigateToEditContainer: (String) -> Unit,
    containerViewModel: ContainerViewModel,
    onRefresh: () -> Unit
) {
    PullToRefreshWrapper(onRefresh = { onRefresh() }) {
        ContainersScreen(
            isBackendAvailable = isBackendAvailable,
            isRootAvailable = isRootAvailable,
            onNavigateToInstallation = onNavigateToInstallation,
            onNavigateToEditContainer = onNavigateToEditContainer,
            containerViewModel = containerViewModel
        )
    }
}

@Composable
private fun ControlPanelTabContent(
    isBackendAvailable: Boolean,
    isRootAvailable: Boolean,
    containerViewModel: ContainerViewModel,
    onRefresh: () -> Unit,
    onNavigateToContainerDetails: (String) -> Unit
) {
    PullToRefreshWrapper(onRefresh = { onRefresh() }) {
        ControlPanelScreen(
            isBackendAvailable = isBackendAvailable,
            isRootAvailable = isRootAvailable,
            containerViewModel = containerViewModel,
            onNavigateToContainerDetails = onNavigateToContainerDetails
        )
    }
}
