cmake_minimum_required(VERSION 3.14.0)
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.12 CACHE STRING "")

set(vst3sdk_SOURCE_DIR D:/codes/vst-sdk_3.7.6_build-18_2022-09-05/VST_SDK/vst3sdk)
if(NOT vst3sdk_SOURCE_DIR)
    message(FATAL_ERROR "Path to VST3 SDK is empty!")
endif()

if(NOT SMTG_ADD_VSTGUI)
    return()
endif()

project(NetProcess
    # This is your plug-in version number. Change it here only.
    # Version number symbols usable in C++ can be found in
    # source/version.h and ${PROJECT_BINARY_DIR}/projectversion.h.
    VERSION 1.0.0.0 
    DESCRIPTION "NetProcess VST 3 Plug-in"
)

set(SMTG_VSTGUI_ROOT "${vst3sdk_SOURCE_DIR}")

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

smtg_add_vst3plugin(NetProcess
    source/version.h
    source/ss_cids.h
    source/ss_processor.h
    source/ss_processor.cpp
    source/ss_controller.h
    source/ss_controller.cpp
    source/ss_entry.cpp
)

#- VSTGUI Wanted ----
if(SMTG_ADD_VSTGUI)
    target_sources(NetProcess
        PRIVATE
            resource/ss_editor.uidesc
    )
    target_link_libraries(NetProcess
        PRIVATE
            vstgui_support
    )
    smtg_target_add_plugin_resources(NetProcess
        RESOURCES
            "resource/ss_editor.uidesc"
    )
endif(SMTG_ADD_VSTGUI)
# -------------------

smtg_target_add_plugin_snapshots (NetProcess
    RESOURCES
        resource/D12E2F4DB12258BDA3EAF720EDB3C40C_snapshot.png
        resource/D12E2F4DB12258BDA3EAF720EDB3C40C_snapshot_2.0x.png
)

target_link_libraries(NetProcess
    PRIVATE
        sdk
)

smtg_target_configure_version_file(NetProcess)

if(SMTG_MAC)
    smtg_target_set_bundle(NetProcess
        BUNDLE_IDENTIFIER natas.sinesynth
        COMPANY_NAME "natas"
    )
    smtg_target_set_debug_executable(NetProcess
        "/Applications/VST3PluginTestHost.app"
        "--pluginfolder;$(BUILT_PRODUCTS_DIR)"
    )
elseif(SMTG_WIN)
    target_sources(NetProcess PRIVATE 
        resource/win32resource.rc
    )
    if(MSVC)
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT NetProcess)

        smtg_target_set_debug_executable(NetProcess
            "$(ProgramW6432)/Steinberg/VST3PluginTestHost/VST3PluginTestHost.exe"
            "--pluginfolder \"$(OutDir)/\""
        )
    endif()
endif(SMTG_MAC)

set(jsoncpp_DIR D:/codes/vcpkg/packages/jsoncpp_x64-windows/share/jsoncpp)
find_package(jsoncpp CONFIG REQUIRED)
target_link_libraries(NetProcess PRIVATE jsoncpp_lib jsoncpp_object JsonCpp::JsonCpp)