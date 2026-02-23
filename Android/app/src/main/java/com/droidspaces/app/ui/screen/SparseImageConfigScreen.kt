package com.droidspaces.app.ui.screen

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SparseImageConfigScreen(
    initialUseSparseImage: Boolean = false,
    initialSizeGB: Int = 8,
    onNext: (useSparseImage: Boolean, sizeGB: Int) -> Unit,
    onBack: () -> Unit
) {
    val context = LocalContext.current
    var useSparseImage by remember { mutableStateOf(initialUseSparseImage) }
    var sizeGB by remember { mutableStateOf(initialSizeGB.toString()) }
    var sizeError by remember { mutableStateOf<String?>(null) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(context.getString(R.string.sparse_image_configuration)) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = context.getString(R.string.back))
                    }
                }
            )
        },
        bottomBar = {
            Surface(
                tonalElevation = 2.dp,
                shadowElevation = 8.dp
            ) {
                Button(
                    onClick = {
                        if (useSparseImage) {
                            val sizeInt = sizeGB.toIntOrNull()
                            if (sizeInt != null && sizeInt >= 4 && sizeInt <= 512) {
                                onNext(useSparseImage, sizeInt)
                            }
                        } else {
                            onNext(false, 8)
                        }
                    },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(24.dp)
                        .navigationBarsPadding()
                        .height(56.dp),
                    enabled = !useSparseImage || (sizeGB.toIntOrNull()?.let { it in 4..512 } == true)
                ) {
                    Text(context.getString(R.string.next_summary), style = MaterialTheme.typography.labelLarge)
                }
            }
        }
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(24.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(24.dp)
        ) {
            Text(
                text = context.getString(R.string.storage_configuration),
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold
            )

            // Info Card explaining sparse images
            ElevatedCard(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.elevatedCardColors(
                    containerColor = MaterialTheme.colorScheme.surfaceVariant
                ),
                shape = RoundedCornerShape(20.dp),
                elevation = CardDefaults.elevatedCardElevation(defaultElevation = 1.dp)
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Info,
                            contentDescription = null,
                            modifier = Modifier.size(24.dp),
                            tint = MaterialTheme.colorScheme.primary
                        )
                        Text(
                            text = context.getString(R.string.about_sparse_images),
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.SemiBold
                        )
                    }

                    Text(
                        text = context.getString(R.string.sparse_images_recommended),
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )

                    Text(
                        text = context.getString(R.string.sparse_images_description),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
                    )
                }
            }

            // Toggle for sparse image
            Card(
                modifier = Modifier.fillMaxWidth(),
                onClick = { useSparseImage = !useSparseImage }
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(20.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(16.dp),
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Storage,
                            contentDescription = null,
                            modifier = Modifier.size(28.dp),
                            tint = MaterialTheme.colorScheme.primary
                        )
                        Column(
                            modifier = Modifier.weight(1f),
                            verticalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            Text(
                                text = context.getString(R.string.use_sparse_image),
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.SemiBold
                            )
                            Text(
                                text = context.getString(R.string.use_sparse_image_description),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                            )
                        }
                    }
                    Switch(
                        checked = useSparseImage,
                        onCheckedChange = { useSparseImage = it }
                    )
                }
            }

            // Size input (only shown when sparse image is enabled)
            if (useSparseImage) {
                Card(
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(20.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Default.Settings,
                                contentDescription = null,
                                modifier = Modifier.size(24.dp),
                                tint = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = context.getString(R.string.image_size),
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.SemiBold
                            )
                        }

                        OutlinedTextField(
                            value = sizeGB,
                            onValueChange = { newValue ->
                                sizeGB = newValue
                                val sizeInt = newValue.toIntOrNull()
                                sizeError = when {
                                    newValue.isEmpty() -> context.getString(R.string.size_required)
                                    sizeInt == null -> context.getString(R.string.invalid_number)
                                    sizeInt < 4 -> context.getString(R.string.minimum_size_4gb)
                                    sizeInt > 512 -> context.getString(R.string.maximum_size_512gb)
                                    else -> null
                                }
                            },
                            label = { Text(context.getString(R.string.size_gb)) },
                            placeholder = { Text("8") },
                            isError = sizeError != null,
                            supportingText = sizeError?.let { { Text(it) } } ?: {
                                Text(context.getString(R.string.enter_size_between_4_512_gb))
                            },
                            modifier = Modifier.fillMaxWidth(),
                            singleLine = true,
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                            leadingIcon = {
                                Icon(Icons.Default.Settings, contentDescription = null)
                            }
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}

