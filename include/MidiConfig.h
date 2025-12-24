/*
 * MIDI Configuration Header
 * 
 * Defines key code constants for USB HID keyboard and configuration constants.
 */

#ifndef MIDI_CONFIG_H
#define MIDI_CONFIG_H

// Maximum number of MIDI notes (standard MIDI range)
#define MAX_MIDI_NOTES 128

// Maximum number of simultaneous keys (polyphony/chords)
// Limited by USB HID keyboard report size (6 keys + modifiers)
#define MAX_SIMULTANEOUS_KEYS 6

// Maximum number of profiles per mapping file
#define MAX_PROFILES 8

// MIDI note for profile switching (default: C1 = note 24, configurable via CONFIG.TXT)
#define PROFILE_SWITCH_NOTE 24

// Configuration file names on SD card
#define CONFIG_FILE_NAME "CONFIG.TXT"
#define MAPPINGS_FILE_NAME "MAPPINGS.TXT"

// HID Keyboard Usage Codes (USB HID Standard)
// Common keys for gaming:
#define KEY_A           0x04
#define KEY_B           0x05
#define KEY_C           0x06
#define KEY_D           0x07
#define KEY_E           0x08
#define KEY_F           0x09
#define KEY_G           0x0A
#define KEY_H           0x0B
#define KEY_I           0x0C
#define KEY_J           0x0D
#define KEY_K           0x0E
#define KEY_L           0x0F
#define KEY_M           0x10
#define KEY_N           0x11
#define KEY_O           0x12
#define KEY_P           0x13
#define KEY_Q           0x14
#define KEY_R           0x15
#define KEY_S           0x16
#define KEY_T           0x17
#define KEY_U           0x18
#define KEY_V           0x19
#define KEY_W           0x1A
#define KEY_X           0x1B
#define KEY_Y           0x1C
#define KEY_Z           0x1D

// Number keys
#define KEY_1           0x1E
#define KEY_2           0x1F
#define KEY_3           0x20
#define KEY_4           0x21
#define KEY_5           0x22
#define KEY_6           0x23
#define KEY_7           0x24
#define KEY_8           0x25
#define KEY_9           0x26
#define KEY_0           0x27

// Special keys
#define KEY_ENTER       0x28
#define KEY_ESC         0x29
#define KEY_BACKSPACE   0x2A
#define KEY_TAB         0x2B
#define KEY_SPACE       0x2C
#define KEY_MINUS       0x2D
#define KEY_EQUAL       0x2E
#define KEY_LEFTBRACE   0x2F  // [
#define KEY_RIGHTBRACE  0x30  // ]
#define KEY_BACKSLASH   0x31  // \
#define KEY_SEMICOLON   0x33  // ;
#define KEY_APOSTROPHE  0x34  // '
#define KEY_GRAVE       0x35  // `
#define KEY_COMMA       0x36  // ,
#define KEY_DOT         0x37  // .
#define KEY_SLASH       0x38  // /

// Arrow keys
#define KEY_UP          0x52
#define KEY_DOWN        0x51
#define KEY_LEFT        0x50
#define KEY_RIGHT       0x4F

// Function keys
#define KEY_F1          0x3A
#define KEY_F2          0x3B
#define KEY_F3          0x3C
#define KEY_F4          0x3D
#define KEY_F5          0x3E
#define KEY_F6          0x3F
#define KEY_F7          0x40
#define KEY_F8          0x41
#define KEY_F9          0x42
#define KEY_F10         0x43
#define KEY_F11         0x44
#define KEY_F12         0x45

// Modifier keys (for combinations)
#define KEY_LEFTCTRL    0xE0
#define KEY_LEFTSHIFT   0xE1
#define KEY_LEFTALT     0xE2
#define KEY_LEFTMETA    0xE3  // Windows/Command key
#define KEY_RIGHTCTRL   0xE4
#define KEY_RIGHTSHIFT  0xE5
#define KEY_RIGHTALT    0xE6
#define KEY_RIGHTMETA   0xE7

// Additional useful keys
#define KEY_CAPSLOCK    0x39
#define KEY_DELETE      0x4C
#define KEY_HOME        0x4A
#define KEY_END         0x4D
#define KEY_PAGEUP      0x4B
#define KEY_PAGEDOWN    0x4E

// Modifier key masks for Keyboard.set_modifier()
#define MODIFIERKEY_LEFTCTRL    0x01
#define MODIFIERKEY_LEFTSHIFT   0x02
#define MODIFIERKEY_LEFTALT     0x04
#define MODIFIERKEY_LEFTMETA    0x08
#define MODIFIERKEY_RIGHTCTRL   0x10
#define MODIFIERKEY_RIGHTSHIFT  0x20
#define MODIFIERKEY_RIGHTALT    0x40
#define MODIFIERKEY_RIGHTMETA   0x80

#endif // MIDI_CONFIG_H
