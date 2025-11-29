# MidiMaster2

A C++ Qt application for MIDI synchronization and DAW control with bidirectional MIDI communication. The application can send MIDI clock signals to DAWs and receive MIDI clock from DAWs for synchronization.

## Features

- **MIDI Output**: Send MIDI clock, start/stop/continue commands, and MIDI notes to DAWs
- **Whole Note Emission**: Emits MIDI notes at whole note boundaries (every bar) with predictive timing to compensate for latency
- **MIDI Input**: Receive MIDI clock and transport commands from DAWs for synchronization
- **BPM Synchronization**: Automatically sync BPM from incoming MIDI clock or manually set BPM
- **Position Tracking**: Track song position via MIDI Song Position Pointer (SPP)
- **GUI Interface**: Qt-based graphical interface for port selection, BPM control, and monitoring
- **Real-time MIDI Processing**: Uses RTMidi for high-performance MIDI input and output with CoreMIDI backend

## Project Structure

The project is organized into the following structure:

```
MidiMaster2/
├── main.cpp                    # Application entry point
├── CMakeLists.txt              # CMake build configuration
├── build.sh                    # Build script
├── lib/
│   ├── ui/                     # User interface components
│   │   ├── MidiMasterWindow.h
│   │   └── MidiMasterWindow.cpp # Main application window with GUI
│   └── midiEngine/             # MIDI processing logic
│       ├── MidiEngine.h/cpp    # MIDI port management and communication
│       ├── SyncController.h/cpp # MIDI clock synchronization and BPM management
│       └── SyncControllerTest.h/cpp # Unit tests for SyncController
└── build/                      # Build directory (generated)
```

## Prerequisites

- CMake 3.16 or higher
- Qt5 or Qt6 (Core and Widgets components)
- C++ compiler with C++17 support
- macOS (designed for macOS with CoreMIDI support)
- Git (for fetching dependencies)

### Dependencies

The project automatically downloads and builds the following dependencies via CMake FetchContent:

- **RTMidi** (v6.0.0) - For high-performance MIDI input and output communication (CoreMIDI backend on macOS)
- **Drumstick** (v2.x) - Included for MIDI message constants and definitions (MIDI communication is handled by RTMidi)

### Installing Qt on macOS

If you don't have Qt installed, you can install it via Homebrew:

```bash
brew install qt@5
# or
brew install qt6
```

Then make sure CMake can find Qt:

```bash
export CMAKE_PREFIX_PATH="/opt/homebrew/opt/qt@5/lib/cmake:$CMAKE_PREFIX_PATH"
# or for Qt6:
# export CMAKE_PREFIX_PATH="/opt/homebrew/opt/qt6/lib/cmake:$CMAKE_PREFIX_PATH"
```

## Building

### Using the Build Script

The easiest way to build is using the provided script:

```bash
./build.sh
```

This will create a `build/` directory, configure CMake, and build the project.

### Manual Build

Alternatively, you can build manually:

```bash
mkdir build
cd build
cmake ..
make
```

The executable will be located at `build/MidiMaster2`.

## Running

```bash
./build/MidiMaster2
```

### Using the Application

1. **Select MIDI Output Port**: Choose the MIDI output port where you want to send MIDI clock and notes (e.g., IAC Driver Bus 1)
2. **Select MIDI Input Port**: Choose the MIDI input port to receive DAW sync messages (e.g., IAC Driver Bus 1)
3. **Set BPM**: Adjust the BPM value or let it sync automatically from incoming MIDI clock
4. **Start/Stop**: Use the Start/Stop button to control MIDI clock transmission
5. **Monitor**: View MIDI events and synchronization status in the log window

The application will:

- Automatically detect and list available MIDI ports
- **Auto-select IAC Driver ports** (recommended for lowest latency and best timing)
- **Filter out Network MIDI ports** to avoid UDP buffering issues that cause rushed/dragged notes
- Send MIDI clock signals at the specified BPM when running
- Receive and respond to MIDI Start, Stop, Continue, and Clock messages from your DAW
- Calculate and display BPM from incoming MIDI clock signals (updated every 24 clock ticks)
- Track song position via MIDI Song Position Pointer (SPP)
- Emit MIDI notes at whole note boundaries (every bar) with predictive timing

**Note on Note Emission:** The current implementation emits notes at whole note boundaries (every 4 quarter notes). This whole note emission system will be modified in future versions to support additional tempo emission options, including quarter notes, half notes, and other rhythmic subdivisions. The predictive timing mechanism (currently ~70ms advance) ensures notes arrive precisely on beat boundaries, and this timing system will be adapted to support the new emission patterns.

**Important**: Network MIDI ports (including "Network Loopback MIDI") are automatically filtered out because they use UDP sockets with buffering that can cause timing issues. **IAC Driver is recommended** for both input and output as it provides CoreMIDI-based communication with minimal latency.

## MIDI Loopback Setup on macOS

**IAC Driver is strongly recommended** for both input and output as it uses CoreMIDI (not UDP) which provides:

- ✅ Lowest latency
- ✅ Most reliable timing (no UDP buffering)
- ✅ No rushed/dragged notes
- ✅ Best synchronization with DAWs

To set up bidirectional MIDI communication between MidiMaster2 and your DAW:

1. Open "Audio MIDI Setup" (Applications > Utilities)
2. Window > Show MIDI Studio
3. Double-click on "IAC Driver"
4. Check "Device is online"
5. Create additional IAC Driver buses if needed (click "+" to add more buses)

**Note**: Network MIDI ports are automatically excluded to prevent timing issues. If you only see Network MIDI ports, configure IAC Driver in Audio MIDI Setup first.

### Configuration Examples

**Option 1: Single Bus (Bidirectional)**

- MidiMaster2 Output → IAC Driver Bus 1 → DAW MIDI Input
- DAW MIDI Output → IAC Driver Bus 1 → MidiMaster2 Input

**Option 2: Separate Buses (Recommended)**

- MidiMaster2 Output → IAC Driver Bus 1 → DAW MIDI Input
- DAW MIDI Output → IAC Driver Bus 2 → MidiMaster2 Input

### DAW Configuration

In your DAW:

1. Enable MIDI input from the selected IAC Driver bus
2. Enable MIDI output to the selected IAC Driver bus (if using bidirectional sync)
3. Configure your DAW to send MIDI clock (usually in Preferences > MIDI Sync)
4. Configure your DAW to receive MIDI clock from MidiMaster2

## Technical Details

### Architecture

The application uses a modular architecture with clear separation of concerns:

- **MidiEngine**: Handles all MIDI port management and communication. Uses RTMidi for both input and output, providing thread-safe message queuing and real-time MIDI processing.
- **SyncController**: Manages MIDI clock synchronization, BPM calculation, position tracking, and note emission. Handles both master mode (generating clock) and slave mode (syncing to DAW clock).

### MIDI Libraries

- **RTMidi** (v6.0.0): Used for both MIDI input and output. Provides low-latency, real-time MIDI communication with excellent performance on macOS via CoreMIDI backend. The codebase was refactored to use RTMidi for output instead of Drumstick to improve IAC Driver detection and reliability.
- **Drumstick** (v2.x): Still included in the build for MIDI message constants and definitions, but actual MIDI communication is handled entirely by RTMidi.

### MIDI Messages Supported

**Outgoing (from MidiMaster2):**

- MIDI Clock (0xF8)
- MIDI Start (0xFA)
- MIDI Stop (0xFC)
- MIDI Continue (0xFB)
- MIDI Song Position Pointer (0xF2)
- MIDI Note On/Off

**Note Emission:**

The application currently emits MIDI notes at whole note boundaries (every 4 quarter notes = every bar). Notes are sent with predictive timing (approximately 70ms advance) to compensate for MIDI output latency, ensuring they arrive at the DAW precisely when the boundary occurs.

**Note:** The whole note emission system will be modified in future versions to support more flexible tempo emission options, including quarter notes, half notes, and other rhythmic subdivisions. The current implementation serves as the foundation for this upcoming enhancement.

**Incoming (to MidiMaster2):**

- MIDI Clock (0xF8) - Used for BPM calculation and synchronization
- MIDI Start (0xFA) - Triggers sync start
- MIDI Stop (0xFC) - Triggers sync stop
- MIDI Continue (0xFB) - Triggers sync continue
- MIDI Song Position Pointer (0xF2) - Used for position tracking
