# Opt-in presentation/input feature. No simulation, math or protocol settings.
option(SAGE_USE_OPENXR "Build the Quest OpenXR presentation and input layer" OFF)
# GeneralsX @build Codex 09/10/2026 One version for launcher, native menu and package evidence.
if(SAGE_USE_OPENXR)
    if(NOT ANDROID OR NOT SAGE_USE_SDL3)
        message(FATAL_ERROR "Quest OpenXR requires the Android SDL3 build")
    endif()
    set(QUEST_OPENXR_ROOT "${CMAKE_SOURCE_DIR}/work/dependencies/openxr" CACHE PATH "Verified OpenXR SDK headers")
    set(QUEST_OPENXR_LOADER "${CMAKE_SOURCE_DIR}/work/dependencies/openxr-loader/jni/arm64-v8a/libopenxr_loader.so" CACHE FILEPATH "Verified Android OpenXR loader")
    if(NOT EXISTS "${QUEST_OPENXR_ROOT}/include/openxr/openxr.h" OR NOT EXISTS "${QUEST_OPENXR_LOADER}")
        message(FATAL_ERROR "Run scripts/tooling/quest/prepare-dependencies.py before configuring Quest")
    endif()
    add_library(quest_openxr_loader SHARED IMPORTED GLOBAL)
    set_target_properties(quest_openxr_loader PROPERTIES
        IMPORTED_LOCATION "${QUEST_OPENXR_LOADER}"
        INTERFACE_INCLUDE_DIRECTORIES "${QUEST_OPENXR_ROOT}/include")
    add_library(OpenXR::openxr_loader ALIAS quest_openxr_loader)
    include_directories("${QUEST_OPENXR_ROOT}/include")
    include_directories("${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include")
    # A version change should invalidate only its presentation consumer, not all engine targets.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/docs/WORKDIR/quest/APP_VERSION.json")
    file(READ "${CMAKE_SOURCE_DIR}/docs/WORKDIR/quest/APP_VERSION.json" QUEST_XR_VERSION_JSON)
    string(JSON QUEST_XR_APP_VERSION GET "${QUEST_XR_VERSION_JSON}" versionName)
    configure_file("${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include/VRDevice/QuestAppVersion.h.in"
        "${CMAKE_BINARY_DIR}/quest-generated/QuestAppVersion.h" @ONLY)
    include_directories("${CMAKE_BINARY_DIR}/quest-generated")
    add_compile_definitions(SAGE_USE_OPENXR=1
        QUEST_ANDROID_PACKAGE="com.generalsx.questvr")
endif()
