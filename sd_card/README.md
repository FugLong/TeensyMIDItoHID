# SD Card Files

Copy all files from this folder to the **root** of your SD card.

## Files to Copy

- `CONFIG.TXT` - Configuration file (optional, uses defaults if missing)
- Mapping files from `mappings/` folder - Copy the mapping file you want to use to the SD card root

## Mapping Files

Mapping files can be named anything you want, as long as:
- The filename contains the word **"MAPPINGS"** (case-insensitive)
- The file extension is **".txt"** or **".TXT"**

**Examples of valid mapping file names:**
- `MAPPINGS.txt`
- `MY_GAME_MAPPINGS.TXT`
- `WWM36_MAPPINGS.txt`
- `custom_mappings.txt`
- `GameMappings.TXT`

The system will automatically find and load the first matching file it encounters in the SD card root directory.

## Example SD Card Structure

```
SD Card Root:
├── CONFIG.TXT
└── MAPPINGS.txt  (or any filename containing "MAPPINGS" with .txt extension)
```

## Source Files

The `mappings/` folder contains source mapping files organized by game. Copy the one you want to use to the SD card root and rename it appropriately.
