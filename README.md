# scutclient Windows

Languages: English | [简体中文](README.zh-CN.md)

SCUT Dr.com(X) client native Windows port.

This fork keeps the original Dr.com packet construction and authentication
state machine, and replaces the Linux/OpenWrt network layer with Windows APIs:

- Npcap for 802.1X/EAPOL Ethernet frames (`0x888e`)
- Winsock for UDP heartbeats
- IP Helper API for adapter selection, IPv4 address, and MAC address

## Requirements

Runtime:

- Windows 10/11
- Npcap runtime installed with `WinPcap API-compatible Mode`
- Administrator privileges for first-run testing and task installation

Build:

- Visual Studio 2022 Build Tools or Visual Studio
- CMake
- Npcap SDK

The Npcap SDK path must contain these files for x64 builds:

```text
C:\npcap-sdk\Include\pcap.h
C:\npcap-sdk\Lib\x64\wpcap.lib
C:\npcap-sdk\Lib\x64\Packet.lib
```

## Build

From a Visual Studio Developer Command Prompt or a normal cmd where CMake can
find Visual Studio:

```bat
set NPCAP_SDK=C:\npcap-sdk
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DNPCAP_SDK=C:\npcap-sdk
cmake --build build --config Release
```

A successful build ends with a line like:

```text
scutclient.vcxproj -> D:\...\scutclient-windows\build\Release\scutclient.exe
```

If CMake reports that the generator does not match a previous run, delete the
old build directory or use a different directory:

```bat
rmdir /s /q build
```

## First Run

Open cmd as Administrator.

List usable Npcap adapters:

```bat
build\Release\scutclient.exe --list-ifaces
```

Authenticate with the adapter that worked in your environment:

```bat
build\Release\scutclient.exe --iface "\Device\NPF_{GUID}" --username USER --password PASS
```

Keep the exact `--iface` value for the startup script.

## Silent Startup

The helper script creates a Windows scheduled task that runs at user logon as
`SYSTEM`. This avoids a visible console window.

Run cmd as Administrator, then:

```bat
scripts\setup-startup-task.bat list
scripts\setup-startup-task.bat install --iface "\Device\NPF_{GUID}" --username USER --password PASS
```

The script automatically looks for:

```text
build\Release\scutclient.exe
build-vs2022-x64\Release\scutclient.exe
build-nmake-x64\scutclient.exe
```

If your executable is elsewhere, pass it explicitly:

```bat
scripts\setup-startup-task.bat install --exe "D:\path\to\scutclient.exe" --iface "\Device\NPF_{GUID}" --username USER --password PASS
```

Useful management commands:

```bat
scripts\setup-startup-task.bat status
scripts\setup-startup-task.bat run
scripts\setup-startup-task.bat uninstall
```

The default task name is `scutclient`. To use another name:

```bat
scripts\setup-startup-task.bat install --task-name scutclient-campus --iface "\Device\NPF_{GUID}" --username USER --password PASS
```

## Logs

When run manually, logs are printed to the console and written to `%TEMP%`.

When run by the scheduled task as `SYSTEM`, the log path is usually:

```text
C:\Windows\Temp\scutclient.log
```

## Security Notes

The scheduled task stores command-line arguments, including the password, in the
task definition. Anyone with administrative access can read it.

If this is not acceptable, the program should be extended later to read
credentials from a protected config file or Windows Credential Manager.

## Troubleshooting

`wpcap.dll` or `Packet.dll` not found:

- Reinstall Npcap runtime.
- Make sure `WinPcap API-compatible Mode` is selected.

`Npcap SDK was not found` during CMake configure:

- Install/extract the Npcap SDK, not only the runtime.
- Check that `C:\npcap-sdk\Include\pcap.h` exists.
- Pass `-DNPCAP_SDK=C:\npcap-sdk`.

The scheduled task does not appear to start:

- Run `scripts\setup-startup-task.bat status`.
- Check `C:\Windows\Temp\scutclient.log`.
- Test manually with `scripts\setup-startup-task.bat run`.

No network response on startup:

- Try logging in again after the adapter is ready.
- If the network stack on your machine initializes slowly, recreate the task as
  `ONSTART` with delay manually, or keep the default logon trigger.
