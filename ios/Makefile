THEOS_PACKAGE_SCHEME = rootless
TARGET = iphone:clang:latest:16.1
ARCHS = arm64
include $(THEOS)/makefiles/common.mk
SUBPROJECTS = daemon agent
include $(THEOS_MAKE_PATH)/aggregate.mk

# Optional Python client, shared by desktop OpenCode and Python 3.9 on iOS.
# A fixed source inventory excludes tests, caches and private pairing files.
IRB_AUTOMATION_FILES = __init__.py __main__.py client.py protocol.py mcp_tools.py mcp_stdio.py mcp_server.py
after-stage::
	mkdir -p "$(THEOS_STAGING_DIR)$(THEOS_PACKAGE_INSTALL_PREFIX)/usr/lib/iosremotebridge/automation"
	for file in $(IRB_AUTOMATION_FILES); do install -m 0644 "$(THEOS_PROJECT_DIR)/../automation/$$file" "$(THEOS_STAGING_DIR)$(THEOS_PACKAGE_INSTALL_PREFIX)/usr/lib/iosremotebridge/automation/"; done
