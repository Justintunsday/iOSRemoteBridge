cmake_minimum_required(VERSION 3.24)
project(IRBController LANGUAGES C OBJC)
set(CMAKE_XCODE_GENERATE_SCHEME ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0 CACHE STRING "Minimum controller iOS version")
file(GLOB CONTROLLER_SOURCES CONFIGURE_DEPENDS RemoteBridge/*.m RemoteBridge/*.h)
add_executable(iOSRemoteBridge MACOSX_BUNDLE ${CONTROLLER_SOURCES} Assets.xcassets)
set_source_files_properties(Assets.xcassets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_compile_options(iOSRemoteBridge PRIVATE -fobjc-arc -Wall -Wextra -Wno-deprecated-declarations)
foreach(FRAMEWORK UIKit Foundation Network Security VideoToolbox CoreMedia CoreVideo CoreGraphics AVFoundation QuartzCore UniformTypeIdentifiers)
  find_library(${FRAMEWORK}_LIBRARY ${FRAMEWORK} REQUIRED)
  target_link_libraries(iOSRemoteBridge PRIVATE ${${FRAMEWORK}_LIBRARY})
endforeach()
set_target_properties(iOSRemoteBridge PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER io.github.justintunsday.iosremotebridge.controller
  XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION 1
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
  XCODE_ATTRIBUTE_ENABLE_USER_SCRIPT_SANDBOXING YES
  XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
)
