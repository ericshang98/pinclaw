<div align="center">

<a href="https://pinclaw.ai">
  <img src="https://pinclaw.ai/logo.png" alt="Pinclaw" width="50" />
  <img src="./assets/pinclaw-title.svg" alt="PinClaw" height="40" />
</a>

[pinclaw.ai](https://pinclaw.ai)

The first hardware product built for [OpenClaw](https://openclaw.ai). A tiny wearable AI clip — always listening, always acting, powered by your own AI agent.

<p align="center">
  <img src="https://pub-6f7c1879412045ad8bf9eef70f06652d.r2.dev/pinclaw/gallery-1.webp" alt="Pinclaw" width="600" />
</p>

[![Discord](https://img.shields.io/badge/discord-join-5865F2?logo=discord&logoColor=white)](https://discord.gg/628R3FbV)&ensp;&ensp;&ensp;
[![Twitter Follow](https://img.shields.io/twitter/follow/EricShang98)](https://x.com/EricShang98)&ensp;&ensp;&ensp;
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)&ensp;&ensp;&ensp;
[![GitHub Repo stars](https://img.shields.io/github/stars/ericshang98/pinclaw)](https://github.com/ericshang98/pinclaw)

<h3>

[Website](https://pinclaw.ai) | [App Store](https://apps.apple.com/app/pinclaw/id6744145735) | [Docs](https://pinclaw.ai/doc) | [Buy Pinclaw](https://pinclaw.ai/#pricing)

</h3>

</div>

<table>
<tr>
<td>

## Your Closest Agent.

We built everything — your own server, your own AI agent, ready to go. Clip it on, speak naturally, and your agent schedules, remembers, researches, and acts on your behalf.

- **Hardware for OpenClaw.** Your agent already lives in the cloud. Pinclaw brings it to the real world — always on, always listening.
- **Your phone, unlocked.** A dedicated app that opens up your entire phone ecosystem. Like owning a mobile OpenClaw.

</td>
<td width="160" align="center">
<img src="https://pinclaw.ai/logos/openclaw.svg" alt="OpenClaw" width="50" />
<br/>
<b>+</b>
<br/>
<img src="https://pinclaw.ai/logo.png" alt="Pinclaw" width="50" />
</td>
</tr>
</table>

## ⚡ 15× Faster Input

Language is the most natural interface. Pinclaw makes it always available.

```
Phone: unlock → open app → type                          ~60s
Pinclaw: tap → speak → accurately transcribed              ~4s
```

*Estimated average time to send a 20-word request to an AI agent, from intent to delivery.*

## 🚀 Quick Start (30 sec)

```bash
# Install the Pinclaw plugin for OpenClaw
openclaw plugin add @openclaw/pinclaw

# Start your agent
openclaw gateway --force

# That's it. Download the iOS app, connect, and go.
```

[<img src="https://developer.apple.com/assets/elements/badges/download-on-the-app-store.svg" alt="Download on the App Store" height="50px" width="180px">](https://apps.apple.com/app/pinclaw/id6744145735)

For a complete setup guide, see the [documentation](https://pinclaw.ai/doc).

## 🌐 Works With Every Claw

Pinclaw integrates with the entire OpenClaw ecosystem — cloud or self-hosted, plug in and go.

| Platform | Type | Platform | Type |
|----------|------|----------|------|
| [**OpenClaw**](https://openclaw.ai) | Self-hosted | **ClawApp** | Managed |
| **KiloClaw** | Managed | **EasyClaw** | Managed |
| **Clawi.ai** | Managed | **HostedClaws** | Managed |
| **chowder.dev** | Managed | **ClawSimple** | Managed |

One plugin. Any platform. Your choice.

## 📱 Device Skills

Other AI hardware lives in a bubble — it can't see your calendar, doesn't know your contacts, and has no idea what you did today. Pinclaw is different.

<p align="center">
  <img src="https://pinclaw.ai/skills-screenshot.png" alt="Pinclaw Device Skills" width="280" />
</p>

| Skill | What Your Agent Can Do |
|-------|-----------------------|
| **Calendar** | View and create events, check availability |
| **Reminders** | Manage tasks and to-do lists |
| **Contacts** | Search your contacts |
| **Timer** | Set and cancel timers |
| **Health** | Read health data summaries |
| **Location** | Get your current location |
| **HomeKit** | Control your smart home devices |

> All data stays on your iPhone. You control every permission.

## 🔧 Two Ways to Use

**Cloud Mode — Zero Setup**
Buy the clip, download the app, subscribe. We run a dedicated OpenClaw instance for you — your own agent, your own database, managed by us.

**My OpenClaw Mode — Full Control**
Run OpenClaw on your own machine. Install the plugin, connect via relay. Your AI, your rules, your hardware.

## 🏗️ Core Technologies

| Technology | Description |
|-----------|-------------|
| **BLE Audio Streaming** | High-quality audio streams wirelessly over BLE. Custom packet protocol with CRC32 integrity checks. |
| **On-Device Speech Recognition** | Transcribed locally on iPhone using Apple Speech. No audio leaves your device. |
| **AI Agent Services** | Connect to Claude, GPT, and other frontier models through the OpenClaw platform. |

## In this repo:

- [plugin](plugin) — OpenClaw channel plugin (`@openclaw/pinclaw`), TypeScript
- [firmware](firmware) — Zephyr RTOS firmware for XIAO nRF52840 Sense, C
- [hardware](hardware) — 3D case designs (Fusion 360 / STL), PCB files

## Documentation:

- [Introduction](https://pinclaw.ai/doc)
- [Getting Started](https://pinclaw.ai/doc?tab=getting-started)
- [Core Protocol](https://pinclaw.ai/doc?tab=core)
- [Plugin Development](https://pinclaw.ai/doc?tab=plugin)
- [API Reference](https://pinclaw.ai/doc?tab=reference)

## Contributions

- Check out the [current issues](https://github.com/ericshang98/pinclaw/issues).
- Join the [Discord](https://discord.gg/628R3FbV).
- Fork, branch, and open a Pull Request.

## Licensing

Pinclaw is available under <a href="https://github.com/ericshang98/pinclaw/blob/main/LICENSE">MIT License</a>
