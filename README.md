<p align="center">
  <a href="https://pinclaw.ai">
    <img src="https://pinclaw.ai/logo.png" alt="Pinclaw" width="100" />
  </a>
</p>

<h1 align="center">Pinclaw</h1>

<p align="center">
  <strong>The first hardware product built for <a href="https://openclaw.ai">OpenClaw</a>.</strong><br/>
  A tiny wearable AI clip. Always listening, always acting — powered by your own AI agent.
</p>

<p align="center">
  <a href="https://pinclaw.ai">Website</a> ·
  <a href="https://apps.apple.com/app/pinclaw/id6744145735">App Store</a> ·
  <a href="https://pinclaw.ai/doc">Docs</a> ·
  <a href="https://discord.gg/628R3FbV">Discord</a> ·
  <a href="https://x.com/EricShang98">Twitter</a>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="MIT License" /></a>
  <a href="https://discord.gg/628R3FbV"><img src="https://img.shields.io/badge/discord-join-5865F2?logo=discord&logoColor=white" alt="Discord" /></a>
  <a href="https://x.com/EricShang98"><img src="https://img.shields.io/twitter/follow/EricShang98?style=social" alt="Twitter" /></a>
</p>

---

## Your Closest Agent.

We built everything — your own server, your own AI agent, ready to go. Clip it on, speak naturally, and your agent schedules, remembers, researches, and acts on your behalf.

**Hardware for OpenClaw.** Your agent already lives in the cloud. Pinclaw brings it to the real world — always on, always listening.

**Your phone, unlocked.** A dedicated app that opens up your entire phone ecosystem. Like owning a mobile OpenClaw.

<p align="center">
  <a href="https://pinclaw.ai">
    <img src="https://pub-6f7c1879412045ad8bf9eef70f06652d.r2.dev/pinclaw/gallery-1.webp" alt="Pinclaw wearable AI clip" width="600" />
  </a>
</p>

## 15X Faster Input

Language is the most natural interface. Pinclaw makes it always available.

| Method | Time |
|--------|------|
| Phone: unlock, open app, type | ~60s |
| **Pinclaw: tap, speak, accurately transcribed** | **~4s** |

*Estimated average time to send a 20-word request to an AI agent, from intent to delivery.*

## Works With Every Claw

Pinclaw integrates seamlessly with the entire OpenClaw ecosystem — cloud or self-hosted, plug in and go.

| Platform | Type |
|----------|------|
| **OpenClaw** | Self-hosted, open source |
| **KiloClaw** | Managed cloud |
| **Clawi.ai** | Managed cloud |
| **chowder.dev** | Managed cloud |
| **ClawApp** | Managed cloud |
| **EasyClaw** | Managed cloud |
| **HostedClaws** | Managed cloud |
| **ClawSimple** | Managed cloud |

One plugin. Any platform. Your choice.

## Device Skills

Other AI hardware lives in a bubble — it can't see your calendar, doesn't know your contacts, and has no idea what you did today. Pinclaw is different.

Enable Device Skills and your AI agent gains access to the data that already lives on your iPhone. Each skill requires your explicit permission.

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

All data stays on your iPhone. You control every permission.

## Quick Start

```bash
# Install the Pinclaw plugin for OpenClaw
openclaw plugin add @openclaw/pinclaw

# Start your agent
openclaw gateway --force

# Download the iOS app, connect, and go.
```

<a href="https://apps.apple.com/app/pinclaw/id6744145735">
  <img src="https://developer.apple.com/assets/elements/badges/download-on-the-app-store.svg" alt="Download on the App Store" height="40" />
</a>

For a complete setup guide, see the [documentation](https://pinclaw.ai/doc).

## Two Ways to Use

### Cloud Mode — Zero Setup

Buy the clip, download the app, subscribe. We run a dedicated OpenClaw instance for you — your own agent, your own database, managed by us.

### My OpenClaw Mode — Full Control

Run OpenClaw on your own machine. Install the plugin, connect via relay. Your AI, your rules, your hardware.

## Core Technologies

**BLE Audio Streaming** — High-quality audio streams wirelessly to your iPhone over Bluetooth Low Energy. Custom packet protocol with CRC32 integrity checks ensures every word is heard perfectly.

**On-Device Speech Recognition** — Your voice is transcribed locally on your iPhone using Apple Speech framework. No audio leaves your device. Fast, accurate, and completely private.

**AI Agent Services** — Connect to Claude, GPT, and other frontier AI models through the OpenClaw platform. Specialized agents for scheduling, research, translation, and more.

## Repository Structure

```
pinclaw/
├── plugin/          OpenClaw channel plugin (@openclaw/pinclaw)
│   ├── src/
│   │   ├── core/    Server, WS handler, AI pipeline, device manager
│   │   └── tools/   Server-side tool definitions (auto-discovered)
│   └── index.ts     Plugin entry point
│
├── firmware/        Zephyr firmware for XIAO nRF52840 Sense
│   ├── src/         Audio capture, BLE streaming, power management
│   └── boards/      Board configurations
│
├── hardware/        Hardware design files
│   ├── 3d/          Case and enclosure (Fusion 360 / STL)
│   └── pcb/         PCB designs
│
└── docs/            Documentation
```

Everything lives in this single repository. Clone it and you have the full ecosystem.

## Contributing

Pinclaw is fully open source. We welcome contributions to any part of the ecosystem.

1. Fork this repository
2. Create your feature branch (`git checkout -b feature/amazing`)
3. Commit your changes
4. Open a Pull Request

Join our [Discord](https://discord.gg/628R3FbV) to discuss ideas, report bugs, or just hang out.

## Links

| | |
|---|---|
| **Website** | [pinclaw.ai](https://pinclaw.ai) |
| **iOS App** | [App Store](https://apps.apple.com/app/pinclaw/id6744145735) |
| **Documentation** | [pinclaw.ai/doc](https://pinclaw.ai/doc) |
| **Buy Pinclaw** | [pinclaw.ai/#pricing](https://pinclaw.ai/#pricing) |
| **Discord** | [Join community](https://discord.gg/628R3FbV) |
| **Twitter** | [@EricShang98](https://x.com/EricShang98) |
| **OpenClaw** | [openclaw.ai](https://openclaw.ai) |

## License

MIT License. See [LICENSE](LICENSE) for details.

---

<p align="center">
  <sub>Built for <a href="https://openclaw.ai">OpenClaw</a> — the open-source AI agent platform.</sub>
</p>
