THEOS_PACKAGE_SCHEME = rootless
TARGET = iphone:clang:latest:16.1
ARCHS = arm64 arm64e
include $(THEOS)/makefiles/common.mk
LIBRARY_NAME = IRBSpringBoard
IRBSpringBoard_FILES = Bridge.m ../capture/IRBCapture.m ../encoder/IRBEncoder.m ../input/IRBInput.m ../common/IRBLocalPeer.m
IRBSpringBoard_FRAMEWORKS = UIKit Foundation CoreGraphics QuartzCore CoreVideo CoreMedia VideoToolbox IOSurface CoreImage
IRBSpringBoard_CFLAGS = -fobjc-arc -Wall -Wextra -Wno-deprecated-declarations
IRBSpringBoard_INSTALL_PATH = /Library/MobileSubstrate/DynamicLibraries
include $(THEOS_MAKE_PATH)/library.mk
