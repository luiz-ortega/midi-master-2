#ifndef SYNCCONTROLLER_H
#define SYNCCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <chrono>

class MidiEngine;

class SyncController : public QObject {
    Q_OBJECT

public:
    explicit SyncController(MidiEngine *engine, QObject *parent = nullptr);
    ~SyncController();

    bool isRunning() const;
    double currentBPM() const;
    
    int getIncomingClockCount() const;
    int getCurrentPositionBeats() const;
    double getCurrentPositionQuarterNotes() const;
    
    void setBPM(double bpm);
    void blockBPMUpdates(bool block);
    void blockTransportSync(bool block);

public slots:
    void start(bool sendStartCommand = true);
    void stop(bool sendStopCommand = true);
    void handleDAWStart();
    void handleDAWStop();
    void handleDAWContinue();
    void handleMIDIClock();
    void handleSongPositionPointer(int positionBeats, double positionQuarterNotes);

signals:
    void runningChanged(bool running);
    void bpmChanged(double bpm);
    void clockTick();
    void beatSent(int quarterNote);
    void positionChanged(int beats, double quarterNotes);

private:
    void updateSyncTimer();
    void checkAndEmitWholeNote(double positionQuarterNotes);

private slots:
    void onSyncTick();
    void syncBPMToDAW(int bpm);
    void updateBPMFromDAW(double bpm);

private:
    MidiEngine *m_engine;
    QTimer *m_syncTimer;
    
    // Thread-safe state
    mutable QMutex m_stateMutex;
    bool m_isRunning;
    double m_currentBPM;
    int m_clockCount;
    int m_incomingClockCount;
    bool m_bpmUpdateBlocked;
    bool m_transportSyncBlocked;
    
    // Position tracking
    int m_currentPositionBeats;
    double m_currentPositionQuarterNotes;
    int m_lastEmittedWholeNote; // Track last whole note position that triggered a note
    
    // MIDI note parameters
    int m_midiChannel;
    int m_midiNote;
    int m_midiVelocity;
    bool m_noteOn;
    
    // High-resolution BPM calculation from incoming clock
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::high_resolution_clock::time_point;
    static TimePoint s_lastClockTime;
    static int s_clockWindow;
    static const int CLOCKS_PER_QUARTER_NOTE = 24;
    static const int CLOCKS_PER_WHOLE_NOTE = 96; // 4 quarter notes = 1 bar
    
    // Predictive emission: emit this many milliseconds before boundary to compensate for latency
    // This maintains consistent timing compensation regardless of BPM
    static constexpr double EMISSION_ADVANCE_MS = 70.0; // 70ms advance time
    
    // Timing tracking for position sync
    TimePoint m_lastClockMessageTime;
    double m_predictedNextBoundaryQuarterNotes;
    bool m_boundaryPending;
    
    // Drift correction: track how many clocks we've seen in each whole note
    // This resets at each boundary to prevent cumulative timing errors
    int m_clocksSinceLastBoundary;
    
    // Start time tracking for elapsed time calculation
    TimePoint m_startTime;
};

#endif // SYNCCONTROLLER_H

