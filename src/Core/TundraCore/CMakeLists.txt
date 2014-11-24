# Define target name and output directory
if (TUNDRACORE_SHARED)
    init_target(TundraCore OUTPUT ./)
else()
    init_target(TundraCore)
endif()

MocFolder()

# Define source files
file(GLOB CPP_FILES *.cpp)
file(GLOB H_FILES *.h)
file(GLOB MOC_FILES # TODO Might be easier to add *.h and then remove couple...
    Asset/AssetAPI.h Asset/IAsset.h Asset/IAssetTransfer.h Asset/IAssetUploadTransfer.h Asset/IAssetStorage.h
    Asset/AssetRefListener.h Asset/BinaryAsset.h Asset/AssetCache.h Asset/IAssetTypeFactory.h Asset/NullAssetFactory.h
    Asset/IAssetBundle.h Asset/IAssetBundleTypeFactory.h Asset/LocalAssetProvider.h Asset/LocalAssetStorage.h
    Audio/AudioAPI.h Audio/AudioAsset.h Audio/SoundChannel.h Audio/SoundSettings.h
    Console/ConsoleAPI.h Console/ConsoleWidget.h Console/ShellInputThread.h
    Framework/Framework.h Framework/Application.h Framework/FrameAPI.h Framework/ConsoleAPI.h
    Framework/DebugAPI.h Framework/ConfigAPI.h Framework/IRenderer.h Framework/IModule.h
    Framework/PluginAPI.h Framework/VersionInfo.h Framework/Profiler.h
    Input/InputAPI.h Input/InputContext.h Input/KeyEvent.h Input/KeyEventSignal.h Input/MouseEvent.h
    Input/GestureEvent.h Input/EC_InputMapper.h
    Scene/SceneAPI.h Scene/Scene.h Scene/Entity.h Scene/IComponent.h Scene/EntityAction.h
    Scene/AttributeChangeType.h Scene/ChangeRequest.h Scene/EC_*.h 
    Ui/UiAPI.h Ui/UiGraphicsView.h Ui/UiMainWindow.h Ui/UiProxyWidget.h Ui/QtUiAsset.h Ui/RedirectedPaintWidget.h
    Script/EC_Script.h Script/IScriptInstance.h Script/ScriptAsset.h
)

AddSourceFolder(Asset)
AddSourceFolder(Audio)
AddSourceFolder(Console)
AddSourceFolder(Framework)
AddSourceFolder(Input)
AddSourceFolder(Scene)
AddSourceFolder(Script)
AddSourceFolder(Ui)

set(SOURCE_FILES ${CPP_FILES} ${H_FILES})
QT4_WRAP_CPP(MOC_SRCS ${MOC_FILES})

UseTundraCore() # Include all subfolders within the project
use_core_modules(Math)
use_package(QJSON)

add_definitions(-DTUNDRACORE_EXPORTS)
# If the following are undefined, the default values in TundraVersionInfo.h are used.
add_definitions(-DTUNDRA_ORGANIZATION_NAME="Meshmoon")
add_definitions(-DTUNDRA_APPLICATION_NAME="Rocket")
add_definitions(-DTUNDRA_VERSION_STRING="2.5.4.1")
add_definitions(-DTUNDRA_VERSION_MAJOR=2)
add_definitions(-DTUNDRA_VERSION_MINOR=5)
add_definitions(-DTUNDRA_VERSION_MAJOR_PATCH=4)
add_definitions(-DTUNDRA_VERSION_MINOR_PATCH=1)
# TUNDRA_VERSION_POSTFIX can be used to append information to the version string f.ex.
# when doing automated nightly builds or release candidates.
#if (DEFINED TUNDRA_VERSION_POSTFIX)
    add_definitions(-DTUNDRA_VERSION_POSTFIX="${TUNDRA_VERSION_POSTFIX}")
#endif()

if (TUNDRACORE_SHARED)
    build_library(${TARGET_NAME} SHARED ${SOURCE_FILES} ${MOC_SRCS})
else()
    build_library(${TARGET_NAME} STATIC ${SOURCE_FILES} ${MOC_SRCS})
endif()

link_boost()
link_package(QT4)
link_package(QJSON)
link_package_knet()     # Scene
if (NOT ANDROID)
    link_package(OPENAL)    # Audio
    link_package_ogg()      # Audio
    link_package_vorbis()   # Audio
endif()
link_modules(Math)

# Add log library on Android
if (ANDROID)
    target_link_libraries (${TARGET_NAME} log)
elseif (UNIX)
    target_link_libraries (${TARGET_NAME} dl)
endif()

SetupCompileFlagsWithPCH()

final_target()

# Declare Windows core dependencies
setup_install_windows_deps ("qjson.dll" Release|RelWithDebInfo|Debug)
 