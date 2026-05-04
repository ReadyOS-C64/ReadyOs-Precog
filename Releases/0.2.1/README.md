# ReadyOS 0.2.1 Release Guide

ReadyOS PRECOG is an experimental REU-first environment for a modern Commodore
64 setup. Its long-term center of gravity is the new Commodore 64 Ultimate and
related Ultimate-family hardware, but it is intended to support a wide range of
C64 setups that have a reasonably large REU. This release line is organized as
multiple media variants so the same ReadyOS runtime can fit different real-world
C64 environments without pretending every machine, cartridge, loader, or
emulator mounts the same media.

- Public release line: `0.2.1`
- Current artifact build in this tree: `0.2.1`
- Main site: [readyos64.com](https://readyos64.com)
- Wiki / working knowledge base: [readyos.notion.site](https://readyos.notion.site)
- GitHub source and issues: [ReadyOS-C64/ReadyOs](https://github.com/ReadyOS-C64/ReadyOs)

If this folder is distributed as a GitHub release or a packaged download, this
README is the landing page for the whole release line. The profile folders next
to it are the actual ReadyOS SKUs for different disk, drive, and cartridge
constraints.

The current public `0.2.1` release is still comparatively generic rather than
being explicitly tailored to the new C64 Ultimate. The next release is expected
to push further in that Ultimate-first direction while still trying to stay
usable on other REU-capable C64 setups.

## What ReadyOS Is

ReadyOS is not trying to be a generic desktop shell squeezed into a C64. It is
an opinionated, keyboard-first environment built around the idea that a modern
C64 workflow should feel immediate once the machine is up:

- fast app switching instead of cold-starting every tool from BASIC
- suspend and resume across apps instead of constantly reloading state
- shared clipboard and common interaction patterns across tools
- full-screen, terminal-style apps that favor repeatable keyboard workflows
- REU-backed state so the machine can behave more like a ready workspace than a
  single-program-at-a-time disk menu

The current public release line is `0.2.1`. Full-content ReadyOS profiles
currently expose `16` launcher-visible apps, with the exact app mix depending
on the variant you choose.

## Why There Are Multiple Variants

The short answer is that C64 storage and loader realities are not uniform.

ReadyOS wants to run on:

- C64 Ultimate and Ultimate 64 setups
- VICE on modern desktops
- real C64 hardware with REU-capable cartridges or other REU-capable expansions
- THEC64 Mini and Maxi style workflows
- web C64 emulators and simplified loaders that may only mount a single `D64`

Those environments differ in four important ways:

1. Drive type support.
   Some setups are happy with `1571` or `1581` style media, while others are
   effectively limited to `1541` / `D64`.

2. Number of simultaneously mounted images.
   Some environments can keep two drives online all the time. Others can only
   mount one disk image at once, which forces ReadyOS into smaller curated
   subsets.

3. Cartridge support.
   Some setups, especially VICE and Ultimate-family hardware, can boot a
   cartridge image cleanly and still keep a companion disk mounted.

4. REU path and convenience model.
   ReadyOS is designed for an REU-capable path. VICE can emulate that cleanly,
   Ultimate-family hardware can provide it directly, and some other modern
   setups can approximate it well enough to be practical. But the storage SKU
   still has to match the drive and media constraints of the environment.

That is why this release line ships multiple folders instead of pretending one
image is universally correct. The runtime philosophy is shared. The packaging
changes to match the target.

## Quick Recommendation By Environment

- If you are on C64 Ultimate, Ultimate 64, or VICE and want the cartridge boot
  path with the fullest current preload behavior: start with `precog-easyflash`.
- If your setup prefers a full-content disk-only path and is comfortable with
  two mounted `1571` drives: use `precog-dual-d71`.
- If your setup prefers one full-content image on a `1581` / `D81` path: use
  `precog-d81`.
- If you only have `1541`-class compatibility but can mount two disks: use
  `precog-dual-d64`.
- If you can only mount one `D64` at a time, especially in simpler emulators,
  loaders, THEC64-style flows, or web environments: choose one of the solo
  `D64` subsets based on the app group you care about.

## Public Variant Matrix

This release line currently has `9` public variants.

| Folder | Media | Best Fit | Why It Exists | Boot |
| --- | --- | --- | --- | --- |
| `precog-easyflash` | `CRT` cartridge + companion `D64` on drive `8` | VICE, C64 Ultimate, and Ultimate 64 setups that can boot EasyFlash while keeping a disk mounted. | Full cartridge cold-boot path with REU preload and a normal disk-backed runtime on drive `8`. | reset into cartridge boot |
| `precog-dual-d71` | 2x `D71` on drives `8` and `9` | C64 Ultimate, Ultimate 64, or VICE setups that can keep two 1571-class drives mounted. | Default full-content profile for two 1571-class drives and the main local verification target. | `PREBOOT -> SETD71 -> BOOT` |
| `precog-d81` | 1x `D81` on drive `8` | C64 Ultimate, VICE, or other 1581-capable setups that prefer one full-content image. | Full-content single-disk profile for 1581 and D81 setups where the whole current app catalog fits on one image. | `PREBOOT -> BOOT` |
| `precog-dual-d64` | 2x `D64` on drives `8` and `9` | Real or emulated 1541-only setups that can mount two disks but not D71 or D81 media. | Reduced dual-disk profile for 1541-class environments that can mount two D64 images but not higher-capacity media. | `PREBOOT -> BOOT` |
| `precog-solo-d64-a` | 1x `D64` on drive `8` | THEC64, web emulators, or simple loaders that can mount only one D64 at a time. | Single-D64 subset focused on editor, reference, and dizzy for one-disk-only environments. | `PREBOOT -> BOOT` |
| `precog-solo-d64-b` | 1x `D64` on drive `8` | THEC64, web emulators, or simple loaders that can mount only one D64 at a time. | Single-D64 productivity subset centered on quicknotes, clipboard, calculator, and files. | `PREBOOT -> BOOT` |
| `precog-solo-d64-c` | 1x `D64` on drive `8` | THEC64, web emulators, or simple loaders that can mount only one D64 at a time. | Single-D64 planning subset centered on tasklist, calendar, and REU viewer. | `PREBOOT -> BOOT` |
| `precog-solo-d64-d` | 1x `D64` on drive `8` | THEC64, web emulators, or simple loaders that can mount only one D64 at a time. | Single-D64 experimental subset for simple cells, calculator, 2048, and deminer. | `PREBOOT -> BOOT` |
| `precog-solo-d64-e` | 1x `D64` on drive `8` | THEC64, web emulators, or simple loaders that can mount only one D64 at a time. | Single-D64 shell-focused subset for readyshell and its overlay payloads in one-disk-only environments. | `PREBOOT -> BOOT` |

## What The Release Root Contains

The release line is centered on these public folders:

- `precog-easyflash/`
- `precog-dual-d71/`
- `precog-d81/`
- `precog-dual-d64/`
- `precog-solo-d64-a/`
- `precog-solo-d64-b/`
- `precog-solo-d64-c/`
- `precog-solo-d64-d/`
- `precog-solo-d64-e/`

Depending on whether a local workflow built one profile or all profiles, a
working tree may not contain every folder until the full multi-profile build
has been run. The intended GitHub release layout is this shared root README
plus the variant folders that carry the actual images, boot PRGs, `manifest.json`,
and per-variant `help.md` / `helpme.md`.

## The Cartridge SKU

`precog-easyflash` is the cartridge-oriented member of the same release family.

- The cartridge contains the EasyFlash boot code plus the launcher, app
  payloads, and ReadyShell overlays used during cold boot preload.
- The companion `readyos_data.d64` stays on drive `8` and provides the normal
  disk-backed runtime target for docs, user files, and app data.
- The on-screen boot label now reads `precog cartridge (beta)`, even though the
  release folder and artifact names still use `precog-easyflash`.
- Cold boot is longer than the disk variants because the loader prebuilds the
  launcher, app, and overlay REU snapshots up front.
- The boot loader now checks for REU very early. If REU is missing, it shows an
  explicit error, waits for a keypress, and then returns to BASIC cold start.
- The border colors now act as a progress signal during that preload.
- light blue: loader control flow
- green: shim setup
- yellow: cartridge-to-RAM copy
- orange: RAM-to-REU stash or REU restore
- light green: final launcher handoff
- red: REU missing, waiting for keypress to return to BASIC

If you want the closest thing to a console-like ReadyOS boot path on VICE or
Ultimate-family hardware, this is the SKU to start with.

## How To Think About The Variants

The variants are not random repacks. They are different answers to the same
question: "What is the best ReadyOS shape for this storage environment?"

- `precog-easyflash` is the cartridge-plus-disk option for setups that can boot
  an EasyFlash image and still keep drive `8` online.
- `precog-dual-d71` is the broadest disk-only "mainline" profile when two
  `1571`-class drives are available. It remains the primary local verification
  target.
- `precog-d81` is the cleanest single-image full-content option when `1581`
  support is available.
- `precog-dual-d64` exists because many C64-adjacent environments still top out
  at `D64`, but can at least keep two images mounted.
- The solo `D64` variants exist for the environments that cannot do more than
  one `D64` at a time. Instead of forcing a bloated or broken one-disk build,
  ReadyOS splits into intentional subsets.

That last category matters more than it may seem. A web emulator that only
mounts one `D64` is a very different target from VICE on a desktop with REU and
multiple virtual drives. THEC64 Mini / Maxi style workflows can also be more
pleasant with smaller, direct, single-image choices. A release that pretends all
of those paths are identical would be harder to understand and harder to boot.

## REU Expectation

ReadyOS is still an REU-first environment. The media variants solve storage
shape, not the absence of an REU-capable path.

Recommended baseline:

- enable the REU
- use `16MB` where the environment supports it
- treat VICE and Ultimate-family hardware as the smoothest targets today

On real C64 hardware, the exact cartridge or expansion path can vary. The main
question is not the brand of REU-capable device, but whether the setup can
deliver the REU behavior the runtime expects and whether the chosen media SKU
matches the drive or cartridge constraints of that setup.

## Debug Variants

This release line also ships debug-trace variants for ReadyShell development:
`precog-d81-rsdebug`, `precog-dual-d71-rsdebug`, `precog-solo-d64-e-rsdebug`.
They are intended for debugging and instrumentation, not as the default end-user
choice.

If you are just trying to run ReadyOS, prefer the non-debug variants first.

## Where To Go Next

- Start with the variant folder that matches your environment.
- Read that folder's `helpme.md` for exact boot and setup details.
- Use [readyos64.com](https://readyos64.com) as the public front door.
- Use [readyos.notion.site](https://readyos.notion.site) for the more wiki-like
  working docs.
- Use [GitHub](https://github.com/ReadyOS-C64/ReadyOs) for source, issues, and
  future packaged releases.

ReadyOS `0.2.1` is still explicitly experimental, but the purpose of this
release layout is simple: make it easier to pick the right image for the
hardware or emulator in front of you, instead of assuming every C64 environment
looks the same.
