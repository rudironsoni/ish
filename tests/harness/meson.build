# iOS App Harness for APPSIM-001 through APPSIM-006
# Validates iOS simulator app testing through XcodeBuildMCP

ios_app_harness = executable('ios_app_harness',
    'ios_app_harness.c',
    include_directories: [include_directories('../..')],
    link_with: [libish_emu],
    install: false
)
