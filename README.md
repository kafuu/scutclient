# scutclient Windows

scutclient Windows 是 SCUT Dr.com(X) 客户端的 Windows 原生版本，用于在Windows 上完成校园网 802.1X/Dr.com 认证。

---

## 快速开始

本程序依赖 Npcap。运行前请先安装 Npcap 运行时，并在安装时勾选：

```text
WinPcap API-compatible Mode
```

Npcap 下载页：

```text
https://npcap.com/#download
```

Release 包应包含：

```text
scutclient.exe
scripts\scutclient-manager.bat
scripts\scutclient-manager.ps1
README.zh-CN.md
README.md
COPYING
```

### 设置Windows登录时静默启动认证程序

管理员 cmd 中执行：

```bat
scripts\scutclient-manager.bat install
```

也可以右键以管理员身份运行 `scripts\scutclient-manager.bat`。

脚本会自动列出可用网卡，按提示**输入网卡序号**，并按提示**输入校园网账号和密码**。

完成后，脚本会自动注册Windows计划任务，使认证程序在当前用户登录时自动静默启动。程序会在后台运行，不会弹出控制台窗口。下次Windows登录后，认证程序会自动启动并尝试完成校园网认证。

如需检查任务是否已经注册成功，可以运行：

```bat
scripts\scutclient-manager.bat status
```

常用管理命令：

```bat
scripts\scutclient-manager.bat list
scripts\scutclient-manager.bat status
scripts\scutclient-manager.bat run
scripts\scutclient-manager.bat stop
scripts\scutclient-manager.bat uninstall
```

### 常见问题

提示找不到 `wpcap.dll` 或 `Packet.dll`：

- 重新安装 Npcap 运行时。
- 确认安装时勾选了 `WinPcap API-compatible Mode`。

---
## 从源码构建

构建环境：

- Visual Studio 2022 Build Tools 或 Visual Studio
- CMake
- Npcap SDK

x64 构建时，Npcap SDK 路径下必须存在：

```text
C:\npcap-sdk\Include\pcap.h
C:\npcap-sdk\Lib\x64\wpcap.lib
C:\npcap-sdk\Lib\x64\Packet.lib
```

构建命令：

```bat
set NPCAP_SDK=C:\npcap-sdk
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DNPCAP_SDK=C:\npcap-sdk
cmake --build build --config Release
```

生成的可执行文件位于：

```text
build\Release\scutclient.exe
```

---
## 致谢与许可证

本项目基于 scutclient 修改而来：

- 原项目：https://github.com/scutclient/scutclient
- 原作者：Scutclient Project
- 原许可证：GNU Affero General Public License v3.0

本分支将原项目的 Linux/OpenWrt 网络层替换为 Windows/Npcap/Winsock 实现，
并增加了 Windows 构建说明和登录时静默启动脚本。

本项目继续以 GNU AGPLv3 发布。发布二进制文件时，也应同时提供对应源码，并在 Release 包中保留 `COPYING` 许可证文本。
