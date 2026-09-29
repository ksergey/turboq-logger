set(CPM_DOWNLOAD_VERSION 0.42.0)
set(CPM_HASH_SUM "2020b4fc42dba44817983e06342e682ecfc3d2f484a581f11cc5731fbe4dce8a")

if(CPM_SOURCE_CACHE)
    set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
    set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else()
    set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()

# Expand relative path. This is important if the provided path contains a tilde (~)
get_filename_component(CPM_DOWNLOAD_LOCATION ${CPM_DOWNLOAD_LOCATION} ABSOLUTE)

file(DOWNLOAD https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
     ${CPM_DOWNLOAD_LOCATION} EXPECTED_HASH SHA256=${CPM_HASH_SUM})

# CPM caches CPM_DIRECTORY and on a later configure silently skips its own initialization if
# CPM.cmake now lives somewhere else (e.g. CPM_SOURCE_CACHE was set, changed or removed since the
# build dir was created), leaving CPMAddPackage undefined. Drop the stale value -- unless CPM was
# already initialized in this run by a parent project, in which case its early return is correct.
get_property(_getcpm_initialized GLOBAL PROPERTY CPM_INITIALIZED SET)
if(NOT _getcpm_initialized)
    unset(CPM_DIRECTORY CACHE)
endif()
unset(_getcpm_initialized)

include(${CPM_DOWNLOAD_LOCATION})
