# scutclient Windows

语言：[English](README.md) | 简体中文

SCUT Dr.com(X) 客户端的 Windows 原生移植版。

这个版本保留原项目的 Dr.com 报文构造和认证状态机，把 Linux/OpenWrt
网络层替换成 Windows API：

- Npcap：发送和接收 802.1X/EAPOL 以太网帧，也就是 `0x888e`
- Winsock：处理 UDP 心跳
- IP Helper API：选择网卡、获取 IPv4 地址和 MAC 地址

## 环境要求

运行环境：

- Windows 10/11
- 已安装 Npcap 运行时，并勾选 `WinPcap API-compatible Mode`
- 首次测试和安装开机/登录启动任务时需要管理员权限

构建环境：

- Visual Studio 2022 Build Tools 或 Visual Studio
- CMake
- Npcap SDK

x64 构建时，Npcap SDK 路径下必须有这些文件：

```text
C:\npcap-sdk\Include\pcap.h
C:\npcap-sdk\Lib\x64\wpcap.lib
C:\npcap-sdk\Lib\x64\Packet.lib
```

## 构建

在 Visual Studio Developer Command Prompt 里执行，或者在普通 cmd 中执行，
前提是 CMake 能找到 Visual Studio：

```bat
set NPCAP_SDK=C:\npcap-sdk
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DNPCAP_SDK=C:\npcap-sdk
cmake --build build --config Release
```

如果构建成功，输出最后会出现类似这一行：

```text
scutclient.vcxproj -> D:\...\scutclient-windows\build\Release\scutclient.exe
```

如果 CMake 提示生成器不匹配，说明 `build` 目录以前用过其他生成器。删除旧
构建目录后重新配置即可：

```bat
rmdir /s /q build
```

## 首次运行

用管理员权限打开 cmd。

先列出可用的 Npcap 网卡：

```bat
build\Release\scutclient.exe --list-ifaces
```

然后使用测试成功的网卡认证：

```bat
build\Release\scutclient.exe --iface "\Device\NPF_{GUID}" --username USER --password PASS
```

后续安装静默启动任务时，继续使用这个完整的 `--iface` 值。

## 登录时静默启动

项目提供了一个辅助脚本，可以创建 Windows 任务计划。任务会在用户登录时以
`SYSTEM` 身份运行，因此不会弹出控制台窗口。

用管理员权限打开 cmd，然后执行：

```bat
scripts\setup-startup-task.bat list
scripts\setup-startup-task.bat install --iface "\Device\NPF_{GUID}" --username USER --password PASS
```

脚本会自动查找这些位置的可执行文件：

```text
build\Release\scutclient.exe
build-vs2022-x64\Release\scutclient.exe
build-nmake-x64\scutclient.exe
```

如果你的 `scutclient.exe` 在其他位置，可以显式指定：

```bat
scripts\setup-startup-task.bat install --exe "D:\path\to\scutclient.exe" --iface "\Device\NPF_{GUID}" --username USER --password PASS
```

常用管理命令：

```bat
scripts\setup-startup-task.bat status
scripts\setup-startup-task.bat run
scripts\setup-startup-task.bat uninstall
```

默认任务名是 `scutclient`。如果想换一个任务名：

```bat
scripts\setup-startup-task.bat install --task-name scutclient-campus --iface "\Device\NPF_{GUID}" --username USER --password PASS
```

## 日志

手动运行时，日志会同时打印到控制台并写入 `%TEMP%`。

通过任务计划以 `SYSTEM` 身份运行时，日志通常在：

```text
C:\Windows\Temp\scutclient.log
```

## 安全说明

任务计划会保存命令行参数，包括密码。拥有管理员权限的人可以看到这些内容。

如果不能接受这一点，后续应该把程序改成从受保护的配置文件或 Windows
Credential Manager 中读取凭据。

## 常见问题

提示找不到 `wpcap.dll` 或 `Packet.dll`：

- 重新安装 Npcap 运行时。
- 确认安装时勾选了 `WinPcap API-compatible Mode`。

CMake 配置时报 `Npcap SDK was not found`：

- 需要安装或解压 Npcap SDK，不只是安装 Npcap 运行时。
- 检查 `C:\npcap-sdk\Include\pcap.h` 是否存在。
- 配置时传入 `-DNPCAP_SDK=C:\npcap-sdk`。

任务计划看起来没有启动：

- 执行 `scripts\setup-startup-task.bat status`。
- 查看 `C:\Windows\Temp\scutclient.log`。
- 执行 `scripts\setup-startup-task.bat run` 手动测试任务。

登录后没有网络响应：

- 等网卡就绪后重新登录或手动运行任务。
- 如果你的机器网络栈初始化较慢，可以保留登录触发；如果要无人登录自动认证，
  再手动改成带延迟的开机触发。
