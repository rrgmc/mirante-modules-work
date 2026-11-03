# This repository's own build, for its CI and for working on a module. It is an
# app whose modules are the folders here, so that ci/manifest.json takes the
# place of an app's mirante.json. An app does not run this file. See
# docs/decisions/0125-how-modules-are-fetched.md in mirante-core.
cmake_minimum_required(VERSION 3.21)

project(mirante LANGUAGES NONE)
enable_testing()

set(MIRANTE_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/ci/manifest.json")
set(FETCHCONTENT_SOURCE_DIR_MIRANTE_MODULES_WORK "${CMAKE_CURRENT_SOURCE_DIR}")
file(READ "${MIRANTE_MANIFEST}" manifest)
string(JSON core_repo GET "${manifest}" core repo)
string(JSON core_ref GET "${manifest}" core ref)

include(FetchContent)
FetchContent_Declare(mirante_core GIT_REPOSITORY "${core_repo}" GIT_TAG "${core_ref}")
FetchContent_MakeAvailable(mirante_core)
