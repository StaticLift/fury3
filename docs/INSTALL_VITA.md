# Installing Fury³ on the PS Vita

## What you need

**Required**

| What | Details |
|---|---|
| A homebrew-enabled Vita | With HENkaku or Ensō and [VitaShell](https://www.rinnegatamante.eu/vitadb/#/). Tested on a Vita 1000; PS TV is untested. |
| Unsafe Homebrew turned on | In HENkaku Settings, turn on **Enable Unsafe Homebrew**. |
| `fury3.vpk` | From [the releases page](https://github.com/StaticLift/fury3/releases). |
| Your own copy of *Fury³* | The 1995 Microsoft Windows CD. The VPK doesn't contain the game's files. Demos and other versions won't work: see below. |
| About 110 MB free on the memory card | Plus room for your disc image while the game sets up, if you use one. |

**Recommended**

| What | Details |
|---|---|
| `libshacccg.suprx` | Needed for the GPU renderer, which is much faster. The [ShaRKBR33D](https://www.rinnegatamante.eu/vitadb/#/) homebrew installs it for you, and many Vita ports use it. It goes in `ur0:data/`. Without it, the game still runs on its slower software renderer. |
| The 19 cutscene videos | Files in `SYSTEM\STORY\`, 62.5 MB in total. They're on the CD and come across automatically. The game runs without them. |

## 1. Install the app

1. Turn on **Enable Unsafe Homebrew** in HENkaku Settings.
2. Install `fury3.vpk` with VitaShell.
3. Optional but recommended: run ShaRKBR33D once to install `libshacccg.suprx`.

## 2. Add your game files

Create the folder `ux0:data/fury3/` if it isn't there, then use **one** of these options. Disc image is the easiest.

### Option A: a disc image

Put the image directly in `ux0:data/fury3/`. On first launch the game copies its files out, with a progress bar. No PC tools are needed.

- `.iso` works.
- For a `.bin`/`.cue` rip, copy only the `.bin`. If your rip has several `.bin` files, use the first track, the data track.
- **A `.zip`, `.7z` or `.rar` won't work.** Unzip it on your PC first.
- The image must be directly in `ux0:data/fury3/`, not in a subfolder.

```
ux0:data/fury3/
└── Your Fury3 disc.iso
```

You can delete the image once the game has started successfully.

### Option B: the FURY3 folder

Copy the `FURY3` folder from your CD so that `FURY3.EXE` sits one level down, like this:

```
ux0:data/fury3/
└── FURY3/
    ├── FURY3.EXE
    └── SYSTEM/
        ├── FURY3.POD
        ├── STARTUP.POD
        ├── FURY3.INI
        ├── FURY3.INF
        └── STORY/        (the cutscenes, optional)
```

Watch out for a doubled folder, such as `FURY3/FURY3/FURY3.EXE`.

## Check your copy

The port is translated from one exact version of the game, so other versions can't run. The game checks these files when it starts:

| File | Size (bytes) | Check |
|---|---|---|
| `FURY3.EXE` | 645,120 | SHA-1 must be `64C42DD37CEDF97CB9F7434C0386896F8F437A93` |
| `SYSTEM\FURY3.POD` | 44,453,636 | Exact size |
| `SYSTEM\STARTUP.POD` | 4,194,399 | Exact size |
| `SYSTEM\FURY3.INI` | 971 | Must exist |
| `SYSTEM\FURY3.INF` | 12,954 | Must exist |

## If something goes wrong

| On-screen message | What it means | What to do |
|---|---|---|
| Fury3 game files not found | No game folder, and no disc image with a `FURY3` folder on it | Check the file is directly in `ux0:data/fury3/`, isn't zipped, and is over 50 MB |
| Missing `SYSTEM/...` | The folder is incomplete | Copy the whole `FURY3` folder again |
| ... is N bytes, expected N | That file isn't the CD version | Use the files from the Fury³ CD, not a rip or demo |
| FURY3.EXE does not match | A different version of the game | The port can't run this version |
| Could not copy the game files out of ... | The memory card is probably full | Free up space and try again |

**The game runs slowly:** check that `libshacccg.suprx` is installed. `ux0:data/fury3/gpu_log.txt` shows which renderer ran.

## 3. Play

On the title screen:

| Button | Action |
|---|---|
| Start | New game |
| Select | Continue |
| Square | Open the planet list; d-pad up and down picks a planet, left and right a mission, Start begins |

In flight:

| Control | Action |
|---|---|
| Sticks | Flight, roll and throttle |
| D-pad | Weapons and the cockpit display |
| Select | Camera view |
| Start | Pause |
| Square, while paused | Save |

Starting from the planet list doesn't grant the weapons you would have earned on earlier planets.

## Mods (optional)

Mods aren't included. To add one:

1. Put its `.pod` file in `ux0:data/fury3/FURY3/mods/`.
2. Create `ux0:data/fury3/FURY3/pod.ini` listing every archive. The first line is how many follow, and the first archive holding a file wins, so mods go above the game's own two:

   ```
   3
   .\mods\mymod.pod
   .\system\startup.pod
   .\system\fury3.pod
   ```

3. Press **Triangle** on the title screen to turn mods on or off, then restart the game when prompted.

The game can only mount a limited number of archives, so keep this list short. `tools/mod/merge_pods.py` combines several mods into one, and `tools/mod/glb2fury.py` converts your own `.glb` or `.gltf` models.
