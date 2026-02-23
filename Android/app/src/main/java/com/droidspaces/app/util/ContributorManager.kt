package com.droidspaces.app.util

import android.content.Context
import com.droidspaces.app.R

/**
 * Utility class for loading and managing contributors from XML resources.
 *
 * Contributors are stored in res/values/contributors.xml as string arrays.
 * Each contributor is represented as a string array with 3 elements:
 * [0] = name
 * [1] = role
 * [2] = githubUrl
 *
 * To add a new contributor, simply add a new string-array to contributors.xml
 * with the naming pattern "contributor_N" and add the resource name to the
 * CONTRIBUTOR_RESOURCE_NAMES list below.
 */
object ContributorManager {

    /**
     * List of contributor resource names in order.
     * Add new contributors here when adding them to contributors.xml
     */
    private val CONTRIBUTOR_RESOURCE_NAMES = listOf(
        "contributor_1",
        "contributor_2",
        "contributor_3",
        "contributor_4",
        "contributor_5"
    )

    /**
     * Loads all contributors from XML resources.
     *
     * @param context The application context
     * @return List of Contributor objects parsed from XML
     */
    fun loadContributors(context: Context): List<Contributor> {
        val contributors = mutableListOf<Contributor>()
        val resources = context.resources

        for (resourceName in CONTRIBUTOR_RESOURCE_NAMES) {
            try {
                val resourceId = resources.getIdentifier(
                    resourceName,
                    "array",
                    context.packageName
                )

                if (resourceId != 0) {
                    val details = resources.getStringArray(resourceId)
                    if (details.size == 3) {
                        val contributor = Contributor(
                            name = details[0],
                            role = details[1],
                            githubUrl = details[2]
                        )
                        contributors.add(contributor)
                    }
                }
            } catch (e: Exception) {
                // Skip invalid contributors and continue
                e.printStackTrace()
            }
        }

        return contributors
    }
}

