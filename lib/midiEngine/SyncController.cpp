#include "SyncController.h"
#include "MidiEngine.h"
#include <drumstick/rtmidioutput.h>
#include <QDateTime>
#include <QtMath>
#include <QMutexLocker>

// Initialize static members for high-resolution timing
SyncController::TimePoint SyncController::s_lastClockTime = SyncController::Clock::now();
int SyncController::s_clockWindow = 0;

SyncController::SyncController(MidiEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_syncTimer(nullptr)
    , m_isRunning(false)
    , m_currentBPM(120.0)
    , m_clockCount(0)
    , m_incomingClockCount(0)
    , m_bpmUpdateBlocked(false)
    , m_transportSyncBlocked(false)
    , m_currentPositionBeats(0)
    , m_currentPositionQuarterNotes(0.0)
    , m_lastEmittedWholeNote(-1)
    , m_midiChannel(0)
    , m_midiNote(60)
    , m_midiVelocity(100)
    , m_noteOn(false)
    , m_lastClockMessageTime(Clock::now())
    , m_predictedNextBoundaryQuarterNotes(-1.0)
    , m_boundaryPending(false)
    , m_clocksSinceLastBoundary(0)
    , m_startTime(Clock::now())
{
    // Reset static clock timing
    s_lastClockTime = Clock::now();
    s_clockWindow = 0;
}

SyncController::~SyncController() {
    stop(false);
}

bool SyncController::isRunning() const {
    QMutexLocker locker(&m_stateMutex);
    return m_isRunning;
}

double SyncController::currentBPM() const {
    QMutexLocker locker(&m_stateMutex);
    return m_currentBPM;
}

int SyncController::getIncomingClockCount() const {
    QMutexLocker locker(&m_stateMutex);
    return m_incomingClockCount;
}

int SyncController::getCurrentPositionBeats() const {
    QMutexLocker locker(&m_stateMutex);
    return m_currentPositionBeats;
}

double SyncController::getCurrentPositionQuarterNotes() const {
    QMutexLocker locker(&m_stateMutex);
    return m_currentPositionQuarterNotes;
}

void SyncController::blockBPMUpdates(bool block) {
    QMutexLocker locker(&m_stateMutex);
    m_bpmUpdateBlocked = block;
}

void SyncController::blockTransportSync(bool block) {
    QMutexLocker locker(&m_stateMutex);
    m_transportSyncBlocked = block;
}

void SyncController::setBPM(double bpm) {
    if (bpm >= 20 && bpm <= 300) {
        {
            QMutexLocker locker(&m_stateMutex);
            m_currentBPM = bpm;
        }
        if (m_syncTimer) {
            updateSyncTimer();
        }
        emit bpmChanged(bpm);
    }
}

void SyncController::start(bool sendStartCommand) {
    {
        QMutexLocker locker(&m_stateMutex);
        if (m_syncTimer == nullptr) {
            m_syncTimer = new QTimer(this);
            connect(m_syncTimer, &QTimer::timeout, this, &SyncController::onSyncTick);
        }
        
        m_isRunning = true;
        m_startTime = Clock::now(); // Reset start time when starting playback
    }
    
    updateSyncTimer();
    m_syncTimer->start();
    
    if (sendStartCommand && m_engine) {
        m_engine->sendSystemMessage(drumstick::rt::MIDI_REALTIME_START);
    }
    
    emit runningChanged(true);
}

void SyncController::stop(bool sendStopCommand) {
    bool noteWasOn = false;
    
    {
        QMutexLocker locker(&m_stateMutex);
        if (m_syncTimer) {
            m_syncTimer->stop();
        }
        
        noteWasOn = m_noteOn;
        m_clockCount = 0;
        m_isRunning = false;
        m_currentPositionBeats = 0;
        m_currentPositionQuarterNotes = 0.0;
        m_lastEmittedWholeNote = -1;
        m_predictedNextBoundaryQuarterNotes = -1.0;
        m_boundaryPending = false;
        m_clocksSinceLastBoundary = 0;
    }
    
    if (m_engine) {
        // Send note off if note is still on
        if (noteWasOn) {
            m_engine->sendNoteOff(m_midiChannel, m_midiNote, 0);
            {
                QMutexLocker locker(&m_stateMutex);
                m_noteOn = false;
            }
        }
        
        if (sendStopCommand) {
            m_engine->sendSystemMessage(drumstick::rt::MIDI_REALTIME_STOP);
        }
    }
    
    emit runningChanged(false);
    emit positionChanged(0, 0.0);
}

void SyncController::handleDAWStart() {
    QMutexLocker locker(&m_stateMutex);
    if (!m_transportSyncBlocked && !m_isRunning) {
        m_transportSyncBlocked = true;
        m_clockCount = 0;
        m_currentPositionBeats = 0;
        m_currentPositionQuarterNotes = 0.0;
        m_lastEmittedWholeNote = -1; // Reset to allow first whole note to emit
        m_predictedNextBoundaryQuarterNotes = 0.0; // First boundary is at 0
        m_boundaryPending = false;
        m_clocksSinceLastBoundary = 0;
        // Reset static clock timing for fresh BPM calculation
        s_lastClockTime = Clock::now();
        s_clockWindow = 0;
        m_lastClockMessageTime = Clock::now();
        m_startTime = Clock::now(); // Reset start time
        
        qDebug() << "DAW START - m_lastEmittedWholeNote initialized to -1";
        
        // When syncing to DAW, just set running state but DON'T start internal timer
        // The DAW will provide clock ticks via handleMIDIClock()
        m_isRunning = true;
        locker.unlock();
        
        // Don't send START command back, and don't start internal timer (we're in slave mode)
        emit runningChanged(true);
        
        locker.relock();
        m_transportSyncBlocked = false;
    }
}

void SyncController::handleDAWStop() {
    QMutexLocker locker(&m_stateMutex);
    if (!m_transportSyncBlocked && m_isRunning) {
        m_transportSyncBlocked = true;
        locker.unlock();
        stop(false); // Don't send STOP command back
        locker.relock();
        m_transportSyncBlocked = false;
    }
}

void SyncController::handleDAWContinue() {
    QMutexLocker locker(&m_stateMutex);
    if (!m_transportSyncBlocked && !m_isRunning) {
        m_transportSyncBlocked = true;
        // Reset last emitted whole note based on current position
        // This ensures we don't re-emit for the current whole note
        int currentWholeNote = static_cast<int>(m_currentPositionQuarterNotes / 4.0);
        m_lastEmittedWholeNote = currentWholeNote - 1; // Set to previous so next emits correctly
        m_predictedNextBoundaryQuarterNotes = currentWholeNote * 4.0;
        m_boundaryPending = false;
        
        // Calculate clocks since the last boundary based on current position
        double positionInCurrentBoundary = m_currentPositionQuarterNotes - (currentWholeNote * 4.0);
        m_clocksSinceLastBoundary = static_cast<int>(positionInCurrentBoundary * CLOCKS_PER_QUARTER_NOTE);
        
        // Reset static clock timing for fresh BPM calculation
        s_lastClockTime = Clock::now();
        s_clockWindow = 0;
        m_lastClockMessageTime = Clock::now();
        m_startTime = Clock::now(); // Reset start time
        
        // When syncing to DAW, just set running state but DON'T start internal timer
        // The DAW will provide clock ticks via handleMIDIClock()
        m_isRunning = true;
        locker.unlock();
        
        // Don't send CONTINUE command back, and don't start internal timer (we're in slave mode)
        emit runningChanged(true);
        
        locker.relock();
        m_transportSyncBlocked = false;
    }
}

void SyncController::handleSongPositionPointer(int positionBeats, double positionQuarterNotes) {
    bool shouldEmitNote = false;
    bool isRunning = false;
    double previousPosition = 0.0;
    
    {
        QMutexLocker locker(&m_stateMutex);
        if (positionBeats >= 0) {
            previousPosition = m_currentPositionQuarterNotes;
            m_currentPositionBeats = positionBeats;
            m_currentPositionQuarterNotes = positionQuarterNotes;
            
            // Update clock count based on position (position is in 16th notes, convert to clock ticks)
            // Each beat = 24 clock ticks, positionBeats is in 16th notes (6 per quarter note)
            // So: clockCount = (positionBeats / 6) * 24 = positionBeats * 4
            m_clockCount = positionBeats * 4;
            
            // CRITICAL FIX: Only update m_lastEmittedWholeNote if we're NOT running
            // or if we're seeking BACKWARDS. During normal playback, let the clock-based
            // boundary detection handle emission - don't let SPP messages interfere!
            bool isSeekingBackwards = (positionQuarterNotes < previousPosition - 0.5);
            
            // Also check if we're at the very start (position near 0) - this handles both
            // initial start and loop backs
            bool isAtStart = (positionQuarterNotes < 0.5);
            
            if (!m_isRunning || isSeekingBackwards) {
                // Recalculate last emitted whole note to prevent duplicate emissions
                // This only runs when:
                // 1. Not running (stopped) - safe to update positioning
                // 2. Seeking backwards (rewind/loop) - prevent duplicate emissions
                // NOTE: We DON'T update during normal playback - let clock-based detection handle it
                int currentWholeNote = static_cast<int>(positionQuarterNotes / 4.0);
                double fractionIntoBoundary = positionQuarterNotes - (currentWholeNote * 4.0);
                
                // If we're in the first half of a boundary, consider it not yet emitted
                if (fractionIntoBoundary < 2.0) {
                    m_lastEmittedWholeNote = currentWholeNote - 1;
                } else {
                    // We're more than halfway through the boundary, so mark it as already emitted
                    m_lastEmittedWholeNote = currentWholeNote;
                }
                
                qDebug() << "SPP UPDATE - m_lastEmittedWholeNote set to:" << m_lastEmittedWholeNote 
                         << "| Position QN:" << QString::number(positionQuarterNotes, 'f', 2)
                         << "| ClockCount:" << m_clockCount
                         << "| Running:" << m_isRunning
                         << "| SeekingBack:" << isSeekingBackwards;
                
                m_predictedNextBoundaryQuarterNotes = (m_lastEmittedWholeNote + 1) * 4.0;
            }
            
            // Recalculate clocks since boundary based on DAW's position
            // This corrects any accumulated drift
            int currentWholeNote = static_cast<int>(positionQuarterNotes / 4.0);
            double fractionIntoBoundary = positionQuarterNotes - (currentWholeNote * 4.0);
            m_clocksSinceLastBoundary = static_cast<int>(fractionIntoBoundary * CLOCKS_PER_QUARTER_NOTE);
            
            isRunning = m_isRunning;
        }
    }
    
    // Check for whole note boundary and emit immediately if crossed
    // This happens OUTSIDE the mutex for minimal latency
    if (isRunning && positionQuarterNotes >= 0) {
        checkAndEmitWholeNote(positionQuarterNotes);
    }
    
    emit positionChanged(positionBeats, positionQuarterNotes);
}

void SyncController::handleMIDIClock() {
    static int clockHandlerCount = 0;
    ++clockHandlerCount;
    
    TimePoint currentTime = Clock::now();
    bool isRunning = false;
    double positionQuarterNotes = 0.0;
    
    // CRITICAL PATH: Update position from clock with minimal mutex hold
    {
        QMutexLocker locker(&m_stateMutex);
        m_incomingClockCount++;
        m_lastClockMessageTime = currentTime;
        isRunning = m_isRunning;
        
        // Update clock count and position based on incoming clock (when running)
        // Position is calculated from clock count: each clock tick = 1/24 quarter note
        if (isRunning) {
            m_clockCount++;
            m_clocksSinceLastBoundary++; // Track clocks for drift detection
            
            // Update position from clock count (24 ticks per quarter note)
            positionQuarterNotes = static_cast<double>(m_clockCount) / CLOCKS_PER_QUARTER_NOTE;
            m_currentPositionQuarterNotes = positionQuarterNotes;
            // Convert quarter notes to beats (4 beats per quarter note)
            m_currentPositionBeats = static_cast<int>(positionQuarterNotes * 4);
        }
    } // Release mutex ASAP - critical timing work is done
    
    // Check for whole note boundary using ACTUAL position (no look-ahead)
    // Look-ahead will be applied INSIDE checkAndEmitWholeNote for emission timing only
    // This prevents cumulative drift from early boundary detection
    if (isRunning && m_engine) {
        checkAndEmitWholeNote(positionQuarterNotes);
    }
    
    // NON-CRITICAL PATH: BPM calculation (deferred - happens after note emission)
    // Only track timing, actual calculation happens after critical path
    bool needsBPMCalc = false;
    TimePoint lastClockTimeForBPM;
    
    {
        // Minimal mutex hold for BPM window tracking
        QMutexLocker locker(&m_stateMutex);
        if (s_clockWindow == 0) {
            s_lastClockTime = currentTime;
            s_clockWindow = CLOCKS_PER_QUARTER_NOTE;
        }
        s_clockWindow--;
        if (s_clockWindow == 0) {
            needsBPMCalc = true;
            lastClockTimeForBPM = s_lastClockTime;
        }
    }
    
    // Do BPM calculation AFTER note emission (non-blocking)
    if (needsBPMCalc) {
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            currentTime - lastClockTimeForBPM);
        double elapsedSeconds = elapsed.count() / 1000000.0;
        
        if (elapsedSeconds > 0.2 && elapsedSeconds < 3.0) {
            double calculatedBPM = 60.0 / elapsedSeconds;
            {
                QMutexLocker locker(&m_stateMutex);
                if (calculatedBPM >= 20 && calculatedBPM <= 300 && 
                    qAbs(calculatedBPM - m_currentBPM) > 0.5 && !m_bpmUpdateBlocked) {
                    m_currentBPM = calculatedBPM;
                    if (m_syncTimer) {
                        updateSyncTimer();
                    }
                }
                s_lastClockTime = currentTime;
                s_clockWindow = CLOCKS_PER_QUARTER_NOTE;
            }
        } else {
            QMutexLocker locker(&m_stateMutex);
            s_lastClockTime = currentTime;
            s_clockWindow = CLOCKS_PER_QUARTER_NOTE;
        }
    }
    
    emit clockTick();
}

void SyncController::checkAndEmitWholeNote(double positionQuarterNotes) {
    // CRITICAL: Simplified, drift-free boundary detection with predictive emission
    // Strategy: Emit when we're within a BPM-adjusted advance time of a boundary (BEFORE crossing it)
    
    // Which whole note period are we currently in?
    int currentBoundary = static_cast<int>(qFloor(positionQuarterNotes / 4.0));
    
    // Calculate position relative to current boundary
    double positionInBoundary = positionQuarterNotes - (currentBoundary * 4.0);
    double ticksIntoBoundary = positionInBoundary * CLOCKS_PER_QUARTER_NOTE;
    
    // Calculate how many ticks until the NEXT boundary
    double ticksToNextBoundary = (4.0 - positionInBoundary) * CLOCKS_PER_QUARTER_NOTE;
    
    // The next boundary we'll cross
    int nextBoundary = currentBoundary + 1;
    
    bool shouldEmit = false;
    bool noteWasOn = false;
    int quarterNoteCount = 0;
    int positionBeats = 0;
    double currentPositionQuarterNotes = 0.0;
    int lastEmitted = 0;
    double bpm = 120.0;
    
    {
        QMutexLocker locker(&m_stateMutex);
        
        // EMIT STRATEGY: Emit BEFORE crossing into next boundary
        // Calculate BPM-adjusted advance time to maintain consistent millisecond offset
        // At 120 BPM: 1 tick = 20.83ms, so 50ms = ~2.4 ticks
        // At 60 BPM: 1 tick = 41.67ms, so 50ms = ~1.2 ticks
        // At 240 BPM: 1 tick = 10.42ms, so 50ms = ~4.8 ticks
        
        bpm = m_currentBPM;
        
        // Safety check: ensure BPM is valid
        if (bpm < 20.0 || bpm > 300.0) {
            bpm = 120.0;
        }
        
        double msPerTick = (60000.0 / bpm) / CLOCKS_PER_QUARTER_NOTE; // milliseconds per tick
        double emissionAdvanceTicks = EMISSION_ADVANCE_MS / msPerTick; // convert ms to ticks
        
        // Ensure minimum advance window to handle timing jitter
        if (emissionAdvanceTicks < 1.5) {
            emissionAdvanceTicks = 1.5;
        }
        
        lastEmitted = m_lastEmittedWholeNote;
        
        // Special case: Emit boundary 0 (absolute first beat) immediately at tick 1
        // This gives the first downbeat of the song
        bool isFirstDownbeat = (currentBoundary == 0 && m_lastEmittedWholeNote < 0 && positionQuarterNotes < 1.0);
        
        // Check: Should we emit for the NEXT boundary?
        // Emit when we're within BPM-adjusted advance time of next boundary
        // OR if this is the first downbeat (boundary 0)
        if (isFirstDownbeat || 
            (nextBoundary > m_lastEmittedWholeNote && ticksToNextBoundary <= emissionAdvanceTicks)) {
            shouldEmit = true;
            
            // Mark the boundary we're emitting for
            int boundaryToEmit = isFirstDownbeat ? currentBoundary : nextBoundary;
            m_lastEmittedWholeNote = boundaryToEmit;
            noteWasOn = m_noteOn;
            quarterNoteCount = boundaryToEmit * 4;
            positionBeats = m_currentPositionBeats;
            currentPositionQuarterNotes = m_currentPositionQuarterNotes;
            
            // Store the predicted next boundary (whole note = 4 quarter notes)
            m_predictedNextBoundaryQuarterNotes = (boundaryToEmit + 1) * 4.0;
            
            // DRIFT CORRECTION: Reset clock counter
            m_clocksSinceLastBoundary = 0;
        }
        
    }
    
    // Check if we need to send NOTE OFF when crossing into a boundary
    // Send NOTE OFF when we CROSS the boundary (not early) so notes sustain full whole note duration
    // This happens OUTSIDE the mutex after all emission checks
    bool noteShouldBeOff = false;
    int currentTickForNoteOff = 0;
    {
        QMutexLocker locker(&m_stateMutex);
        // Check if we've crossed into a new boundary and a note is still on
        // We check if we're just past a whole note boundary (position >= boundary * 4.0)
        // and within the first 10% of that boundary to catch it exactly at the boundary
        double positionInCurrentBoundary = positionQuarterNotes - (currentBoundary * 4.0);
        if (m_noteOn && positionInCurrentBoundary > 0.0 && positionInCurrentBoundary < 0.4) {
            // We've just crossed into a boundary - turn off the previous note
            noteShouldBeOff = true;
            currentTickForNoteOff = m_clockCount;
            m_noteOn = false;
        }
    }
    
    if (noteShouldBeOff && m_engine) {
        m_engine->sendNoteOff(m_midiChannel, m_midiNote, 0);
        qDebug() << "NOTE OFF - Tick:" << currentTickForNoteOff 
                 << "| QN:" << QString::number(positionQuarterNotes, 'f', 2) 
                 << "| Boundary:" << currentBoundary;
    }
    
    // Debug logging every 24 clocks (once per quarter note) around boundaries
    static int debugCounter = 0;
    debugCounter++;
    if (debugCounter % 24 == 0 || ticksToNextBoundary <= 10.0) {
        int currentTick = 0;
        double msPerTick = (60000.0 / bpm) / CLOCKS_PER_QUARTER_NOTE;
        double emissionAdvanceTicks = EMISSION_ADVANCE_MS / msPerTick;
        {
            QMutexLocker locker(&m_stateMutex);
            currentTick = m_clockCount;
        }
        qDebug() << "BOUNDARY CHECK - Tick:" << currentTick 
                 << "| QN:" << QString::number(positionQuarterNotes, 'f', 2)
                 << "| CurrentBoundary:" << currentBoundary 
                 << "| NextBoundary:" << nextBoundary
                 << "| LastEmitted:" << lastEmitted
                 << "| TicksToNext:" << QString::number(ticksToNextBoundary, 'f', 3)
                 << "| AdvTicks:" << QString::number(emissionAdvanceTicks, 'f', 1)
                 << "| BPM:" << QString::number(bpm, 'f', 0)
                 << "| ShouldEmit:" << (shouldEmit ? "YES" : "NO");
    }
    
    // Emit note immediately
    // This happens OUTSIDE the mutex for minimal latency
    if (shouldEmit && m_engine) {
        // Send note ON for whole note boundary - will arrive at output just as boundary occurs
        // The note will sustain until the next boundary
        // Note: We don't send note-off here - it will be sent at the exact next boundary
        // to ensure full whole note duration
        m_engine->sendNoteOn(m_midiChannel, m_midiNote, m_midiVelocity);
        
        // Calculate time elapsed and log note emission
        int currentTick = 0;
        double bpm = 0.0;
        double elapsedSeconds = 0.0;
        {
            QMutexLocker locker(&m_stateMutex);
            m_noteOn = true;
            currentTick = m_clockCount;
            bpm = m_currentBPM;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - m_startTime);
            elapsedSeconds = elapsed.count() / 1000.0;
        }
        
        // Log note emission with current tick, time elapsed, and BPM
        qDebug() << "NOTE EMITTED - Tick:" << currentTick 
                 << "| Elapsed:" << QString::number(elapsedSeconds, 'f', 3) << "s"
                 << "| BPM:" << QString::number(bpm, 'f', 2);
        
        emit beatSent(quarterNoteCount);
        emit positionChanged(positionBeats, currentPositionQuarterNotes);
    }
}

void SyncController::updateBPMFromDAW(double bpm) {
    if (bpm >= 20 && bpm <= 300) {
        bool shouldUpdate = false;
        {
            QMutexLocker locker(&m_stateMutex);
            if (!m_bpmUpdateBlocked && qAbs(bpm - m_currentBPM) > 0.1) {
                m_currentBPM = bpm;
                shouldUpdate = true;
            }
        }
        
        if (shouldUpdate) {
            if (m_syncTimer) {
                updateSyncTimer();
            }
            emit bpmChanged(bpm);
        }
    }
}

void SyncController::syncBPMToDAW(int bpm) {
    emit bpmChanged(bpm);
}

void SyncController::updateSyncTimer() {
    if (!m_syncTimer) return;
    
    // MIDI Clock: 24 ticks per quarter note
    // Interval in ms = (60 / BPM / 24) * 1000
    double intervalMs = (60.0 / m_currentBPM / 24.0) * 1000.0;
    m_syncTimer->setInterval(static_cast<int>(intervalMs));
}

void SyncController::onSyncTick() {
    bool isRunning = false;
    double positionQuarterNotes = 0.0;
    
    {
        QMutexLocker locker(&m_stateMutex);
        isRunning = m_isRunning;
        if (isRunning) {
            m_clockCount++;
            // Update position from clock count (24 ticks per quarter note)
            positionQuarterNotes = static_cast<double>(m_clockCount) / CLOCKS_PER_QUARTER_NOTE;
            m_currentPositionQuarterNotes = positionQuarterNotes;
            // Convert quarter notes to beats (4 beats per quarter note)
            m_currentPositionBeats = static_cast<int>(positionQuarterNotes * 4);
        }
    }
    
    if (!isRunning || !m_engine) return;
    
    // Send MIDI clock when running from internal timer (master mode)
    // When syncing to DAW, clock comes from incoming messages
    m_engine->sendSystemMessage(drumstick::rt::MIDI_REALTIME_CLOCK);
    
    // Check for whole note boundary and emit immediately if crossed
    // Uses same position-based approach as handleMIDIClock for consistency
    checkAndEmitWholeNote(positionQuarterNotes);
}


