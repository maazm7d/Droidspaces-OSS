package com.droidspaces.app.util

/**
 * Data class representing a contributor to the project.
 *
 * @param name The contributor's name or GitHub username
 * @param role The contributor's role or contribution description
 * @param githubUrl The contributor's GitHub profile URL
 */
data class Contributor(
    val name: String,
    val role: String,
    val githubUrl: String
)

