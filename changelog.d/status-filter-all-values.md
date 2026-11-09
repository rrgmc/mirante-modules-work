### Fixed

- The Status filter of the GitHub run lists and the GitLab pipeline lists always lists every status. Before, it listed only the statuses of the current rows, so a chosen status, such as Running, fell back to "All" while no row had it. It needs the core's `ItemListView::setFilterValues()`.
