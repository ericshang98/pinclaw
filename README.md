<p align="center">
  <img src="https://pinclaw.ai/logo.png" alt="Pinclaw" width="100" />
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

## What is Pinclaw?

Pinclaw is a complete personal AI agent system. Not just a mic. Not just an app. A full ecosystem — hardware, software, and cloud — purpose-built for the [OpenClaw](https://openclaw.ai) platform.

Clip it on. Talk. Your AI agent hears you, thinks, and acts.

```
You speak → Pinclaw Clip → iPhone (BLE) → OpenClaw Plugin → AI Agent
                                                                 ↓
You hear  ← iPhone ← ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─  AI Response
```

### Why Pinclaw?

- **Purpose-built for OpenClaw** — Every layer is optimized for agent interaction, not retrofitted
- **Your own AI** — Not a shared model. Your own agent instance, your own database, your own personality
- **Open source** — Hardware designs, firmware, plugin — all MIT licensed, all hackable
- **iPhone-native** — Works with your phone, not against it. No extra device to charge or carry
- **Device skills** — Your AI can access your calendar, reminders, and more through native iPhone APIs

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

## The Ecosystem

| Layer | Component | Description |
|-------|-----------|-------------|
| **Hardware** | [Pinclaw Clip](./hardware) | XIAO nRF52840 Sense — always-on mic, BLE streaming |
| **Firmware** | [Pinclaw Firmware](./firmware) | Zephyr RTOS — audio capture, BLE protocol, power management |
| **Plugin** | [@openclaw/pinclaw](./plugin) | OpenClaw channel — bridges iPhone to your AI agent |
| **iPhone App** | [App Store](https://apps.apple.com/app/pinclaw/id6744145735) | Speech recognition, device skills, agent interaction |
| **Cloud** | [pinclaw.ai](https://pinclaw.ai) | Managed OpenClaw instances for subscribers |

## Two Ways to Use

### Cloud Mode — Zero Setup

Buy the clip, download the app, subscribe. We run a dedicated OpenClaw instance for you — your own agent, your own database, managed by us.

### My OpenClaw Mode — Full Control

Run OpenClaw on your own machine. Install the plugin, connect via relay. Your AI, your rules, your hardware.

```bash
# Your machine, your agent
openclaw plugin add @openclaw/pinclaw
openclaw gateway --force
# iPhone connects through relay — works from anywhere
```

## Device Skills

Your iPhone registers native capabilities as tools your AI agent can use:

| Skill | What the AI Can Do |
|-------|--------------------|
| **Calendar** | Read events, create meetings, check availability |
| **Reminders** | Add tasks, mark complete, query lists |
| **Screenshot** | Capture and analyze what's on screen |
| **Context** | Battery, location, time — passive awareness |

Say "schedule lunch with Sarah tomorrow at noon" — your agent calls the Calendar API directly on your phone.

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

Each subdirectory is also available as a standalone repository:
- [`ericshang98/pinclaw-plugin`](https://github.com/ericshang98/pinclaw-plugin) — Install just the plugin
- [`ericshang98/pinclaw-firmware`](https://github.com/ericshang98/pinclaw-firmware) — Build the firmware
- [`ericshang98/pinclaw-hardware`](https://github.com/ericshang98/pinclaw-hardware) — 3D print the case

## Architecture

```
┌──────────────┐     BLE      ┌──────────────────────────────┐
│ Pinclaw Clip │ ──────────── │         iPhone App            │
│ nRF52840     │              │                                │
│ • Mic        │              │  • Apple STT + Deepgram        │
│ • BLE 5.0    │              │  • Device Skills (Calendar...) │
│ • Battery    │              │  • Context Awareness           │
└──────────────┘              └──────────────┬─────────────────┘
                                             │
                                     Unified WebSocket
                                             │
                              ┌──────────────▼──────────────┐
                              │      Pinclaw Cloud          │
                              │      (Relay Server)         │
                              └──────────────┬──────────────┘
                                             │
                              ┌──────────────▼──────────────┐
                              │    @openclaw/pinclaw         │
                              │    Plugin                    │
                              │                              │
                              │  • WebSocket Handler         │
                              │  • Device Manager            │
                              │  • AI Pipeline               │
                              │  • Cron Scheduling           │
                              │  • Server Tools              │
                              └──────────────┬──────────────┘
                                             │
                              ┌──────────────▼──────────────┐
                              │    OpenClaw Gateway          │
                              │    Your Personal AI Agent    │
                              └─────────────────────────────┘
```

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
| **Discord** | [Join community](https://discord.gg/628R3FbV) |
| **Twitter** | [@EricShang98](https://x.com/EricShang98) |
| **OpenClaw** | [openclaw.ai](https://openclaw.ai) |

## License

MIT License. See [LICENSE](LICENSE) for details.

---

<p align="center">
  <sub>Built for <a href="https://openclaw.ai">OpenClaw</a> — the open-source AI agent platform.</sub>
</p>
