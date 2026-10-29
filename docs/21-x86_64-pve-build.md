# PVE x86_64 旁路由固件构建

## 已确认目标

- 虚拟化平台：Proxmox VE；用途：单网卡旁路由。
- BIOS：SeaBIOS（用户原消息写作 SealBIOS）。因此输出传统 BIOS/MBR 的 `generic-ext4-combined.img.gz`，不选 EFI 镜像。
- OpenWrt：官方稳定版 25.12.6；源码 commit 和 archive SHA256 见 `config/x86_64/source-lock.json`。
- OpenClash：官方 v0.48.002，按固定 commit 从官方源码构建并预装 LuCI 包与依赖。
- Mihomo/Clash 核心、订阅、认证凭据均不放入固件，首次启动后由用户在设备上安装核心并私下添加订阅。
- 包含 PVE VirtIO 网卡、块设备和 SCSI 驱动。默认 OpenWrt 网络配置保留；不写入具体 VM 网卡映射、局域网地址、上游网关或旁路由策略。
- 根分区镜像配置为 1024 MiB。导入 PVE 后可按需扩展 VM 虚拟磁盘；增大虚拟磁盘后还需确认分区和文件系统已扩展。

## 构建

工作流 `.github/workflows/x86_64-openwrt.yml` 在 `feature/x86_64-openclash` 分支相关输入变化或面向 `main` 的 PR 更新时运行。GitHub 要求 `workflow_dispatch` 工作流先存在于默认分支，因此本次首次构建通过 PR 触发；合并后才可从 Actions 页面手动触发。它在 GitHub Actions 中构建，不使用本地 SDK。

工作流固定 OpenWrt 25.12.6 源码、OpenClash v0.48.002、OpenWrt 25.12 packages/LuCI feed 提交，并校验 OpenWrt 源码 SHA256。产物校验会检查 BIOS MBR 签名、镜像类型、OpenClash 版本、VirtIO/代理依赖及未打包 Mihomo 核心，并生成 SHA256。

Actions 成功仅代表构建和产物结构检查通过，不代表已在 PVE 启动或验证旁路由网络。首次引导应使用隔离的 PVE bridge/控制台检查；接入实际 LAN 前，先确认 LAN 地址唯一并关闭不需要的 DHCP 服务，避免与主网冲突。不要把生产订阅、账号或密钥写入构建配置。
