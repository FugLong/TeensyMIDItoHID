# Mapping Files

This folder contains mapping files organized by game. Mapping files define how MIDI notes are translated to keyboard keys.

## Structure

```
mappings/
└── games/
    └── where_winds_meet/
        ├── WWM21_MAPPINGS.txt  (21-key mode)
        └── WWM36_MAPPINGS.txt  (36-key mode)
```

## Usage

To use a mapping file:

1. Copy the mapping file you want from this folder to the **root** of your SD card
2. Rename it to any name containing "MAPPINGS" with a `.txt` extension
   - Examples: `MAPPINGS.txt`, `MY_GAME_MAPPINGS.TXT`, `custom_mappings.txt`
   - The filename must contain the word "MAPPINGS" (case-insensitive) and end with `.txt` or `.TXT`

The code will automatically find and load the first matching file it encounters in the SD card root directory.

## Creating Your Own Mapping File

### Basic Format

Mapping files use a simple format: `MIDI_NOTE=KEY_NAME`

```
# Comments start with #
60=H      # MIDI note 60 (Middle C) -> H key
62=J      # MIDI note 62 (D4) -> J key
64=K      # MIDI note 64 (E4) -> K key
```

### Supported Key Names

**Letters:** `A` through `Z` (case-insensitive)

**Numbers:** `0` through `9`

**Special Keys:**
- `SPACE` or `SPC` - Spacebar
- `ENTER` or `RETURN` - Enter key
- `ESC` or `ESCAPE` - Escape key
- `TAB` - Tab key
- `BACKSPACE` or `BS` - Backspace

**Modifiers (for combinations):**
- `SHIFT` - Left Shift
- `CTRL` or `CONTROL` - Left Ctrl
- `ALT` - Left Alt
- `META` or `WIN` or `CMD` - Windows/Command key

**Modifier Format:**
- `SHIFT+KEY` or `KEY+SHIFT` - Both formats work
- Example: `SHIFT+A` or `A+SHIFT` both map to Shift+A

### Examples

**Simple mapping (no modifiers):**
```
# Basic game mapping
48=Z      # C3 -> Z
50=X      # D3 -> X
52=C      # E3 -> C
60=A      # C4 -> A
62=S      # D4 -> S
64=D      # E4 -> D
```

**With modifiers (for sharps/flats):**
```
# Natural notes use normal keys, accidentals use modifiers
60=A          # C4 -> A
61=SHIFT+A    # C#4 -> Shift+A
62=S          # D4 -> S
63=CTRL+D     # D#4 -> Ctrl+D
64=D          # E4 -> D
```

**Complete example (chromatic scale):**
```
# C Major scale with sharps
48=Z          # C3
49=SHIFT+Z    # C#3
50=X          # D3
51=CTRL+C     # D#3
52=C          # E3
53=V          # F3
54=SHIFT+V    # F#3
55=B          # G3
56=SHIFT+B    # G#3
57=N          # A3
58=CTRL+M     # A#3
59=M          # B3
```

### MIDI Note Reference

Standard MIDI note numbers:
- **C4 (Middle C)** = 60
- **C#4** = 61
- **D4** = 62
- **D#4** = 63
- **E4** = 64
- **F4** = 65
- **F#4** = 66
- **G4** = 67
- **G#4** = 68
- **A4** = 69
- **A#4** = 70
- **B4** = 71
- **C5** = 72

Each octave adds 12 semitones. Full range: 0-127

### Tips

1. **Use comments** - Document your mappings with `#` comments
2. **Test incrementally** - Start with a few notes, test, then add more
3. **Consider game layout** - Map notes to keys that make sense for your game's controls
4. **Modifiers for accidentals** - Use Shift/Ctrl for sharps/flats to keep natural notes on normal keys
5. **Case sensitivity** - File names may be case-sensitive on some SD cards, so use uppercase extensions (`.TXT`)

## Adding New Games

To add mappings for a new game:

1. Create a new folder under `games/` (e.g., `games/my_game/`)
2. Add your mapping file(s) there
3. Document the mapping layout in comments
4. Copy to SD card root when ready to use

## File Discovery

The code searches the SD card root directory for any `.txt` file whose filename contains "MAPPINGS" (case-insensitive). The first matching file found is loaded.

**Examples of valid filenames:**
- `MAPPINGS.txt`
- `MY_GAME_MAPPINGS.TXT`
- `WWM36_MAPPINGS.txt`
- `custom_mappings.txt`
- `GameMappings.TXT`

If no matching file is found, fallback test mappings are used (MIDI note 60 -> H, 58 -> G).
