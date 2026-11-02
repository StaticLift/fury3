# Installing Fury³ on the PS Vita

## Quick start

1. Turn on **Enable Unsafe Homebrew** in HENkaku Settings.
2. Install `fury3.vpk` with VitaShell.
3. Copy your Fury³ disc image to this folder, creating it if needed:

   ```
   ux0:data/fury3/
   ```

   - **`.iso`:** copy the `.iso`.
   - **`.bin` and `.cue`:** copy them both. The game only reads the `.bin`.

   Unzip the image on your PC first, because `.zip`, `.7z` and `.rar` files won't work.

4. Launch Fury³. It sets itself up the first time, with a progress bar, then you're ready to play.

That's it. You can delete the disc image once the game has started.

For the faster GPU renderer, also run the [ShaRKBR33D](https://www.rinnegatamante.eu/vitadb/#/) homebrew once. Without it, the game still works but runs slower.

## What you need

- A homebrew-enabled Vita (HENkaku or Ensō) with VitaShell. Tested on a Vita 1000; PS TV is untested.
- `fury3.vpk`, from [the releases page](https://github.com/StaticLift/fury3/releases).
- A disc image of your own *Fury³* for Windows CD (1995). The VPK doesn't contain the game's files. Demos and other versions won't work.
- About 110 MB free on the memory card, plus room for the disc image during setup.

Cutscenes are on the disc and come across automatically.

## If something goes wrong

| On-screen message | What to do |
|---|---|
| Fury3 game files not found | Check the image is directly in `ux0:data/fury3/`, not in a subfolder, and isn't zipped |
| Could not copy the game files out of ... | The memory card is probably full. Free up space and try again |
| FURY3.EXE does not match, or a file size error | This is a different version of the game, which the port can't run |
| Missing `SYSTEM/...` | Delete `ux0:data/fury3/FURY3/` and let the game set up again |

**The game runs slowly:** run ShaRKBR33D once, which installs `libshacccg.suprx`. `ux0:data/fury3/gpu_log.txt` shows which renderer ran.

## Controls

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

## Advanced

### No disc image?

You can copy the `FURY3` folder from your CD instead, so that this file exists:

```
ux0:data/fury3/FURY3/FURY3.EXE
```

### Which version works

The port is translated from one exact version of the game. It checks these files when it starts:

| File | Size (bytes) | Check |
|---|---|---|
| `FURY3.EXE` | 645,120 | SHA-1 `64C42DD37CEDF97CB9F7434C0386896F8F437A93` |
| `SYSTEM\FURY3.POD` | 44,453,636 | Exact size |
| `SYSTEM\STARTUP.POD` | 4,194,399 | Exact size |
| `SYSTEM\FURY3.INI` | 971 | Must exist |
| `SYSTEM\FURY3.INF` | 12,954 | Must exist |

### Mods

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
