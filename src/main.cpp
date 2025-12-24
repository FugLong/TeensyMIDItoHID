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

// Structure to store a profile (set of mappings)
struct Profile {
  String name;                              // Profile name (e.g., "default", "touchscreen")
  KeyMapping noteToKey[MAX_MIDI_NOTES];     // 128 MIDI notes (0-127)
  bool isValid;                              // True if profile has been loaded
  bool fastPressMode;                        // Fast-press mode for this profile (overrides global config)
  unsigned int pressDurationMs;              // Press duration for this profile (overrides global config)
};

// Multiple profiles support
Profile profiles[MAX_PROFILES];
byte profileCount = 0;                      // Number of profiles loaded
byte currentProfileIndex = 0;                // Index of currently active profile

// Configuration settings
struct Config {
  bool fastPressMode;     // If true, send quick press/release regardless of MIDI duration
  unsigned int pressDurationMs;  // Duration for fast press mode (milliseconds)
  byte profileSwitchNote; // MIDI note to trigger profile switching (default: 12 = C0)
};

Config config = {
  .fastPressMode = true,      // Default: fast press mode enabled
  .pressDurationMs = 0,       // Default: 0ms = immediate press/release (like open source player)
  .profileSwitchNote = PROFILE_SWITCH_NOTE  // Default: C1 = note 24 (configurable via CONFIG.TXT)
};

// Polyphony support: Track simultaneously pressed keys with modifiers
// USB HID keyboard supports up to 6 keys + modifiers in a single report
struct PressedKey {
  byte keyCode;
  byte modifierMask;
};

PressedKey pressedKeys[MAX_SIMULTANEOUS_KEYS];
byte pressedKeyCount = 0;  // Number of keys currently pressed

// Track modifier-only keys separately (LSHIFT, RSHIFT, etc. as standalone keys)
// This prevents modifier changes from causing other keys to replay
byte activeModifierKeys = 0;  // Combined modifier mask from modifier-only keys

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
void switchProfile(byte profileIndex);
void addPressedKey(byte keyCode, byte modifierMask);
void removePressedKey(byte keyCode, byte modifierMask);
void updateKeyboardState();
void handleFastPress();
void processMidiMessage(MIDIDevice& midi, int deviceNum);

void setup() {
  // Initialize Serial for debugging (only if ENABLE_DEBUG is defined)
  #ifdef ENABLE_DEBUG
  Serial.begin(115200);
  delay(1000);  // Give Serial time to initialize
  Serial.println("=== Teensy MIDI to HID Translator ===");
  #endif
  
  // Initialize USB Host
  myusb.begin();
  
  // Give USB Host time to initialize, especially important for hubs
  delay(500);
  
  // Initialize profiles
  profileCount = 0;
  currentProfileIndex = 0;
  for (int i = 0; i < MAX_PROFILES; i++) {
    profiles[i].name = "";
    profiles[i].isValid = false;
    profiles[i].fastPressMode = config.fastPressMode;  // Default to global config
    profiles[i].pressDurationMs = config.pressDurationMs;  // Default to global config
    for (int j = 0; j < MAX_MIDI_NOTES; j++) {
      profiles[i].noteToKey[j].keyCode = 0;
      profiles[i].noteToKey[j].modifierMask = 0;
    }
  }
  
  // Initialize SD card
  if (!SD.begin(BUILTIN_SDCARD)) {
    // SD card failed - use hardcoded fallback mappings for testing
    profiles[0].name = "default";
    profiles[0].isValid = true;
    profiles[0].fastPressMode = config.fastPressMode;
    profiles[0].pressDurationMs = config.pressDurationMs;
    profiles[0].noteToKey[60].keyCode = KEY_H;
    profiles[0].noteToKey[60].modifierMask = 0;
    profiles[0].noteToKey[58].keyCode = KEY_G;
    profiles[0].noteToKey[58].modifierMask = 0;
    profileCount = 1;
    currentProfileIndex = 0;
    delay(2000);  // Give USB Host more time to enumerate devices, especially with hubs
    return;
  }
  
  // Load configuration from CONFIG.TXT
  loadConfig();
  
  // Load all mapping files from SD card (each file becomes one profile)
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
  
  // Handle fast-press mode timing (use current profile's setting)
  if (profiles[currentProfileIndex].isValid && profiles[currentProfileIndex].fastPressMode) {
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
  
  // Debug: Log all MIDI messages
  #ifdef ENABLE_DEBUG
  if (type == midi.NoteOn || type == midi.NoteOff) {
    Serial.print("MIDI: ");
    Serial.print(type == midi.NoteOn ? "NoteOn" : "NoteOff");
    Serial.print(" note=");
    Serial.print(note);
    Serial.print(" velocity=");
    Serial.println(velocity);
  }
  #endif
  
  // Check for profile switch note (configurable, default: C1 = note 24)
  // Note: 255 disables profile switching
  if (config.profileSwitchNote < 255 && type == midi.NoteOn && velocity > 0 && note == config.profileSwitchNote) {
    #ifdef ENABLE_DEBUG
    Serial.print("Profile switch note received (note ");
    Serial.print(note);
    Serial.print("), current profile count: ");
    Serial.println(profileCount);
    #endif
    
    // Switch to next profile
    if (profileCount > 1) {
      byte nextProfile = (currentProfileIndex + 1) % profileCount;
      #ifdef ENABLE_DEBUG
      Serial.print("Switching from profile ");
      Serial.print(currentProfileIndex);
      Serial.print(" (");
      Serial.print(profiles[currentProfileIndex].name);
      Serial.print(") to profile ");
      Serial.print(nextProfile);
      Serial.print(" (");
      Serial.print(profiles[nextProfile].name);
      Serial.println(")");
      #endif
      switchProfile(nextProfile);
    } else {
      #ifdef ENABLE_DEBUG
      Serial.println("ERROR: Only 1 profile loaded - cannot switch! Need multiple mapping files on SD card.");
      #endif
    }
    return;  // Don't process profile switch note as a regular key
  }
  
  if (type == midi.NoteOn && velocity > 0) {
    // Note On
    KeyMapping mapping = profiles[currentProfileIndex].noteToKey[note];
    // Process if there's a key code OR a modifier (for modifier-only keys like LSHIFT/RSHIFT)
    if (mapping.keyCode > 0 || mapping.modifierMask > 0) {
      #ifdef ENABLE_DEBUG
      Serial.print("Key press: note ");
      Serial.print(note);
      Serial.print(" -> keyCode ");
      Serial.print(mapping.keyCode);
      Serial.print(" (profile: ");
      Serial.print(profiles[currentProfileIndex].name);
      Serial.println(")");
      #endif
      
      // Check if this is a modifier-only key (keyCode=0, modifierMask>0)
      if (mapping.keyCode == 0 && mapping.modifierMask > 0) {
        // Modifier-only key (LSHIFT, RSHIFT, etc.) - handle separately to avoid replaying other keys
        activeModifierKeys |= mapping.modifierMask;
        updateKeyboardState();
        return;  // Don't process as regular key
      }
      
      // Regular key (with or without modifier)
      // Use current profile's fast-press mode setting
      bool profileFastPress = profiles[currentProfileIndex].fastPressMode;
      unsigned int profileDuration = profiles[currentProfileIndex].pressDurationMs;
      
      if (profileFastPress) {
        // Fast press mode: send quick press/release
        if (profileDuration == 0) {
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
            fastPressTimers[fastPressKeyCount].releaseTime = millis() + profileDuration;
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
    KeyMapping mapping = profiles[currentProfileIndex].noteToKey[note];
    // Process if there's a key code OR a modifier (for modifier-only keys like LSHIFT/RSHIFT)
    if (mapping.keyCode > 0 || mapping.modifierMask > 0) {
      // Check if this is a modifier-only key (keyCode=0, modifierMask>0)
      if (mapping.keyCode == 0 && mapping.modifierMask > 0) {
        // Modifier-only key release - handle separately to avoid replaying other keys
        activeModifierKeys &= ~mapping.modifierMask;
        updateKeyboardState();
        return;  // Don't process as regular key
      }
      
      // Regular key release
      // Use current profile's fast-press mode setting
      bool profileFastPress = profiles[currentProfileIndex].fastPressMode;
      if (!profileFastPress) {
        // Only handle NoteOff in normal mode (fast mode uses timers)
        removePressedKey(mapping.keyCode, mapping.modifierMask);
        updateKeyboardState();
      }
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
      else if (setting == "PROFILE_SWITCH_NOTE" || setting == "PROFILE_SWITCH" || setting == "SWITCH_NOTE") {
        int note = value.toInt();
        // Valid range: 0-127 (MIDI note range), or 255 to disable
        if ((note >= 0 && note < MAX_MIDI_NOTES) || note == 255) {
          config.profileSwitchNote = note;
        }
      }
    }
  }
  file.close();
}

// Switch to a different profile
void switchProfile(byte profileIndex) {
  if (profileIndex < profileCount && profiles[profileIndex].isValid) {
    currentProfileIndex = profileIndex;
    // Release all currently pressed keys when switching profiles
    for (int i = pressedKeyCount - 1; i >= 0; i--) {
      removePressedKey(pressedKeys[i].keyCode, pressedKeys[i].modifierMask);
    }
    // Clear modifier-only keys
    activeModifierKeys = 0;
    updateKeyboardState();
    // Clear fast press timers
    fastPressKeyCount = 0;
  }
}

// Load all mapping files from SD card root directory
// Each .txt file containing "MAPPINGS" in its name becomes one profile
// Profile name is derived from the filename (without .txt extension)
// Pressing the profile switch note cycles through all loaded mapping files
void loadMappings() {
  // Initialize all profiles
  profileCount = 0;
  currentProfileIndex = 0;
  for (int i = 0; i < MAX_PROFILES; i++) {
    profiles[i].name = "";
    profiles[i].isValid = false;
    for (int j = 0; j < MAX_MIDI_NOTES; j++) {
      profiles[i].noteToKey[j].keyCode = 0;
      profiles[i].noteToKey[j].modifierMask = 0;
    }
  }
  
  // Open root directory and search for all mapping files
  File root = SD.open("/");
  if (!root) {
    // SD card root not accessible - use fallback test mappings
    profiles[0].name = "default";
    profiles[0].isValid = true;
    profiles[0].noteToKey[60].keyCode = KEY_H;
    profiles[0].noteToKey[60].modifierMask = 0;
    profiles[0].noteToKey[58].keyCode = KEY_G;
    profiles[0].noteToKey[58].modifierMask = 0;
    profileCount = 1;
    currentProfileIndex = 0;
    return;
  }
  
  // First pass: collect all mapping file names
  String mappingFiles[MAX_PROFILES];
  int fileCount = 0;
  
  #ifdef ENABLE_DEBUG
  Serial.println("Scanning SD card for mapping files...");
  #endif
  
  while (fileCount < MAX_PROFILES) {
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
    String fileNameUpper = fileName;
    fileNameUpper.toUpperCase();  // Convert to uppercase for case-insensitive comparison
    
    #ifdef ENABLE_DEBUG
    Serial.print("Found file: ");
    Serial.println(fileName);
    #endif
    
    // Skip macOS metadata files (._ files)
    if (fileName.startsWith("._")) {
      #ifdef ENABLE_DEBUG
      Serial.println("  -> Skipping macOS metadata file");
      #endif
      entry.close();
      continue;
    }
    
    // Check if filename contains "MAPPINGS" and ends with ".TXT"
    if (fileNameUpper.indexOf("MAPPINGS") >= 0 && fileNameUpper.endsWith(".TXT")) {
      mappingFiles[fileCount] = fileName;
      fileCount++;
      #ifdef ENABLE_DEBUG
      Serial.print("  -> Added as mapping file #");
      Serial.println(fileCount);
      #endif
    }
    
    entry.close();
  }
  
  root.close();
  
  #ifdef ENABLE_DEBUG
  Serial.print("Total mapping files found: ");
  Serial.println(fileCount);
  #endif
  
  if (fileCount == 0) {
    // No mapping files found - use fallback test mappings
    profiles[0].name = "default";
    profiles[0].isValid = true;
    profiles[0].fastPressMode = config.fastPressMode;
    profiles[0].pressDurationMs = config.pressDurationMs;
    profiles[0].noteToKey[60].keyCode = KEY_H;
    profiles[0].noteToKey[60].modifierMask = 0;
    profiles[0].noteToKey[58].keyCode = KEY_G;
    profiles[0].noteToKey[58].modifierMask = 0;
    profileCount = 1;
    currentProfileIndex = 0;
    return;
  }
  
  // Second pass: load each mapping file as a separate profile
  for (int fileIdx = 0; fileIdx < fileCount && profileCount < MAX_PROFILES; fileIdx++) {
    File file = SD.open(mappingFiles[fileIdx].c_str(), FILE_READ);
    if (!file) {
      continue;  // Skip files that can't be opened
    }
    
    // Extract profile name from filename (remove .txt extension)
    String profileName = mappingFiles[fileIdx];
    int dotPos = profileName.lastIndexOf('.');
    if (dotPos > 0) {
      profileName = profileName.substring(0, dotPos);
    }
    profileName.trim();
    
    // If profile name is empty, use a default
    if (profileName.length() == 0) {
      profileName = "mapping";
    }
    
    // Create new profile for this file
    int profileIdx = profileCount;
    profiles[profileIdx].name = profileName;
    profiles[profileIdx].isValid = true;
    // Initialize with global config defaults from CONFIG.TXT
    // These can be overridden by FAST_PRESS_MODE= and PRESS_DURATION= lines in the mapping file
    profiles[profileIdx].fastPressMode = config.fastPressMode;
    profiles[profileIdx].pressDurationMs = config.pressDurationMs;
    profileCount++;
    
    // If this is the first profile, make it the active one
    if (profileCount == 1) {
      currentProfileIndex = 0;
    }
    
    #ifdef ENABLE_DEBUG
    Serial.print("Loading profile ");
    Serial.print(profileCount);
    Serial.print(": ");
    Serial.print(profileName);
    Serial.print(" from ");
    Serial.println(mappingFiles[fileIdx]);
    #endif
    
    // Load mappings from this file (ignore [profile_name] sections - each file is one profile)
    int mappingCount = 0;
    
    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();
      
      // Skip empty lines
      if (line.length() == 0) {
        continue;
      }
      
      // Skip profile section headers (legacy support - they're ignored now)
      if (line.startsWith("[") && line.endsWith("]")) {
        continue;
      }
      
      // Skip comments
      if (line.startsWith("#")) {
        continue;
      }
      
      // Parse profile-specific settings: FAST_PRESS_MODE=value or PRESS_DURATION=value
      // OR parse MIDI note mappings: MIDI_NOTE=KEY_NAME
      int equalsPos = line.indexOf('=');
      if (equalsPos > 0) {
        String leftSide = line.substring(0, equalsPos);
        String rightSide = line.substring(equalsPos + 1);
        leftSide.trim();
        rightSide.trim();
        
        // Check if it's a setting (not a MIDI note mapping)
        // Settings have text keywords on the left side, MIDI notes are numbers 0-127
        String leftUpper = leftSide;
        leftUpper.toUpperCase();
        
        bool isSetting = false;
        if (leftUpper == "FAST_PRESS_MODE" || leftUpper == "FASTPRESS") {
          String value = rightSide;
          value.toUpperCase();
          profiles[profileIdx].fastPressMode = (value == "1" || value == "TRUE" || value == "ON" || value == "YES");
          #ifdef ENABLE_DEBUG
          Serial.print("  Profile fast-press mode: ");
          Serial.println(profiles[profileIdx].fastPressMode ? "enabled" : "disabled");
          #endif
          isSetting = true;
        }
        else if (leftUpper == "PRESS_DURATION" || leftUpper == "DURATION") {
          int duration = rightSide.toInt();
          if (duration >= 0 && duration <= 1000) {
            profiles[profileIdx].pressDurationMs = duration;
            #ifdef ENABLE_DEBUG
            Serial.print("  Profile press duration: ");
            Serial.print(duration);
            Serial.println("ms");
            #endif
          }
          isSetting = true;
        }
        
        if (isSetting) {
          continue;  // Skip to next line, this was a setting
        }
        
        // Not a setting, so it must be a MIDI note mapping: MIDI_NOTE=KEY_NAME
        int note = leftSide.toInt();
        String keyName = rightSide;
        
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
            profiles[profileIdx].noteToKey[note].keyCode = keyCode;
            profiles[profileIdx].noteToKey[note].modifierMask = modifierMask;
            mappingCount++;
          }
        }
      }
    }
    
    file.close();
    #ifdef ENABLE_DEBUG
    Serial.print("  -> Loaded ");
    Serial.print(mappingCount);
    Serial.println(" mappings");
    #endif
  }
  
  // Ensure we have at least one profile
  if (profileCount == 0) {
    profiles[0].name = "default";
    profiles[0].isValid = true;
    profiles[0].fastPressMode = config.fastPressMode;
    profiles[0].pressDurationMs = config.pressDurationMs;
    profileCount = 1;
    currentProfileIndex = 0;
    #ifdef ENABLE_DEBUG
    Serial.println("No profiles loaded - using fallback");
    #endif
  }
  
  #ifdef ENABLE_DEBUG
  Serial.println("=== Profile Loading Complete ===");
  Serial.print("Total profiles: ");
  Serial.println(profileCount);
  Serial.print("Active profile: ");
  Serial.print(currentProfileIndex);
  Serial.print(" (");
  Serial.print(profiles[currentProfileIndex].name);
  Serial.println(")");
  Serial.print("Profile switch note: ");
  Serial.println(config.profileSwitchNote);
  Serial.println();
  #endif
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
    } else if (modifierStr == "RSHIFT" || modifierStr == "RIGHTSHIFT") {
      modifierMask |= MODIFIERKEY_RIGHTSHIFT;
    } else if (modifierStr == "CTRL" || modifierStr == "CONTROL" || modifierStr == "LEFTCTRL") {
      modifierMask |= MODIFIERKEY_LEFTCTRL;
    } else if (modifierStr == "RCTRL" || modifierStr == "RIGHTCTRL") {
      modifierMask |= MODIFIERKEY_RIGHTCTRL;
    } else if (modifierStr == "ALT" || modifierStr == "LEFTALT") {
      modifierMask |= MODIFIERKEY_LEFTALT;
    } else if (modifierStr == "RALT" || modifierStr == "RIGHTALT") {
      modifierMask |= MODIFIERKEY_RIGHTALT;
    } else if (modifierStr == "META" || modifierStr == "WIN" || modifierStr == "CMD" || modifierStr == "LEFTMETA") {
      modifierMask |= MODIFIERKEY_LEFTMETA;
    } else if (modifierStr == "RMETA" || modifierStr == "RIGHTMETA") {
      modifierMask |= MODIFIERKEY_RIGHTMETA;
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
  
  // Modifier keys as standalone keys (must be sent as modifiers, not key codes)
  // USB HID keyboard protocol: modifiers are sent via modifier byte, not as key codes
  if (baseKey == "LSHIFT" || baseKey == "LEFTSHIFT") {
    keyCode = 0;  // No regular key, just the modifier
    modifierMask = MODIFIERKEY_LEFTSHIFT;
    return true;
  }
  if (baseKey == "RSHIFT" || baseKey == "RIGHTSHIFT") {
    keyCode = 0;  // No regular key, just the modifier
    modifierMask = MODIFIERKEY_RIGHTSHIFT;
    return true;
  }
  if (baseKey == "LCTRL" || baseKey == "LEFTCTRL") {
    keyCode = 0;  // No regular key, just the modifier
    modifierMask = MODIFIERKEY_LEFTCTRL;
    return true;
  }
  if (baseKey == "RCTRL" || baseKey == "RIGHTCTRL") {
    keyCode = 0;  // No regular key, just the modifier
    modifierMask = MODIFIERKEY_RIGHTCTRL;
    return true;
  }
  if (baseKey == "LALT" || baseKey == "LEFTALT") {
    keyCode = 0;  // No regular key, just the modifier
    modifierMask = MODIFIERKEY_LEFTALT;
    return true;
  }
  if (baseKey == "RALT" || baseKey == "RIGHTALT") {
    keyCode = 0;  // No regular key, just the modifier
    modifierMask = MODIFIERKEY_RIGHTALT;
    return true;
  }
  if (baseKey == "LMETA" || baseKey == "LEFTMETA" || baseKey == "LWIN" || baseKey == "LCMD") {
    keyCode = 0;  // No regular key, just the modifier
    modifierMask = MODIFIERKEY_LEFTMETA;
    return true;
  }
  if (baseKey == "RMETA" || baseKey == "RIGHTMETA" || baseKey == "RWIN" || baseKey == "RCMD") {
    keyCode = 0;  // No regular key, just the modifier
    modifierMask = MODIFIERKEY_RIGHTMETA;
    return true;
  }
  
  // Punctuation and special characters
  if (baseKey == "COMMA" || baseKey == ",") {
    keyCode = KEY_COMMA;
    return true;
  }
  if (baseKey == "DOT" || baseKey == "PERIOD" || baseKey == ".") {
    keyCode = KEY_DOT;
    return true;
  }
  if (baseKey == "SLASH" || baseKey == "/" || baseKey == "?") {
    // Note: "?" is typically SHIFT+/, but we'll map it to / for standalone use
    // If you need actual ?, use SHIFT+SLASH or SHIFT+/
    keyCode = KEY_SLASH;
    return true;
  }
  if (baseKey == "MINUS" || baseKey == "-" || baseKey == "DASH") {
    keyCode = KEY_MINUS;
    return true;
  }
  if (baseKey == "EQUAL" || baseKey == "EQUALS" || baseKey == "=") {
    keyCode = KEY_EQUAL;
    return true;
  }
  if (baseKey == "LEFTBRACE" || baseKey == "LBRACE" || baseKey == "[") {
    keyCode = KEY_LEFTBRACE;
    return true;
  }
  if (baseKey == "RIGHTBRACE" || baseKey == "RBRACE" || baseKey == "]") {
    keyCode = KEY_RIGHTBRACE;
    return true;
  }
  if (baseKey == "BACKSLASH" || baseKey == "BSLASH" || baseKey == "\\") {
    keyCode = KEY_BACKSLASH;
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
// Combines modifier-only keys (LSHIFT, RSHIFT, etc.) with regular keys without replaying
void updateKeyboardState() {
  // Combine modifier-only keys with regular key modifiers
  // activeModifierKeys contains modifiers from standalone modifier keys (LSHIFT, RSHIFT, etc.)
  
  if (pressedKeyCount == 0 && activeModifierKeys == 0) {
    // No keys pressed and no modifier-only keys - clear everything
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
  
  if (pressedKeyCount == 0) {
    // Only modifier-only keys active (no regular keys)
    Keyboard.set_key1(0);
    Keyboard.set_key2(0);
    Keyboard.set_key3(0);
    Keyboard.set_key4(0);
    Keyboard.set_key5(0);
    Keyboard.set_key6(0);
    Keyboard.set_modifier(activeModifierKeys);
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
  
  // Combine regular key modifiers with modifier-only keys
  byte combinedModifier = firstModifier | activeModifierKeys;
  
  if (allSameModifier) {
    // All keys have same modifier - send them all at once (fastest)
    Keyboard.set_key1(0);
    Keyboard.set_key2(0);
    Keyboard.set_key3(0);
    Keyboard.set_key4(0);
    Keyboard.set_key5(0);
    Keyboard.set_key6(0);
    Keyboard.set_modifier(combinedModifier);
    
    // Set all keys in order (only keys with keyCode > 0)
    int keyIdx = 0;
    for (int i = 0; i < pressedKeyCount && keyIdx < MAX_SIMULTANEOUS_KEYS; i++) {
      if (pressedKeys[i].keyCode > 0) {
        if (keyIdx == 0) Keyboard.set_key1(pressedKeys[i].keyCode);
        else if (keyIdx == 1) Keyboard.set_key2(pressedKeys[i].keyCode);
        else if (keyIdx == 2) Keyboard.set_key3(pressedKeys[i].keyCode);
        else if (keyIdx == 3) Keyboard.set_key4(pressedKeys[i].keyCode);
        else if (keyIdx == 4) Keyboard.set_key5(pressedKeys[i].keyCode);
        else if (keyIdx == 5) Keyboard.set_key6(pressedKeys[i].keyCode);
        keyIdx++;
      }
    }
    
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
        // Combine regular key modifier with modifier-only keys
        Keyboard.set_modifier(currentModifier | activeModifierKeys);
        
        // Set keys in this batch (in order, max 6 keys per USB HID report, only keyCode > 0)
        int keyIdx = 0;
        for (int j = startIdx; j < i && keyIdx < MAX_SIMULTANEOUS_KEYS; j++) {
          if (pressedKeys[j].keyCode > 0) {
            if (keyIdx == 0) Keyboard.set_key1(pressedKeys[j].keyCode);
            else if (keyIdx == 1) Keyboard.set_key2(pressedKeys[j].keyCode);
            else if (keyIdx == 2) Keyboard.set_key3(pressedKeys[j].keyCode);
            else if (keyIdx == 3) Keyboard.set_key4(pressedKeys[j].keyCode);
            else if (keyIdx == 4) Keyboard.set_key5(pressedKeys[j].keyCode);
            else if (keyIdx == 5) Keyboard.set_key6(pressedKeys[j].keyCode);
            keyIdx++;
          }
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

