cmake_minimum_required(VERSION 3.24)
project(IRBController LANGUAGES C OBJC Swift)
set(CMAKE_XCODE_GENERATE_SCHEME ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0 CACHE STRING "Minimum controller iOS version")
file(GLOB CONTROLLER_SOURCES CONFIGURE_DEPENDS RemoteBridge/*.m RemoteBridge/*.h RemoteBridge/*.swift)
add_executable(iOSRemoteBridge MACOSX_BUNDLE ${CONTROLLER_SOURCES} Assets.xcassets)
set_source_files_properties(Assets.xcassets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_compile_options(iOSRemoteBridge PRIVATE
  "$<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc;-Wall;-Wextra;-Wno-deprecated-declarations>")
foreach(FRAMEWORK SwiftUI UIKit Foundation Network Security VideoToolbox CoreMedia CoreVideo CoreGraphics AVFoundation QuartzCore UniformTypeIdentifiers)
  find_library(${FRAMEWORK}_LIBRARY ${FRAMEWORK} REQUIRED)
  target_link_libraries(iOSRemoteBridge PRIVATE ${${FRAMEWORK}_LIBRARY})
endforeach()
set_target_properties(iOSRemoteBridge PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER io.github.justintunsday.iosremotebridge.controller
  XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION 3
  XCODE_ATTRIBUTE_SWIFT_VERSION 5.0
  XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/RemoteBridge/IRB-Bridging-Header.h"
  XCODE_ATTRIBUTE_SWIFT_OBJC_INTERFACE_HEADER_NAME iOSRemoteBridge-Swift.h
  XCODE_ATTRIBUTE_SWIFT_INSTALL_OBJC_HEADER YES
  XCODE_ATTRIBUTE_ALWAYS_EMBED_SWIFT_STANDARD_LIBRARIES YES
  XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "$(inherited) @executable_path/Frameworks /usr/lib/swift"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
  XCODE_ATTRIBUTE_ENABLE_USER_SCRIPT_SANDBOXING YES
  XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
)
