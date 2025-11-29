# SyncController Unit Tests

This directory contains unit tests for the `SyncController` class to ensure note emissions remain consistent and don't delay after many bars (e.g., after bar 10).

## Test Overview

The test suite verifies:

1. **Note Emission at Boundaries**: Notes are emitted at correct whole note boundaries (every 4 quarter notes = every bar)
2. **No Delays After Many Bars**: Ensures consistent timing even after 10+ bars
3. **Consistent Timing**: Verifies 4-quarter-note intervals remain consistent over 20+ bars
4. **Bar 10 Specific Test**: Explicitly tests that bar 10 boundary is hit correctly
5. **Bar 20 Specific Test**: Tests even further to ensure no drift occurs
6. **Boundary Detection Accuracy**: Tests precise boundary detection logic
7. **No Delays After Bar 10**: Key test that verifies emissions continue correctly after bar 10

## Building and Running Tests

### Build the test executable:

```bash
cd build
cmake ..
make SyncControllerTest
```

### Run the tests:

```bash
# Run directly
./SyncControllerTest

# Or via CTest
ctest -R SyncControllerTest -V
```

## Test Strategy

The tests simulate MIDI clock ticks by calling `handleMIDIClock()` repeatedly, advancing the position through many bars. They verify:

- The `beatSent` signal is emitted at the correct quarter note positions (0, 4, 8, 12, 16, ...)
- Intervals between emissions are consistently 4 quarter notes (1 bar)
- No boundaries are missed, even after many bars
- Position tracking remains accurate

## Key Musical Concepts

### 1. **Quarter Notes**

- A quarter note is a basic unit of musical time
- Think of it like a "beat" in a song
- At 120 BPM (beats per minute), each quarter note lasts 0.5 seconds

### 2. **Bars (Measures)**

- A bar typically contains 4 beats (4 quarter notes)
- So 1 bar = 4 quarter notes

### 3. **MIDI Clock Ticks**

- MIDI uses a clock system: **24 ticks per quarter note**
- This means: 1 quarter note = 24 clock ticks
- So: 1 bar (4 quarter notes) = 96 clock ticks

### 4. **Whole Note Boundaries**

- The `SyncController` emits notes every **4 quarter notes** (called a "whole note")
- This equals **1 bar** of music
- Boundaries occur at: 0, 4, 8, 12, 16, 20, 24... quarter notes

## Quick Reference: Test Values

### Basic Conversions

```
1 bar = 4 quarter notes
1 quarter note = 24 MIDI clock ticks
1 bar = 96 clock ticks (4 × 24)
1 boundary = 4 quarter notes = 96 clock ticks (4 × 24)
```

### Common Test Scenarios

#### Test: 3 Boundaries (12 quarter notes)

```
Boundaries: 0, 4, 8, 12 quarter notes
Clock ticks: 0, 96, 192, 288 ticks
Calculation: 3 boundaries × 96 ticks = 288 ticks
```

#### Test: 10 Bars (40 quarter notes)

```
Boundaries: 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40 quarter notes
Clock ticks: 0, 96, 192, 288, 384, 480, 576, 672, 768, 864, 960 ticks
Calculation: 10 bars × 96 ticks = 960 ticks
Expected emissions: 11 notes (one every 4 quarter notes = one per bar)
```

#### Test: 20 Bars (80 quarter notes)

```
Boundaries: 0, 4, 8, 12, ..., 80 quarter notes
Clock ticks: 0, 96, 192, 288, ..., 1920 ticks
Calculation: 20 bars × 96 ticks = 1920 ticks
Expected emissions: 21 notes
```

## Why These Specific Values?

### 120.0 BPM

- Common tempo for testing
- Easy to calculate with
- Represents a typical song speed

### 4 Quarter Notes (Boundary Interval)

- This is a "whole note" in music theory
- The `SyncController` is designed to emit notes at these boundaries (every bar)
- Testing ensures it never misses one

### 960 Ticks (10 Bars)

- A significant amount of music (10 bars)
- Tests if timing drifts over time
- If there's a bug, it often shows up after this many bars

## Visual Timeline Example

For the "10 bars" test, here's what happens:

```
Time:    0   4   8  12  16  20  24  28  32  36  40  (quarter notes)
Bars:    0   1   2   3   4   5   6   7   8   9  10
Ticks:   0  96 192 288 384 480 576 672 768 864 960
Notes:   ✓   ✓   ✓   ✓   ✓   ✓   ✓   ✓   ✓   ✓   ✓
         ↑   ↑   ↑   ↑   ↑   ↑   ↑   ↑   ↑   ↑   ↑
      Note 1 Note 2 Note 3 ... (one note per bar)
```

Each ✓ represents a note emission. The interval between each is exactly 4 quarter notes (96 ticks = 1 bar).

## Understanding the Test Code

### Setup and Cleanup Functions

```cpp
void SyncControllerTest::init() {
    m_syncController = new SyncController(nullptr, this);
}
```

**What this does:**

- Creates a new `SyncController` before each test
- Passes `nullptr` for the MIDI engine (we don't need real hardware for testing)
- `this` is the parent object (the test class itself)

**Why `nullptr`?**

- We're testing the logic, not the actual MIDI hardware
- The `SyncController` checks if the engine exists before using it, so this is safe

### Helper Function: `simulateClockTicks()`

```cpp
void SyncControllerTest::simulateClockTicks(int count, double bpm) {
    m_syncController->setBPM(bpm);
    if (!m_syncController->isRunning()) {
        m_syncController->start(false);
    }

    for (int i = 0; i < count; ++i) {
        m_syncController->handleMIDIClock();
        QTest::qWait(1);
    }
}
```

**What it does:**

- Simulates MIDI clock ticks by calling `handleMIDIClock()` repeatedly
- This advances the musical position forward

**Variables explained:**

- `count`: How many clock ticks to simulate
- `bpm`: Beats per minute (e.g., 120.0 = 120 beats per minute)
- `QTest::qWait(1)`: Waits 1 millisecond between ticks (optional, for realism)

**Why these values?**

- **120 BPM** is a common tempo (like a typical pop song)
- We simulate ticks because in real life, a DAW sends clock messages 24 times per quarter note

### Helper Function: `getEmittedQuarterNotePositions()`

```cpp
QList<int> SyncControllerTest::getEmittedQuarterNotePositions(QSignalSpy &beatSentSpy) {
    QList<int> positions;
    for (int i = 0; i < beatSentSpy.count(); ++i) {
        positions.append(beatSentSpy.at(i).at(0).toInt());
    }
    return positions;
}
```

**What it does:**

- Extracts the quarter note positions from the signal spy
- Returns a list like: `[0, 4, 8, 12, 16, 20]`

**What is `QSignalSpy`?**

- A Qt tool that "listens" to signals (like events)
- When `SyncController` emits `beatSent`, the spy records it
- We can check what positions were emitted

## Example Test: `testNoteEmissionAtBoundaries()`

Let's break this down step by step:

```cpp
void SyncControllerTest::testNoteEmissionAtBoundaries() {
    // Step 1: Create a spy to listen for note emissions
    QSignalSpy beatSentSpy(m_syncController, &SyncController::beatSent);

    // Step 2: Set tempo to 120 BPM and start
    m_syncController->setBPM(120.0);
    m_syncController->start(false);

    // Step 3: Simulate 288 clock ticks
    // Why 288? Let's calculate:
    // - Each boundary is at 4 quarter notes
    // - 4 quarter notes = 4 × 24 = 96 clock ticks
    // - We want to go through 3 boundaries = 3 × 96 = 288 ticks
    // - This takes us to 12 quarter notes (3 × 4)
    simulateClockTicks(288, 120.0);

    // Step 4: Check that we got at least 4 emissions
    // Expected: notes at 0, 4, 8, 12 quarter notes = 4 notes
    QVERIFY(beatSentSpy.count() >= 4);

    // Step 5: Verify the first note was at position 0
    int firstNote = beatSentSpy.at(0).at(0).toInt();
    QCOMPARE(firstNote, 0);

    // Step 6: Verify all notes are at correct boundaries
    QList<int> positions = getEmittedQuarterNotePositions(beatSentSpy);
    for (int i = 1; i < positions.size(); ++i) {
        int quarterNote = positions[i];
        QVERIFY(quarterNote % 4 == 0);  // Must be divisible by 4
        QCOMPARE(quarterNote, i * 4);   // Must be exactly i × 4
    }
}
```

**Why 288 ticks?**

- We want to test 3 boundaries (0, 4, 8, 12 quarter notes)
- Each boundary is 4 quarter notes apart
- 4 quarter notes × 24 ticks = 96 ticks per boundary
- 3 boundaries × 96 ticks = 288 ticks total

**What `QVERIFY` and `QCOMPARE` do:**

- `QVERIFY(condition)`: Fails the test if condition is false
- `QCOMPARE(actual, expected)`: Fails if values don't match exactly

## Example Test: `testNoDelaysAfterManyBars()`

This is a key test that checks for delays after 10 bars:

```cpp
void SyncControllerTest::testNoDelaysAfterManyBars() {
    QSignalSpy beatSentSpy(m_syncController, &SyncController::beatSent);

    m_syncController->setBPM(120.0);
    m_syncController->start(false);

    // Simulate 10 bars of music
    // 10 bars = 10 × 4 quarter notes = 40 quarter notes
    // 40 quarter notes × 24 ticks = 960 clock ticks
    simulateClockTicks(960, 120.0);

    // We should have 11 note emissions:
    // At positions: 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40 quarter notes
    // Why 11? Because boundaries occur every 4 quarter notes (every bar):
    // - 0 (start)
    // - 4 (bar 1)
    // - 8 (bar 2)
    // - ...
    // - 40 (bar 10)
    QVERIFY(beatSentSpy.count() >= 11);

    // Get all the positions where notes were emitted
    QList<int> emissionPositions = getEmittedQuarterNotePositions(beatSentSpy);

    // Verify each position is correct
    for (int i = 0; i < emissionPositions.size(); ++i) {
        int expectedPos = i * 4;  // 0, 4, 8, 12, 16, 20...
        QCOMPARE(emissionPositions[i], expectedPos);
    }

    // Specifically check that bar 10 (position 40) was hit
    QCOMPARE(emissionPositions[10], 40);
}
```

**Why 960 ticks?**

- 10 bars × 4 quarter notes per bar = 40 quarter notes
- 40 quarter notes × 24 ticks per quarter note = 960 ticks

**Why check for 11 emissions?**

- Boundaries occur every 4 quarter notes (every bar)
- At 40 quarter notes (bar 10), we've crossed 10 boundaries (4, 8, 12, ..., 40)
- Plus the initial emission at 0 = 11 total emissions

## Example Test: `testNoDelaysAfterBar10()`

This is the most important test - it specifically checks for delays after bar 10:

```cpp
void SyncControllerTest::testNoDelaysAfterBar10() {
    QSignalSpy beatSentSpy(m_syncController, &SyncController::beatSent);

    m_syncController->setBPM(120.0);
    m_syncController->start(false);

    // Step 1: Advance to bar 10 (40 quarter notes = 960 ticks)
    simulateClockTicks(960, 120.0);

    // Step 2: Record what we have so far
    QList<int> positionsBeforeBar10 = getEmittedQuarterNotePositions(beatSentSpy);
    int countBeforeBar10 = positionsBeforeBar10.size();

    // Step 3: Continue playing to bar 15 (60 quarter notes total)
    // We need 20 more quarter notes = 480 more ticks
    simulateClockTicks(480, 120.0);

    // Step 4: Check what we have now
    QList<int> positionsAfterBar10 = getEmittedQuarterNotePositions(beatSentSpy);
    int countAfterBar10 = positionsAfterBar10.size();

    // Step 5: Verify we got MORE emissions (didn't stop!)
    QVERIFY(countAfterBar10 > countBeforeBar10);

    // Step 6: Verify intervals are still 4 quarter notes (no delays)
    for (int i = 1; i < positionsAfterBar10.size(); ++i) {
        int interval = positionsAfterBar10[i] - positionsAfterBar10[i-1];
        QCOMPARE(interval, 4);  // Must be exactly 4, not 5 or 6!
    }

    // Step 7: Verify specific boundaries after bar 10
    // After bar 10 (40), next boundaries are at 44, 48, 52, 56, 60...
    QVERIFY(positionsAfterBar10.contains(44));  // Bar 11
    QVERIFY(positionsAfterBar10.contains(48));  // Bar 12

    // Step 8: Critical check - no gap after bar 10
    int lastBeforeBar10 = positionsBeforeBar10.last();  // Should be 40
    QCOMPARE(lastBeforeBar10, 40);

    // Find first emission after bar 10
    int firstAfterBar10 = -1;
    for (int pos : positionsAfterBar10) {
        if (pos > 40) {
            firstAfterBar10 = pos;
            break;
        }
    }
    // This MUST be 44, not 45 or 46 (which would indicate a delay)
    QCOMPARE(firstAfterBar10, 44);
}
```

**Why this test matters:**

- If there's a bug causing delays, the next emission might be at 45 or 46 instead of 44
- This test catches that by checking the interval is exactly 4

**Why 480 more ticks?**

- We're going from bar 10 (40 quarter notes) to bar 15 (60 quarter notes)
- That's 20 more quarter notes
- 20 × 24 ticks = 480 ticks

## What Could Go Wrong?

### Problem 1: Missing Boundary

```
Expected: 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40
Actual:   0, 4, 8, 12, 16, 20, 24, 28, 32, 36     (missing 40!)
```

**Test catches this:** `QVERIFY(beatSentSpy.count() >= 11)` would fail

### Problem 2: Delayed Emission

```
Expected: 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44
Actual:   0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 46  (44 is delayed to 46!)
```

**Test catches this:** `QCOMPARE(firstAfterBar10, 44)` would fail

### Problem 3: Inconsistent Intervals

```
Expected intervals: 4, 4, 4, 4, 4, 4
Actual intervals:  4, 4, 4, 4, 4, 5  (last one is wrong!)
```

**Test catches this:** `QCOMPARE(interval, 4)` would fail

## Key Assertions Explained

| Assertion               | What It Checks              | Why It Matters              |
| ----------------------- | --------------------------- | --------------------------- |
| `QVERIFY(count >= 11)`  | At least 11 notes emitted   | Catches missing notes       |
| `QCOMPARE(pos, 40)`     | Note at exactly position 40 | Ensures bar 10 boundary hit |
| `QCOMPARE(interval, 4)` | Interval is exactly 4       | Catches timing drift        |
| `QVERIFY(pos % 4 == 0)` | Position divisible by 4     | Ensures correct boundaries  |

## Reading Test Code: Pattern Recognition

Most tests follow this pattern:

1. **Setup**: Create spy, set BPM, start controller
2. **Simulate**: Call `simulateClockTicks()` with calculated value
3. **Verify Count**: Check we got expected number of emissions
4. **Verify Positions**: Check each position is correct
5. **Verify Intervals**: Check spacing is consistent

Once you recognize this pattern, reading tests becomes much easier!

## Common Values Reference

| Value     | Meaning                      | Calculation             |
| --------- | ---------------------------- | ----------------------- |
| **24**    | Clock ticks per quarter note | MIDI standard           |
| **4**     | Quarter notes per boundary   | Whole note (1 bar)      |
| **96**    | Clock ticks per boundary     | 4 × 24                  |
| **4**     | Quarter notes per bar        | Standard time signature |
| **96**    | Clock ticks per bar          | 4 × 24                  |
| **120.0** | BPM (beats per minute)       | Common test tempo       |
| **960**   | Ticks for 10 bars            | 10 bars × 96 ticks      |
| **1920**  | Ticks for 20 bars            | 20 bars × 96 ticks      |

## How to Read Test Results

When you run the tests, you'll see output like:

```
PASS   : SyncControllerTest::testNoteEmissionAtBoundaries()
PASS   : SyncControllerTest::testNoDelaysAfterManyBars()
PASS   : SyncControllerTest::testNoDelaysAfterBar10()
```

**PASS** = The test succeeded - everything worked correctly!

**FAIL** = Something is wrong. The test will tell you:

- Which assertion failed
- What value was expected vs. what was received

For example:

```
FAIL!  : SyncControllerTest::testNoDelaysAfterBar10()
   Actual   (firstAfterBar10) : 46
   Expected (firstAfterBar10) : 44
```

This would mean there's a 2-quarter-note delay after bar 10 - a bug!

## Notes

- The tests use `nullptr` for `MidiEngine` since `SyncController` checks for nullptr before calling engine methods
- This allows testing the synchronization logic without requiring actual MIDI hardware
- The tests verify signal emissions and position tracking, which are the critical aspects for ensuring no delays
- The whole note emission system (every 4 quarter notes) will be modified in future versions to support more flexible tempo emission options

## Summary

1. **Tests simulate MIDI clock ticks** to advance through music
2. **We check that notes emit at boundaries** (every 4 quarter notes = every bar)
3. **We verify no delays occur** by checking intervals are exactly 4
4. **Specific values are used** because they represent real musical timing
5. **The tests catch bugs** where timing drifts or delays occur after many bars

The key insight: If the code is working correctly, the interval between note emissions should ALWAYS be exactly 4 quarter notes (1 bar), no matter how many bars have passed!
