[![](https://discordapp.com/api/guilds/410828272679518241/widget.png?style=banner2)](https://discord.gg/Mvk7W7gjE4)
[![](https://raidcore.gg/Resources/Images/Patreon.png)](https://www.patreon.com/bePatron?u=46163080)

![](https://img.shields.io/github/license/RaidcoreGG/MouseLookHandler?style=for-the-badge&labelColor=%23131519&color=%230F79AA)
![](https://img.shields.io/github/v/release/RaidcoreGG/MouseLookHandler?style=for-the-badge&labelColor=%23131519&color=%230F79AA)
![](https://img.shields.io/github/downloads/RaidcoreGG/MouseLookHandler/total?style=for-the-badge&labelColor=%23131519&color=%230F79AA)

# MouseLookHandler
Accessibility addon aiding with action cam and other little tweaks.

Please direct suggestions on the [Raidcore Discord](https://discord.gg/raidcore) server.

## Installation
Install via the [Nexus](https://raidcore.gg/Nexus) Addon Library or download the latest `MouseLookHandler.dll` from the [Releases](https://github.com/RaidcoreGG/MouseLookHandler/releases) and place in `<Guild Wars 2>/addons`

## Features
- Automatically enable action cam while moving.
- Automatically enable action cam while in combat.
- Automatically enable action cam while mounted.
- Reroute left-/right-click to another button while action cam is on. E.g. right-click to dodge.
- Reset cursor to center after action cam.
- Hold down a specific key to temporarily disable action cam.

## How it works
Using the Mumble API to keep track of whether the player is moving or mounted.
Hooking WndProc to intercept inputs and then sending different inputs.
