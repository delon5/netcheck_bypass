Vita netcheck Bypass
====================
taiHEN plugin that keeps the PSN sign-in prompt out of PS Vita games.

It only acts inside retail games (title IDs starting with `PCS`). When such a
game asks the system for the PSN sign-in dialog (`sceNetCheckDialogInit` in PSN
or PSN-online mode) the dialog is never created. Instead the plugin answers the
game exactly as if you had pressed Cancel, so the prompt is never shown and the
game carries on the way it does after a manual Cancel. In every other process
(SceShell, system apps such as the PS Store or Settings, homebrew) the plugin
installs no hooks and does nothing.

Ad-hoc and PS3-connect dialogs are passed through untouched, so local wireless
play keeps working.

## Install

Copy `netcheck_bypass.suprx` to `ux0:tai/` and add it to your taiHEN config.
taiHEN reads `ux0:tai/config.txt` if that file exists, otherwise
`ur0:tai/config.txt`; edit whichever one your setup uses. Because the plugin
ignores every non-game process it is safe to load it everywhere:

```
*ALL
ux0:tai/netcheck_bypass.suprx
```

You can also list it under individual title IDs instead if you only want
specific games covered. Reboot afterwards.

## Log

The plugin appends a few lines per launch to `ux0:data/netcheck_bypass.log`:
the title ID it saw, whether it treated the process as a game, which modules
it hooked, and every sign-in request it cancelled. If a game still shows the
prompt, that file says why.

## Build

Requires [VitaSDK](https://vitasdk.org/).

```
mkdir build && cd build
cmake ..
make
```

## History

Version 1.x faked a *successful* sign-in so that apps requiring PSN would start
without it, and ran in whatever process it was listed under. Version 2.x only
runs in games and answers Cancel instead.

## License

The code is public domain and can be used in any way you want.
