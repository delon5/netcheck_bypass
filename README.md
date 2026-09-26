Vita netcheck Bypass
====================
taiHEN plugin that keeps PS Vita games away from PSN sign-in.

It only acts inside retail games (title IDs starting with `PCS`). When such a
game asks the system for the PSN sign-in dialog (`sceNetCheckDialogInit` in PSN
or PSN-online mode) the plugin closes the game immediately: the game is stopped
and the login prompt is never shown. In every other process (SceShell, system
apps such as the PS Store or Settings, homebrew) the plugin installs no hooks
and does nothing.

Ad-hoc and PS3-connect dialogs are passed through untouched, so local wireless
play keeps working.

## Install

Copy `netcheck_bypass.suprx` to `ux0:tai/` and add it to `ux0:tai/config.txt`.
Because the plugin ignores every non-game process it is safe to load it
everywhere:

```
*ALL
ux0:tai/netcheck_bypass.suprx
```

You can also list it under individual title IDs instead if you only want
specific games covered. Reboot or reload taiHEN config afterwards.

## Build

Requires [VitaSDK](https://vitasdk.org/).

```
mkdir build && cd build
cmake ..
make
```

## History

Version 1.x of this plugin did the opposite job: it faked a successful sign-in
so that apps requiring PSN would start without it. That behaviour is gone; use
the 1.1 release if you need it.

## License

The code is public domain and can be used in any way you want.
