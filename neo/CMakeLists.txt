cmake_minimum_required(VERSION 3.20)
project(Doom3BFG_SDL LANGUAGES C CXX)

# Target Linux with SDL2
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

# Build configuration
set(CMAKE_BUILD_TYPE RelWithDebInfo)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Warning flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-switch -Wno-parentheses -Wno-multichar -Wno-format")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations")

# Platform definitions (for compatibility with original code)
add_compile_definitions(_CRT_SECURE_NO_WARNINGS ID_GL_HARDLINK ID_SINGLEPLAYER_ONLY)

# Platform detection
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" OR MINGW)
    add_compile_definitions(ID_PC_WIN)
else()
    add_compile_definitions(ID_PC_LINUX)
endif()

# Find SDL2
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2>=2.0.14)

# FFmpeg (libav*) — used by the engine-side Bink (.bik) cinematic decoder
# in renderer/Cinematic.cpp (B8: video playback with stock assets)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libswscale libavutil)
link_directories(${FFMPEG_LIBRARY_DIRS})
# pkg_check_modules(SDL2_IMAGE sdl2_image>=2.0.5)
# pkg_check_modules(SDL2_MIXER sdl2_mixer>=2.0.4)

# Debug SDL2 paths
message(STATUS "SDL2_INCLUDE_DIRS: ${SDL2_INCLUDE_DIRS}")
message(STATUS "SDL2_CFLAGS: ${SDL2_CFLAGS}")
message(STATUS "SDL2_CFLAGS_OTHER: ${SDL2_CFLAGS_OTHER}")

# OpenGL
find_package(OpenGL REQUIRED)

# Platform libraries
set(PLATFORM_LIBS
    ${SDL2_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    ${OPENGL_LIBRARIES}
    pthread
    dl
    m
    rt
)

# Source file collections
file(GLOB IDLIB_SOURCES
    idlib/*.cpp
    idlib/*.c
    idlib/math/*.cpp
    idlib/math/*.c
    idlib/bv/*.cpp
    idlib/bv/*.c
    idlib/containers/*.cpp
    idlib/containers/*.c
    idlib/sys/*.cpp
    idlib/sys/*.c
    idlib/geometry/*.cpp
    idlib/geometry/*.c
    idlib/hashing/*.cpp
    idlib/hashing/*.c
)

file(GLOB RENDERER_SOURCES
    renderer/*.cpp
    renderer/*.c
    renderer/OpenGL/*.cpp
    renderer/DXT/*.cpp
    renderer/Color/*.cpp
    renderer/jpeg-6/*.c
    renderer/jobs/*.cpp
    renderer/jobs/*/*.cpp
    renderer/backend/*.cpp
    renderer/backend/*.c
)

file(GLOB FRAMEWORK_SOURCES
    framework/*.cpp
)

# On Linux, replace Session.cpp with Session_linux.cpp
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT MINGW)
    list(REMOVE_ITEM FRAMEWORK_SOURCES ${CMAKE_SOURCE_DIR}/framework/Session.cpp)
    list(APPEND FRAMEWORK_SOURCES ${CMAKE_SOURCE_DIR}/framework/Session_linux.cpp)
endif()

file(GLOB SYS_SOURCES
    sys/sdl/*.cpp
    sys/*.cpp
)
list(REMOVE_ITEM SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sdl/sdl_main.cpp)
list(REMOVE_ITEM SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/win32/*.cpp)
list(REMOVE_ITEM SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sys_session_callbacks.cpp)
list(REMOVE_ITEM SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sys_session_local.cpp)
list(REMOVE_ITEM SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sys_lobby.cpp)
list(REMOVE_ITEM SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sys_lobby_backend_direct.cpp)
list(REMOVE_ITEM SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sys_lobby_migrate.cpp)
list(REMOVE_ITEM SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sys_lobby_snapshot.cpp)
list(REMOVE_ITEM SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sys_lobby_users.cpp)
list(REMOVE_ITEM SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sys_dedicated_server_search.cpp)
list(REMOVE_ITEM SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sys_voicechat.cpp)
list(REMOVE_ITEM SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sdl/doomlib_stubs.cpp)
list(REMOVE_ITEM SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sys_local.cpp)
list(REMOVE_ITEM SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sys_localuser.cpp)

# Add Linux stubs
list(APPEND SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sdl/sys_leaderboard_stub.cpp)
list(APPEND SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sdl/sys_signin_stub.cpp)
list(APPEND SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sdl/sys_linux_stubs.cpp)
list(APPEND SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sdl/doomlib_linux.cpp)

file(GLOB SOUND_SOURCES
    sound/*.cpp
    sound/sdl/*.cpp
)

file(GLOB SWF_SOURCES
    swf/*.cpp
)

file(GLOB UI_SOURCES
    ui/*.cpp
)

file(GLOB GAME_SOURCES
    d3xp/*.cpp
    d3xp/ai/*.cpp
    d3xp/anim/*.cpp
    d3xp/physics/*.cpp
    d3xp/gamesys/*.cpp
    d3xp/script/*.cpp
    d3xp/menus/MenuHandler.cpp
    d3xp/menus/MenuHandler_HUD.cpp
    d3xp/menus/MenuHandler_PDA.cpp
    d3xp/menus/MenuHandler_Scoreboard.cpp
    d3xp/menus/MenuHandler_Shell.cpp
    d3xp/menus/MenuScreen.cpp
    d3xp/menus/MenuScreen_HUD.cpp
    d3xp/menus/MenuScreen_PDA_Inventory.cpp
    d3xp/menus/MenuScreen_PDA_UserData.cpp
    d3xp/menus/MenuScreen_PDA_UserEmails.cpp
    d3xp/menus/MenuScreen_PDA_VideoDisks.cpp
    d3xp/menus/MenuScreen_Scoreboard.cpp
    d3xp/menus/MenuScreen_Shell_Bindings.cpp
    d3xp/menus/MenuScreen_Shell_Browser.cpp
    d3xp/menus/MenuScreen_Shell_ControllerLayout.cpp
    d3xp/menus/MenuScreen_Shell_Controls.cpp
    d3xp/menus/MenuScreen_Shell_Credits.cpp
    d3xp/menus/MenuScreen_Shell_Dev.cpp
    d3xp/menus/MenuScreen_Shell_Difficulty.cpp
    d3xp/menus/MenuScreen_Shell_GameLobby.cpp
    d3xp/menus/MenuScreen_Shell_GameOptions.cpp
    d3xp/menus/MenuScreen_Shell_Gamepad.cpp
    d3xp/menus/MenuScreen_Shell_Leaderboards.cpp
    d3xp/menus/MenuScreen_Shell_Load.cpp
    d3xp/menus/MenuScreen_Shell_MatchSettings.cpp
    d3xp/menus/MenuScreen_Shell_ModeSelect.cpp
    d3xp/menus/MenuScreen_Shell_NewGame.cpp
    d3xp/menus/MenuScreen_Shell_PartyLobby.cpp
    d3xp/menus/MenuScreen_Shell_Pause.cpp
    d3xp/menus/MenuScreen_Shell_Playstation.cpp
    d3xp/menus/MenuScreen_Shell_PressStart.cpp
    d3xp/menus/MenuScreen_Shell_Resolution.cpp
    d3xp/menus/MenuScreen_Shell_Root.cpp
    d3xp/menus/MenuScreen_Shell_Save.cpp
    d3xp/menus/MenuScreen_Shell_Settings.cpp
    d3xp/menus/MenuScreen_Shell_Singleplayer.cpp
    d3xp/menus/MenuScreen_Shell_Stereoscopics.cpp
    d3xp/menus/MenuScreen_Shell_SystemOptions.cpp
    d3xp/menus/MenuWidget_Button.cpp
    d3xp/menus/MenuWidget_Carousel.cpp
    d3xp/menus/MenuWidget_CommandBar.cpp
    d3xp/menus/MenuWidget.cpp
    d3xp/menus/MenuWidget_DynamicList.cpp
    d3xp/menus/MenuWidget_Help.cpp
    d3xp/menus/MenuWidget_InfoBox.cpp
    d3xp/menus/MenuWidget_ItemAssignment.cpp
    d3xp/menus/MenuWidget_List.cpp
    d3xp/menus/MenuWidget_LobbyList.cpp
    d3xp/menus/MenuWidget_MenuBar.cpp
    d3xp/menus/MenuWidget_NavBar.cpp
    d3xp/menus/MenuWidget_NavButton.cpp
    d3xp/menus/MenuWidget_PDA_AudioFiles.cpp
    d3xp/menus/MenuWidget_PDA_EmailInbox.cpp
    d3xp/menus/MenuWidget_PDA_Objective.cpp
    d3xp/menus/MenuWidget_PDA_UserData.cpp
    d3xp/menus/MenuWidget_PDA_VideoInfo.cpp
    d3xp/menus/MenuWidget_Scrollbar.cpp
    d3xp/menus/MenuWidget_Shell_SaveInfo.cpp
    cm/*.cpp
    aas/*.cpp
)

# Exclude Callbacks.cpp which is included in Class.cpp
list(REMOVE_ITEM GAME_SOURCES ${CMAKE_SOURCE_DIR}/d3xp/gamesys/Callbacks.cpp)
list(REMOVE_ITEM GAME_SOURCES ${CMAKE_SOURCE_DIR}/d3xp/Leaderboards.cpp)

# Add Linux stubs
list(APPEND SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sdl/sys_leaderboard_stub.cpp)
list(APPEND SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sdl/sys_signin_stub.cpp)
list(APPEND SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sdl/sys_linux_stubs.cpp)

list(REMOVE_ITEM SYS_SOURCES ${CMAKE_SOURCE_DIR}/sys/sys_signin.cpp)

file(GLOB DOOMEXE_SOURCES
    sys/sdl/sdl_main.cpp
)

# Resource file (skip for Linux)
# set(DOOMEXE_RC sys/win32/rc/doom.rc)

# --- Libraries ---

# idlib static library
add_library(idlib STATIC ${IDLIB_SOURCES})
target_include_directories(idlib PUBLIC
    ${CMAKE_SOURCE_DIR}/idlib
    ${CMAKE_SOURCE_DIR}/idlib/sys
    SYSTEM ${CMAKE_SOURCE_DIR}/idlib/sys
    ${CMAKE_SOURCE_DIR}
    ${SDL2_INCLUDE_DIRS}
)
target_compile_definitions(idlib PUBLIC IDLIB_EXPORTS)
target_compile_options(idlib PRIVATE -fno-rtti)

# external static library (zlib + jpeg)
add_library(external STATIC
    framework/zlib/adler32.c
    framework/zlib/compress.c
    framework/zlib/crc32.c
    framework/zlib/deflate.c
    framework/zlib/gzio.c
    framework/zlib/infback.c
    framework/zlib/inffast.c
    framework/zlib/inflate.c
    framework/zlib/inftrees.c
    framework/zlib/trees.c
    framework/zlib/uncompr.c
    framework/zlib/zutil.c
    renderer/jpeg-6/jcapimin.cpp
    renderer/jpeg-6/jccoefct.cpp
    renderer/jpeg-6/jccolor.cpp
    renderer/jpeg-6/jcdctmgr.cpp
    renderer/jpeg-6/jchuff.cpp
    renderer/jpeg-6/jcinit.cpp
    renderer/jpeg-6/jcmainct.cpp
    renderer/jpeg-6/jcmarker.cpp
    renderer/jpeg-6/jcmaster.cpp
    renderer/jpeg-6/jcomapi.cpp
    renderer/jpeg-6/jcparam.cpp
    renderer/jpeg-6/jcphuff.cpp
    renderer/jpeg-6/jcprepct.cpp
    renderer/jpeg-6/jcsample.cpp
    renderer/jpeg-6/jdapimin.cpp
    renderer/jpeg-6/jdapistd.cpp
    renderer/jpeg-6/jdatadst.cpp
    renderer/jpeg-6/jdatasrc.cpp
    renderer/jpeg-6/jdcoefct.cpp
    renderer/jpeg-6/jdcolor.cpp
    renderer/jpeg-6/jddctmgr.cpp
    renderer/jpeg-6/jdhuff.cpp
    renderer/jpeg-6/jdinput.cpp
    renderer/jpeg-6/jdmainct.cpp
    renderer/jpeg-6/jdmarker.cpp
    renderer/jpeg-6/jdmaster.cpp
    renderer/jpeg-6/jdmerge.cpp
    renderer/jpeg-6/jdphuff.cpp
    renderer/jpeg-6/jdpostct.cpp
    renderer/jpeg-6/jdsample.cpp
    renderer/jpeg-6/jdtrans.cpp
    renderer/jpeg-6/jerror.cpp
    renderer/jpeg-6/jfdctflt.cpp
    renderer/jpeg-6/jfdctfst.cpp
    renderer/jpeg-6/jfdctint.cpp
    renderer/jpeg-6/jidctflt.cpp
    renderer/jpeg-6/jidctfst.cpp
    renderer/jpeg-6/jidctint.cpp
    renderer/jpeg-6/jidctred.cpp
    renderer/jpeg-6/jmemmgr.cpp
    renderer/jpeg-6/jmemnobs.cpp
    renderer/jpeg-6/jquant1.cpp
    renderer/jpeg-6/jquant2.cpp
    renderer/jpeg-6/jutils.cpp
)
target_include_directories(external PUBLIC
    ${CMAKE_SOURCE_DIR}/framework/zlib
    ${CMAKE_SOURCE_DIR}/renderer/jpeg-6
    ${CMAKE_SOURCE_DIR}
)
target_compile_definitions(external PRIVATE)

# --- Test executable (minimal engine test) ---
# Disabled by default - enable by setting BUILD_TEST=ON
if( BUILD_TEST )
add_executable(doom3bfg_test
    ${CMAKE_SOURCE_DIR}/test_main.cpp
    ${CMAKE_SOURCE_DIR}/test_stubs.cpp
    ${RENDERER_SOURCES}
    ${FRAMEWORK_SOURCES}
    ${SYS_SOURCES}
    ${SOUND_SOURCES}
    ${IDLIB_SOURCES}
)

target_include_directories(doom3bfg_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/idlib
    ${CMAKE_SOURCE_DIR}/idlib/sys
    ${CMAKE_SOURCE_DIR}/framework
    ${CMAKE_SOURCE_DIR}/renderer
    ${CMAKE_SOURCE_DIR}/renderer/OpenGL
    ${CMAKE_SOURCE_DIR}/renderer/DXT
    ${CMAKE_SOURCE_DIR}/renderer/Color
    ${CMAKE_SOURCE_DIR}/renderer/jpeg-6
    ${CMAKE_SOURCE_DIR}/renderer/backend
    ${CMAKE_SOURCE_DIR}/sys
    ${CMAKE_SOURCE_DIR}/sys/sdl
    ${CMAKE_SOURCE_DIR}/sound
    ${CMAKE_SOURCE_DIR}/sound/sdl
    ${CMAKE_BINARY_DIR}/gl
    ${SDL2_INCLUDE_DIRS}
)

target_compile_definitions(doom3bfg_test PRIVATE
    IDLIB_EXPORTS
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(doom3bfg_test
    ${PLATFORM_LIBS}
    idlib
    external
)

endif()

# --- Main executable (full game - requires d3xp) ---
# Disabled by default - enable by setting BUILD_FULL_GAME=ON
if( BUILD_FULL_GAME )
    add_executable(doom3bfg
        ${DOOMEXE_SOURCES}
        ${RENDERER_SOURCES}
        ${FRAMEWORK_SOURCES}
        ${SYS_SOURCES}
        ${SOUND_SOURCES}
        ${SWF_SOURCES}
        ${UI_SOURCES}
        ${GAME_SOURCES}
        ${IDLIB_SOURCES}
    )

    target_include_directories(doom3bfg PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/idlib
        ${CMAKE_SOURCE_DIR}/idlib/sys
        ${CMAKE_SOURCE_DIR}/framework
        ${CMAKE_SOURCE_DIR}/renderer
        ${CMAKE_SOURCE_DIR}/renderer/OpenGL
        ${CMAKE_SOURCE_DIR}/renderer/DXT
        ${CMAKE_SOURCE_DIR}/renderer/Color
        ${CMAKE_SOURCE_DIR}/renderer/jpeg-6
        ${CMAKE_SOURCE_DIR}/renderer/backend
        ${CMAKE_SOURCE_DIR}/sys
        ${CMAKE_SOURCE_DIR}/sys/sdl
        ${CMAKE_SOURCE_DIR}/sound
        ${CMAKE_SOURCE_DIR}/sound/sdl
        ${CMAKE_SOURCE_DIR}/d3xp
        ${CMAKE_SOURCE_DIR}/d3xp/gamesys
        ${CMAKE_SOURCE_DIR}/d3xp/ai
        ${CMAKE_SOURCE_DIR}/d3xp/anim
        ${CMAKE_SOURCE_DIR}/d3xp/script
        ${CMAKE_SOURCE_DIR}/d3xp/menus
        ${CMAKE_SOURCE_DIR}/d3xp/physics
        ${CMAKE_BINARY_DIR}/gl
        ${SDL2_INCLUDE_DIRS}
        ${FFMPEG_INCLUDE_DIRS}
    )

    target_compile_definitions(doom3bfg PRIVATE
        IDLIB_EXPORTS
        _CRT_SECURE_NO_WARNINGS
    )

    target_link_libraries(doom3bfg
        ${PLATFORM_LIBS}
        idlib
        external
    )
endif()

# Compiler flags for test executable
if( BUILD_TEST )
target_compile_options(doom3bfg_test PRIVATE
    -Wno-unused-parameter
    -Wno-deprecated-declarations
    -Wno-switch
    -Wno-parentheses
    -Wno-multichar
    -Wno-format
    -fpermissive
)

# Output directory
set_target_properties(doom3bfg_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    DEBUG_POSTFIX "_d"
)

if( BUILD_FULL_GAME )
    set_target_properties(doom3bfg PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        DEBUG_POSTFIX "_d"
    )

    # Install
    install(TARGETS doom3bfg RUNTIME DESTINATION bin)
endif()

endif()

# Custom target for full rebuild
add_custom_target(rebuild
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --clean-first
    COMMENT "Full rebuild"
)