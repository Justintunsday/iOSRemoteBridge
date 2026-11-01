THEOS_PACKAGE_SCHEME = rootless
TARGET = iphone:clang:latest:16.1
ARCHS = arm64
include $(THEOS)/makefiles/common.mk
TOOL_NAME = iosremotebridged
iosremotebridged_FILES = main.m ../common/IRBLocalPeer.m ../input/IRBNativeHID.m
iosremotebridged_FRAMEWORKS = Foundation Network Security
iosremotebridged_CFLAGS = -fobjc-arc -Wall -Wextra -Wno-deprecated-declarations
iosremotebridged_CODESIGN_FLAGS = -Sentitlements.plist
iosremotebridged_INSTALL_PATH = /usr/bin
include $(THEOS_MAKE_PATH)/tool.mk
