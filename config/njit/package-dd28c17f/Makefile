# Research build only: no init script, credentials or router installation.
include $(TOPDIR)/rules.mk

PKG_NAME:=njit8021xclient
PKG_VERSION:=2018.11.24~dd28c17f
PKG_RELEASE:=2
PKG_SOURCE_VERSION:=dd28c17f24275bbbf4c44504b832c0f1e6b9ae40
PKG_SOURCE:=$(PKG_NAME)-$(PKG_SOURCE_VERSION).tar.gz
PKG_SOURCE_URL:=https://codeload.github.com/bitdust/njit8021xclient/tar.gz/$(PKG_SOURCE_VERSION)?
PKG_HASH:=e5a87e95803df0cf1e5e7662d68e4a29346f5f821098e06b0654c5bb41d39264
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_SOURCE_VERSION)
PKG_FIXUP:=autoreconf

include $(INCLUDE_DIR)/package.mk

define Package/njit8021xclient
  SECTION:=net
  CATEGORY:=Network
  TITLE:=NJIT 802.1X client research build
  URL:=https://github.com/bitdust/njit8021xclient
  DEPENDS:=+libpcap +libopenssl
endef

CONFIGURE_ARGS += --program-prefix=njit-
# Upstream detects libcrypto but omits its linker flags in client_LDADD.
MAKE_FLAGS += 'client_LDADD=$$(LBL_LIBS) $$(libcrypto_LIBS)'

define Package/njit8021xclient/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/src/client $(1)/usr/sbin/njit-client
endef

$(eval $(call BuildPackage,njit8021xclient))
