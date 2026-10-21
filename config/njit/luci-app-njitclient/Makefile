include $(TOPDIR)/rules.mk
PKG_NAME:=luci-app-njitclient
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=MIT
include $(INCLUDE_DIR)/package.mk

define Package/luci-app-njitclient
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=Managed NJIT wired authentication
  DEPENDS:=+luci-base +njit8021xclient
  PKGARCH:=all
endef

define Build/Compile
endef

define Package/luci-app-njitclient/install
	$(INSTALL_DIR) $(1)/www $(1)/usr/share
	$(CP) ./htdocs/* $(1)/www/
	$(CP) ./root/* $(1)/
endef

$(eval $(call BuildPackage,luci-app-njitclient))
