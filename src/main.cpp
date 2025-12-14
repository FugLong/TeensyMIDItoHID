/*
 * Teensy MIDI to HID Keyboard Translator
 * 
 * Hardware-based MIDI to keyboard translator for Teensy 4.1
 * Allows using MIDI instruments in video games without software on the PC
 * 
 * Features:
 * - USB MIDI Host support (class-compliant devices)
 * - HID Keyboard output (appears as generic USB keyboard)
 * - SD card configuration (CONFIG.TXT and mapping files)
 * - Fast-press mode for games that don't recognize held keys
 * - Polyphonic chord support (up to 6 simultaneous keys)
 * - Modifier key support (Shift, Ctrl, Alt, Meta/Win)
 * 
 * Configuration:
 * - CONFIG.TXT: FAST_PRESS_MODE, PRESS_DURATION settings
 * - Mapping files: MIDI note to keyboard key mappings
 * 
 * See README.md for full documentation
 */

#include <Arduino.h>
#include <USBHost_t36.h>
#include <SD.h>
#include <SPI.h>
#include "MidiConfig.h"

// USB MIDI Host - support up to 4 MIDI devices
USBHost myusb;
// USB Hub support (needed when using a USB hub)
USBHub hub1(myusb);
USBHub hub2(myusb);
MIDIDevice midi1(myusb);
MIDIDevice midi2(myusb);
MIDIDevice midi3(myusb);
MIDIDevice midi4(myusb);

// Structure to store key mapping with modifier
struct KeyMapping {
  byte keyCode;      // HID key code (0 = unmapped)
  byte modifierMask; // Modifier mask (SHIFT, CTRL, etc.)
};

// MIDI note -> Key mapping with modifiers
KeyMapping noteToKey[MAX_MIDI_NOTES];  // 128 MIDI notes (0-127)

// Configuration settings
struct Config {
  bool fastPressMode;     // If true, send quick press/release regardless of MIDI duration
  unsigned int pressDurationMs;  // Duration for fast press mode (milliseconds)
};

Config config = {
  .fastPressMode = true,      // Default: fast press mode enabled
  .pressDurationMs = 0        // Default: 0ms = immediate press/release (like open source player)
};

// Polyphony support: Track simultaneously pressed keys with modifiers
// USB HID keyboard supports up to 6 keys + modifiers in a single report
struct PressedKey {
  byte keyCode;
  byte modifierMask;
};

PressedKey pressedKeys[MAX_SIMULTANEOUS_KEYS];
byte pressedKeyCount = 0;  // Number of keys currently pressed

// For fast-press mode: track keys that need timed release
struct FastPressTimer {
  byte keyCode;
  byte modifierMask;
  unsigned long releaseTime;  // millis() timestamp when key should be released
};

FastPressTimer fastPressTimers[MAX_SIMULTANEOUS_KEYS];
byte fastPressKeyCount = 0;

// Forward declaration
bool parseKeyMapping(String keyName, byte& keyCode, byte& modifierMask);
void loadConfig();
void loadMappings();
void addPressedKey(byte keyCode, byte modifierMask);
void removePressedKey(byte keyCode, byte modifierMask);
void updateKeyboardState();
void handleFastPress();
void processMidiMessage(MIDIDevice& midi, int deviceNum);

void setup() {
  // Initialize USB Host
  myusb.begin();
  
  // Give USB Host time to initialize, especially important for hubs
  delay(500);
  
  // Initialize SD card
  if (!SD.begin(BUILTIN_SDCARD)) {
    // SD card failed - use hardcoded fallback mappings for testing
    noteToKey[60].keyCode = KEY_H;
    noteToKey[60].modifierMask = 0;
    noteToKey[58].keyCode = KEY_G;
    noteToKey[58].modifierMask = 0;
    delay(2000);  // Give USB Host more time to enumerate devices, especially with hubs
    return;
  }
  
  // Load configuration from CONFIG.TXT
  loadConfig();
  
  // Load mappings from mapping file (WWM36_MAPPINGS.TXT, WWM21_MAPPINGS.TXT, or MAPPINGS.TXT)
  loadMappings();
  
  // Allow time for USB Host to enumerate devices (hubs may take longer)
  // Run USB Task multiple times to ensure hubs and devices are detected
  for (int i = 0; i < 20; i++) {
    myusb.Task();
    delay(50);  // Reduced delay for faster enumeration
  }
  
  delay(500);  // Wait for USB keyboard to initialize
}

void loop() {
  // USB Task must be called frequently for proper device communication
  // This is especially important with hubs that may buffer or delay messages
  myusb.Task();
  
  // Handle fast-press mode timing
  if (config.fastPressMode) {
    handleFastPress();
  }
  
  // Check for MIDI messages from all 4 possible MIDI devices
  // This ensures we catch messages regardless of which device instance the controller uses
  // With hubs, devices may enumerate on different instances, so check all
  // Only check devices that are connected (using the bool operator)
  if (midi1 && midi1.read()) {
    processMidiMessage(midi1, 1);
  }
  if (midi2 && midi2.read()) {
    processMidiMessage(midi2, 2);
  }
  if (midi3 && midi3.read()) {
    processMidiMessage(midi3, 3);
  }
  if (midi4 && midi4.read()) {
    processMidiMessage(midi4, 4);
  }
  
  // Small delay to prevent tight loop (helps with hub communication)
  delayMicroseconds(100);
}

// Process MIDI message from any MIDI device (handles all MIDI channels)
void processMidiMessage(MIDIDevice& midi, int deviceNum) {
  byte type = midi.getType();
  byte note = midi.getData1();
  byte velocity = midi.getData2();
  
  // Accept all MIDI channels (0-15) - no channel filtering
  // The USBHost_t36 library handles channel messages automatically
  
  if (type == midi.NoteOn && velocity > 0) {
    // Note On
    KeyMapping mapping = noteToKey[note];
    if (mapping.keyCode > 0) {
      if (config.fastPressMode) {
        // Fast press mode: send quick press/release
        if (config.pressDurationMs == 0) {
          // Immediate press/release (like open source player)
          addPressedKey(mapping.keyCode, mapping.modifierMask);
          updateKeyboardState();
          // Release immediately
          removePressedKey(mapping.keyCode, mapping.modifierMask);
          updateKeyboardState();
        } else {
          // Timed press/release (for longer durations)
          addPressedKey(mapping.keyCode, mapping.modifierMask);
          updateKeyboardState();
          
          // Schedule release after pressDurationMs
          if (fastPressKeyCount < MAX_SIMULTANEOUS_KEYS) {
            fastPressTimers[fastPressKeyCount].keyCode = mapping.keyCode;
            fastPressTimers[fastPressKeyCount].modifierMask = mapping.modifierMask;
            fastPressTimers[fastPressKeyCount].releaseTime = millis() + config.pressDurationMs;
            fastPressKeyCount++;
          }
        }
      } else {
        // Normal mode: hold key until NoteOff
        addPressedKey(mapping.keyCode, mapping.modifierMask);
        updateKeyboardState();
      }
    }
  }
  else if (type == midi.NoteOff || (type == midi.NoteOn && velocity == 0)) {
    // Note Off
    KeyMapping mapping = noteToKey[note];
    if (mapping.keyCode > 0 && !config.fastPressMode) {
      // Only handle NoteOff in normal mode (fast mode uses timers)
      removePressedKey(mapping.keyCode, mapping.modifierMask);
      updateKeyboardState();
    }
  }
}

// Load configuration from CONFIG.TXT
void loadConfig() {
  File file = SD.open(CONFIG_FILE_NAME, FILE_READ);
  if (!file) {
    // Config file doesn't exist, use defaults
    return;
  }
  
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    
    // Skip comments and empty lines
    if (line.length() == 0 || line.startsWith("#")) {
      continue;
    }
    
    // Parse: SETTING=VALUE
    int equalsPos = line.indexOf('=');
    if (equalsPos > 0) {
      String setting = line.substring(0, equalsPos);
      String value = line.substring(equalsPos + 1);
      setting.trim();
      setting.toUpperCase();
      value.trim();
      value.toUpperCase();
      
      if (setting == "FAST_PRESS_MODE" || setting == "FASTPRESS") {
        config.fastPressMode = (value == "1" || value == "TRUE" || value == "ON" || value == "YES");
      }
      else if (setting == "PRESS_DURATION" || setting == "DURATION") {
        int duration = value.toInt();
        // Valid range: 0ms (immediate) to 1000ms (1 second)
        if (duration >= 0 && duration <= 1000) {
          config.pressDurationMs = duration;
        }
      }
    }
  }
  file.close();
}

// Load mappings from first .txt file found that contains "MAPPINGS" in filename (case-insensitive)
// This allows users to name their mapping files however they want (e.g., "MY_GAME_MAPPINGS.txt", "mappings.txt")
void loadMappings() {
  // Initialize all mappings to unmapped (keyCode = 0 means unmapped)
  for (int i = 0; i < MAX_MIDI_NOTES; i++) {
    noteToKey[i].keyCode = 0;
    noteToKey[i].modifierMask = 0;
  }
  
  // Open root directory and search for mapping files
  File root = SD.open("/");
  if (!root) {
    // SD card root not accessible - use fallback test mappings
    noteToKey[60].keyCode = KEY_H;
    noteToKey[60].modifierMask = 0;
    noteToKey[58].keyCode = KEY_G;
    noteToKey[58].modifierMask = 0;
    return;
  }
  
  File file;
  bool fileFound = false;
  String foundFileName = "";
  
  // Search through files in root directory
  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      // No more files
      break;
    }
    
    // Skip directories
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }
    
    // Get filename (must capture before closing entry)
    String fileName = String(entry.name());
    fileName.toUpperCase();  // Convert to uppercase for case-insensitive comparison
    
    // Check if filename contains "MAPPINGS" and ends with ".TXT"
    if (fileName.indexOf("MAPPINGS") >= 0 && fileName.endsWith(".TXT")) {
      // Found a mapping file - save filename, close directory entry, then open file for reading
      foundFileName = String(entry.name());
      entry.close();
      file = SD.open(foundFileName.c_str(), FILE_READ);
      if (file) {
        fileFound = true;
        break;
      }
    } else {
      entry.close();
    }
  }
  
  root.close();
  
  if (!fileFound) {
    // No mapping file found - use fallback test mappings
    noteToKey[60].keyCode = KEY_H;
    noteToKey[60].modifierMask = 0;
    noteToKey[58].keyCode = KEY_G;
    noteToKey[58].modifierMask = 0;
    return;
  }
  
  // Load mappings from the found file
  int mappingCount = 0;
  
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    
    // Skip comments and empty lines
    if (line.length() == 0 || line.startsWith("#")) {
      continue;
    }
    
    // Parse: MIDI_NOTE=KEY_NAME
    int equalsPos = line.indexOf('=');
    if (equalsPos > 0) {
      int note = line.substring(0, equalsPos).toInt();
      String keyName = line.substring(equalsPos + 1);
      
      // Remove inline comments (everything after #)
      int commentPos = keyName.indexOf('#');
      if (commentPos >= 0) {
        keyName = keyName.substring(0, commentPos);
      }
      keyName.trim();
      
      // Validate MIDI note range (0-127)
      if (note >= 0 && note < MAX_MIDI_NOTES) {
        byte keyCode = 0;
        byte modifierMask = 0;
        if (parseKeyMapping(keyName, keyCode, modifierMask)) {
          noteToKey[note].keyCode = keyCode;
          noteToKey[note].modifierMask = modifierMask;
          mappingCount++;
        }
      }
    }
  }
  
  file.close();
}

// Parse key name with optional modifiers (e.g., "SHIFT+F", "F+SHIFT", "CTRL+SPACE")
// Returns true if parsing succeeded
bool parseKeyMapping(String keyName, byte& keyCode, byte& modifierMask) {
  keyName.trim();
  keyName.toUpperCase();
  modifierMask = 0;
  
  // Check for modifier combinations (SHIFT+F, CTRL+SPACE, etc.)
  int plusPos = keyName.indexOf('+');
  String baseKey = keyName;
  String modifierStr = "";
  
  if (plusPos > 0) {
    // Try "MODIFIER+KEY" format
    modifierStr = keyName.substring(0, plusPos);
    modifierStr.trim();
    baseKey = keyName.substring(plusPos + 1);
    baseKey.trim();
  } else {
    // Try "KEY+MODIFIER" format
    plusPos = keyName.lastIndexOf('+');
    if (plusPos > 0) {
      baseKey = keyName.substring(0, plusPos);
      baseKey.trim();
      modifierStr = keyName.substring(plusPos + 1);
      modifierStr.trim();
    }
  }
  
  // Parse modifiers
  if (modifierStr.length() > 0) {
    if (modifierStr == "SHIFT" || modifierStr == "LEFTSHIFT") {
      modifierMask |= MODIFIERKEY_LEFTSHIFT;
    } else if (modifierStr == "CTRL" || modifierStr == "CONTROL" || modifierStr == "LEFTCTRL") {
      modifierMask |= MODIFIERKEY_LEFTCTRL;
    } else if (modifierStr == "ALT" || modifierStr == "LEFTALT") {
      modifierMask |= MODIFIERKEY_LEFTALT;
    } else if (modifierStr == "META" || modifierStr == "WIN" || modifierStr == "CMD" || modifierStr == "LEFTMETA") {
      modifierMask |= MODIFIERKEY_LEFTMETA;
    }
  }
  
  // Parse base key
  baseKey.trim();
  
  // Single letter A-Z
  if (baseKey.length() == 1 && baseKey[0] >= 'A' && baseKey[0] <= 'Z') {
    keyCode = KEY_A + (baseKey[0] - 'A');
    return true;
  }
  
  // Number keys 0-9
  if (baseKey.length() == 1 && baseKey[0] >= '0' && baseKey[0] <= '9') {
    if (baseKey[0] == '0') {
      keyCode = KEY_0;
    } else {
      keyCode = KEY_1 + (baseKey[0] - '1');
    }
    return true;
  }
  
  // Named keys
  if (baseKey == "SPACE" || baseKey == "SPC") {
    keyCode = KEY_SPACE;
    return true;
  }
  if (baseKey == "ENTER" || baseKey == "RETURN") {
    keyCode = KEY_ENTER;
    return true;
  }
  if (baseKey == "TAB") {
    keyCode = KEY_TAB;
    return true;
  }
  if (baseKey == "ESC" || baseKey == "ESCAPE") {
    keyCode = KEY_ESC;
    return true;
  }
  if (baseKey == "BACKSPACE" || baseKey == "BS") {
    keyCode = KEY_BACKSPACE;
    return true;
  }
  
  return false; // Invalid
}

// Handle fast-press mode timing - release keys after duration
void handleFastPress() {
  unsigned long now = millis();
  for (int i = fastPressKeyCount - 1; i >= 0; i--) {
    if (now >= fastPressTimers[i].releaseTime) {
      // Time to release this specific key
      removePressedKey(fastPressTimers[i].keyCode, fastPressTimers[i].modifierMask);
      updateKeyboardState();
      
      // Remove timer
      for (int j = i; j < fastPressKeyCount - 1; j++) {
        fastPressTimers[j] = fastPressTimers[j + 1];
      }
      fastPressKeyCount--;
    }
  }
}

// Add a key to the pressed keys list (polyphony support)
// Prevents duplicate entries (same keyCode + modifierMask combo)
void addPressedKey(byte keyCode, byte modifierMask) {
  // Check if key+modifier combo is already pressed
  for (int i = 0; i < pressedKeyCount; i++) {
    if (pressedKeys[i].keyCode == keyCode && pressedKeys[i].modifierMask == modifierMask) {
      return;  // Already pressed, skip duplicate
    }
  }
  
  // Add if we have room (USB HID keyboard supports max 6 keys)
  if (pressedKeyCount < MAX_SIMULTANEOUS_KEYS) {
    pressedKeys[pressedKeyCount].keyCode = keyCode;
    pressedKeys[pressedKeyCount].modifierMask = modifierMask;
    pressedKeyCount++;
  }
}

// Remove a key from the pressed keys list
void removePressedKey(byte keyCode, byte modifierMask) {
  for (int i = 0; i < pressedKeyCount; i++) {
    if (pressedKeys[i].keyCode == keyCode && pressedKeys[i].modifierMask == modifierMask) {
      // Shift remaining keys down
      for (int j = i; j < pressedKeyCount - 1; j++) {
        pressedKeys[j] = pressedKeys[j + 1];
      }
      pressedKeyCount--;
      return;
    }
  }
}

// Update the keyboard state with all currently pressed keys
// Preserves order of key presses, batches consecutive keys with same modifier for speed
// Optimized for fast execution: single send for all-same-modifier chords, batched sends for mixed modifiers
void updateKeyboardState() {
  if (pressedKeyCount == 0) {
    // No keys pressed - clear everything
    Keyboard.set_key1(0);
    Keyboard.set_key2(0);
    Keyboard.set_key3(0);
    Keyboard.set_key4(0);
    Keyboard.set_key5(0);
    Keyboard.set_key6(0);
    Keyboard.set_modifier(0);
    Keyboard.send_now();
    return;
  }
  
  // Check if all keys have the same modifier state
  bool allSameModifier = true;
  byte firstModifier = pressedKeys[0].modifierMask;
  for (int i = 1; i < pressedKeyCount; i++) {
    if (pressedKeys[i].modifierMask != firstModifier) {
      allSameModifier = false;
      break;
    }
  }
  
  if (allSameModifier) {
    // All keys have same modifier - send them all at once (fastest)
    Keyboard.set_key1(0);
    Keyboard.set_key2(0);
    Keyboard.set_key3(0);
    Keyboard.set_key4(0);
    Keyboard.set_key5(0);
    Keyboard.set_key6(0);
    Keyboard.set_modifier(firstModifier);
    
    // Set all keys in order
    if (pressedKeyCount > 0) Keyboard.set_key1(pressedKeys[0].keyCode);
    if (pressedKeyCount > 1) Keyboard.set_key2(pressedKeys[1].keyCode);
    if (pressedKeyCount > 2) Keyboard.set_key3(pressedKeys[2].keyCode);
    if (pressedKeyCount > 3) Keyboard.set_key4(pressedKeys[3].keyCode);
    if (pressedKeyCount > 4) Keyboard.set_key5(pressedKeys[4].keyCode);
    if (pressedKeyCount > 5) Keyboard.set_key6(pressedKeys[5].keyCode);
    
    Keyboard.send_now();
  } else {
    // Mixed modifiers - batch consecutive keys with same modifier, preserve order
    // Process keys in order, grouping consecutive keys with same modifier
    
    int startIdx = 0;
    byte currentModifier = pressedKeys[0].modifierMask;
    
    for (int i = 0; i <= pressedKeyCount; i++) {
      // Check if we've reached end or modifier changed
      bool modifierChanged = (i == pressedKeyCount) || 
                            (pressedKeys[i].modifierMask != currentModifier);
      
      if (modifierChanged && i > startIdx) {
        // Send batch of consecutive keys with same modifier
        Keyboard.set_key1(0);
        Keyboard.set_key2(0);
        Keyboard.set_key3(0);
        Keyboard.set_key4(0);
        Keyboard.set_key5(0);
        Keyboard.set_key6(0);
        Keyboard.set_modifier(currentModifier);
        
        // Set keys in this batch (in order, max 6 keys per USB HID report)
        int keyIdx = 0;
        for (int j = startIdx; j < i && keyIdx < MAX_SIMULTANEOUS_KEYS; j++) {
          if (keyIdx == 0) Keyboard.set_key1(pressedKeys[j].keyCode);
          else if (keyIdx == 1) Keyboard.set_key2(pressedKeys[j].keyCode);
          else if (keyIdx == 2) Keyboard.set_key3(pressedKeys[j].keyCode);
          else if (keyIdx == 3) Keyboard.set_key4(pressedKeys[j].keyCode);
          else if (keyIdx == 4) Keyboard.set_key5(pressedKeys[j].keyCode);
          else if (keyIdx == 5) Keyboard.set_key6(pressedKeys[j].keyCode);
          keyIdx++;
        }
        
        Keyboard.send_now();
        
        // Start next batch
        if (i < pressedKeyCount) {
          startIdx = i;
          currentModifier = pressedKeys[i].modifierMask;
        }
      }
    }
  }
}

