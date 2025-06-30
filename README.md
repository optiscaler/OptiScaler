<div align="center">

![Logo](https://github.com/user-attachments/assets/c7dad5da-0b29-4710-8a57-b58e4e407abd)

</div>

<div align="center">
  <a href="https://discord.gg/2JDHx6kcXB"><img src="https://img.shields.io/badge/DLSS2FSR-blue?style=for-the-badge&logo=discord&logoColor=white&logoSize=auto&color=5865F2" alt="Discord invite"></a>
  <a href="https://github.com/optiscaler/OptiScaler/releases/latest"><img src="https://img.shields.io/badge/Download-Stable-green?style=for-the-badge&logo=github&logoSize=auto" alt="Stable release"></a>
  <a href="https://github.com/optiscaler/OptiScaler/releases/tag/nightly"><img src="https://img.shields.io/badge/Download-Nightly-purple?style=for-the-badge&logo=github&logoSize=auto" alt="Nightly release"></a>
  <a href="https://github.com/optiscaler/OptiScaler/wiki"><img src="https://img.shields.io/badge/Documentation-blue?style=for-the-badge&logo=gitbook&logoColor=white&logoSize=auto" alt="Wiki"></a>
  
</div>

## Table of Contents

**1.** [**About**](#about)  
**2.** [**How it works?**](#how-it-works)  
**3.** [**Supported APIs and Upscalers**](#which-apis-and-upscalers-are-supported)  
**4.** [**Installation**](#installation)  
**5.** [**Known Issues**](#known-issues)  
**6.** [**Compilation and Credits**](#compilation)  
**7.** [**Wiki**](https://github.com/optiscaler/OptiScaler/wiki)

## About

**OptiScaler** is a tool that lets you replace upscalers in games that ***already support DLSS2+ / FSR2+ / XeSS*** ($`^1`$), now also supports ***enabling frame generation*** in those same games (through ***Nukem's dlssg-to-fsr3*** or ***OptiFG***). It also offers extensive customization options for all users, including those with Nvidia GPUs using DLSS.

> [!TIP]
> _For example, if a game has DLSS only, OptiScaler can be used to replace DLSS with XeSS or FSR 3.1 (also works for FSR2-only games, like The Outer Worlds Spacer's Choice)._

**Key aspects of OptiScaler:**
- Enables usage of XeSS, FSR2, FSR3, **FSR4**$`^2`$ (_RDNA4 only_) and DLSS in upscaler-enabled games
- Allows users to fine-tune their upscaling experience with a wide range of tweaks and enhancements (RCAS & MAS, Output Scaling, DLSS Presets, Ratio & DRS Overrides etc.)
- Since v0.7.0+, added ***experimental DX12*** frame generation support with possible HUDfix solution ([**OptiFG**](#optifg-powered-by-fsr3-fg--hudfix-experimental-hud-ghosting-fix) by FSR3)
- Supports [**Fakenvapi**](#installation) integration - enables Reflex hooking and injecting _Anti-Lag 2_ (RDNA1+ only) or _LatencyFlex_ (LFX) - **_not bundled_**$`^3`$  
- Since v0.7.7, added support for **Nukem's** FSR FG mod [**dlssg-to-fsr3**](#installation), only supports games with ***native DLSS-FG*** - **_not bundled_**$`^3`$  
- For a detailed list of all features, check [Features](Features.md)


> [!IMPORTANT]
> _**Always check the [Wiki Compatibility list](https://github.com/optiscaler/OptiScaler/wiki) for known game issues and workarounds.**_  
> Also please check the  [***OptiScaler known issues***](#known-issues) at the end regarding **RTSS** compatibility.
> A separate [***FSR4 Compatibility list***](https://github.com/optiscaler/OptiScaler/wiki/FSR4-Compatibility-List) is available for community-sourced tested games.  
> ***[3]** For **not bundled** items, please check [Installation](#installation).*  

> [!NOTE]
> <details>
>  <summary><b>Expand for [1], [2] </b></summary>  
>  
> ***[1]** Regarding **XeSS**, since Unreal Engine plugin does not provide depth, replacing in-game XeSS breaks other upscalers (e.g. Redout 2 as a XeSS-only game), but you can still apply RCAS sharpening to XeSS to reduce blurry visuals (in short, if it's a UE game, in-game XeSS only works with XeSS in OptiScaler overlay).*
>
> *Regarding **FSR inputs**, FSR 3.1 is the first version with a fully standardised, forward-looking API and should be fully supported. Since FSR2 and FSR3 support custom interfaces, game support will depend on the developers' implementation. With Unreal Engine games, you might need [ini tweaks](https://github.com/optiscaler/OptiScaler/wiki/Unreal-Engine-Tweaks) for FSR inputs.*  
>
> ***[2]** Regarding **FSR4**, support added with recent Nightly builds. Please check [FSR4 Compatibility list](https://github.com/optiscaler/OptiScaler/wiki/FSR4-Compatibility-List) for known supported games and general info.*
> 
> </details>


## Official Discord Server: [DLSS2FSR](https://discord.gg/2JDHx6kcXB)

*This project is based on [PotatoOfDoom](https://github.com/PotatoOfDoom)'s excellent [CyberFSR2](https://github.com/PotatoOfDoom/CyberFSR2).*

## How it works?
OptiScaler implements the necessary API methods of DLSS2+ & NVAPI, XeSS and FSR2+ to act as a middleware. It intercepts upscaler calls from the game (_**Inputs**_) and redirects them to the chosen upscaling backend (_**Output**_), allowing games using one technology to use another of your choice. **Inputs -> OptiScaler -> Outputs**
> [!NOTE]
> Pressing **`Insert`** should open the OptiScaler **Overlay** in-game with all of the options (`ShortcutKey=` can be changed in the config file). Pressing **`Page Up`** shows the performance stats overlay in the top left, and can be cycled between different modes with **`Page Down`**.


![image](https://github.com/user-attachments/assets/e138c979-c5d9-499f-a89b-165bb7cfcb32)


## Which APIs and Upscalers are Supported?
Currently **OptiScaler** can be used with DirectX 11, DirectX 12 and Vulkan, but each API has different sets of supported upscalers.  
[**OptiFG**](#optifg-powered-by-fsr3-fg--hudfix-experimental-hud-ghosting-fix) currently **only supports DX12** and is explained in a separate paragraph.

#### For DirectX 12
- XeSS (Default)
- FSR 2.1.2, 2.2.1
- FSR 3.X (and FSR 2.3.X)
- FSR 4.0.X (via FSR3.X update, _RDNA4 only_)
- DLSS

#### For DirectX 11
- FSR 2.2.1 (Default, native DX11)
- FSR 3.1.2 (unofficial port to native DX11)
- DLSS (native DX11)
- XeSS 2.X (native DX11, _Intel ARC only_)
- XeSS, FSR 2.1.2, 2.2.1, FSR 3.X w/Dx12 (_via D3D11on12_)$`^1`$
- FSR 4.0.X (via FSR 3.X w/Dx12 update, _RDNA4 only_)

> [!NOTE]
> <details>
>  <summary><b>Expand for [1]</b></summary>
>
> _**[1]** These implementations use a background DirectX12 device to be able to use Dirext12-only upscalers. There is a 10-15% performance penalty for this method, but allows many more upscaler options. Also native DirectX11 implementation of FSR 2.2.1 is a backport from Unity renderer and has its own problems of which some were fixed by OptiScaler._
> </details>

#### For Vulkan
- FSR2 2.1.2 (Default), 2.2.1
- FSR3 3.1 (and FSR2 2.3.2)
- DLSS
- XeSS 2.x

#### OptiFG (powered by FSR3 FG) + HUDfix (experimental HUD ghosting fix) 
**OptiFG** was added with **v0.7** and is **only supported in DX12**. 
It's an **experimental** way of adding FSR3 FG to games without native Frame Generation, or can also be used as a last case scenario if the native FG is not working properly.

For more information on OptiFG and how to use it, please check the Wiki page - [OptiFG](https://github.com/optiscaler/OptiScaler/wiki/OptiFG).


## Installation
> [!CAUTION]
> _**Warning**: **Do not use this mod with online games.** It may trigger anti-cheat software and cause bans!_

> [!IMPORTANT]
> **For installation steps, please check the [**Wiki**](https://github.com/optiscaler/OptiScaler/wiki)**  


## Configuration
Please check [this](Config.md) document for configuration parameters and explanations. If your GPU is not an Nvidia one, check [GPU spoofing options](Spoofing.md) *(Will be updated)*

## Known Issues
If you can't open the in-game menu overlay:
1. Please check that you have enabled DLSS, XeSS or FSR from game options and are in-game, not inside game settings
2. If using legacy installation, please try opening menu while you are in-game (while 3D rendering is happening)
3. If you are using **RTSS** (MSI Afterburner, CapFrameX), please enable this setting in RTSS and/or try updating RTSS. **When using OptiFG please disable RTSS for best compatibility**.
 
 ![image](https://github.com/optiscaler/OptiScaler/assets/35529761/8afb24ac-662a-40ae-a97c-837369e03fc7)

Please check [this](Issues.md) document for the rest of the known issues and possible solutions for them.  
Also check the community [Wiki](https://github.com/optiscaler/OptiScaler/wiki) for possible game issues and HUDfix incompatible games.

## Compilation

### Requirements
* Visual Studio 2022

### Instructions
* Clone this repo with **all of its submodules**.
* Open the OptiScaler.sln with Visual Studio 2022.
* Build the project

## Thanks
* @PotatoOfDoom for CyberFSR2
* @Artur for DLSS Enabler and helping me implement NVNGX api correctly
* @LukeFZ & @Nukem for their great mods and sharing their knowledge 
* @FakeMichau for continous support, testing and feature creep
* @QM for continous testing efforts and helping me to reach games
* @TheRazerMD for continous testing and support
* @Cryio, @krispy, @krisshietala, @Lordubuntu, @scz, @Veeqo for their hard work on [compatibility matrix](https://docs.google.com/spreadsheets/d/1qsvM0uRW-RgAYsOVprDWK2sjCqHnd_1teYAx00_TwUY)
* And the whole DLSS2FSR community for all their support

## Credit
This project uses [FreeType](https://gitlab.freedesktop.org/freetype/freetype) licensed under the [FTL](https://gitlab.freedesktop.org/freetype/freetype/-/blob/master/docs/FTL.TXT)
