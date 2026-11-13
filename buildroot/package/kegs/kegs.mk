################################################################################
#
# kegs
#
################################################################################

# Built from this repository's own source tree (the directory above
# buildroot/), so every image contains exactly the checked-out KEGS.
KEGS_VERSION = local
KEGS_SITE = $(BR2_EXTERNAL_KEGS_PATH)/..
KEGS_SITE_METHOD = local
KEGS_LICENSE = GPL-3.0
KEGS_LICENSE_FILES = LICENSE
KEGS_DEPENDENCIES = sdl2
# Don't copy Buildroot output dirs, Mac app bundles or disk images
KEGS_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS = \
	--exclude buildroot --exclude output --exclude 'output-*' \
	--exclude sdcard --exclude kegs-files --exclude '*.img' \
	--exclude '*.app' --exclude '*.o' --exclude /sdlkegs \
	--exclude '*.gz' --exclude '*.dsk' \
	--exclude '*.2mg' --exclude '*.po' --exclude '*.woz'

define KEGS_BUILD_CMDS
	rm -f $(@D)/src/vars
	ln -s vars_linux_sdl $(@D)/src/vars
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/src \
		CC="$(TARGET_CC) $(TARGET_CFLAGS)" \
		SDL2_CONFIG="$(STAGING_DIR)/usr/bin/sdl2-config" \
		sdlkegs
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(@D)/kegs-splash \
		$(@D)/src/kegs-splash.c
endef

define KEGS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/sdlkegs $(TARGET_DIR)/usr/bin/sdlkegs
	$(INSTALL) -D -m 0755 $(@D)/src/kegs-net.sh $(TARGET_DIR)/usr/bin/kegs-net
	$(INSTALL) -D -m 0755 $(@D)/kegs-splash $(TARGET_DIR)/usr/bin/kegs-splash
endef

$(eval $(generic-package))
