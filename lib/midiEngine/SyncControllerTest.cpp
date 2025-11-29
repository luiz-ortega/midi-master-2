#include "SyncControllerTest.h"
#include <QSignalSpy>
#include <QDebug>
#include <QtMath>

void SyncControllerTest::initTestCase() {
    // Setup before all tests
}

void SyncControllerTest::cleanupTestCase() {
    // Cleanup after all tests
}

void SyncControllerTest::init() {
    // Pass nullptr for MidiEngine - SyncController checks for nullptr before using it
    // This allows us to test the logic without needing a real MIDI engine
    m_syncController = new SyncController(nullptr, this);
}

void SyncControllerTest::cleanup() {
    if (m_syncController) {
        m_syncController->stop(false);
        delete m_syncController;
        m_syncController = nullptr;
    }
}

void SyncControllerTest::simulateClockTicks(int count, double bpm) {
    m_syncController->setBPM(bpm);
    // Only start if not already running
    if (!m_syncController->isRunning()) {
        m_syncController->start(false);
    }
    
    for (int i = 0; i < count; ++i) {
        m_syncController->handleMIDIClock();
        // Small delay to simulate real timing (optional, can be removed for faster tests)
        QTest::qWait(1);
    }
}

void SyncControllerTest::simulateClockTicksToPosition(double targetQuarterNotes, double bpm) {
    m_syncController->setBPM(bpm);
    // Only start if not already running
    if (!m_syncController->isRunning()) {
        m_syncController->start(false);
    }
    
    // Calculate how many clock ticks we need (24 ticks per quarter note)
    // But we need to account for current position
    double currentPos = m_syncController->getCurrentPositionQuarterNotes();
    double remainingQuarterNotes = targetQuarterNotes - currentPos;
    if (remainingQuarterNotes <= 0) {
        return; // Already past target
    }
    
    int targetTicks = static_cast<int>(remainingQuarterNotes * 24.0);
    
    for (int i = 0; i < targetTicks; ++i) {
        m_syncController->handleMIDIClock();
    }
}

QList<int> SyncControllerTest::getEmittedQuarterNotePositions(QSignalSpy &beatSentSpy) {
    QList<int> positions;
    for (int i = 0; i < beatSentSpy.count(); ++i) {
        positions.append(beatSentSpy.at(i).at(0).toInt());
    }
    return positions;
}

void SyncControllerTest::testNoteEmissionAtBoundaries() {
    QSignalSpy beatSentSpy(m_syncController, &SyncController::beatSent);
    
    m_syncController->setBPM(120.0);
    m_syncController->start(false);
    
    // Simulate enough clocks to get past several boundaries
    // Each boundary is at 4 quarter notes (96 clock ticks) = 1 bar
    // Let's go through 3 boundaries = 12 quarter notes = 288 ticks
    simulateClockTicks(288, 120.0);
    
    // Should have emitted notes at: 0, 4, 8, 12 quarter notes
    QVERIFY(beatSentSpy.count() >= 4);
    
    // Verify first note at position 0
    QVERIFY(beatSentSpy.count() > 0);
    int firstNote = beatSentSpy.at(0).at(0).toInt();
    QCOMPARE(firstNote, 0);
    
    // Verify subsequent notes are at 4-quarter-note intervals (whole note boundaries)
    QList<int> positions = getEmittedQuarterNotePositions(beatSentSpy);
    for (int i = 1; i < positions.size(); ++i) {
        int quarterNote = positions[i];
        QVERIFY(quarterNote % 4 == 0);
        QCOMPARE(quarterNote, i * 4);
    }
}

void SyncControllerTest::testNoDelaysAfterManyBars() {
    QSignalSpy beatSentSpy(m_syncController, &SyncController::beatSent);
    
    m_syncController->setBPM(120.0);
    m_syncController->start(false);
    
    // Simulate 10 bars = 10 * 4 quarter notes = 40 quarter notes
    // Whole note boundary is every 4 quarter notes = 1 bar
    // So 10 bars = 10 whole note boundaries (excluding start)
    // Total boundaries including start: 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40 quarter notes = 11 notes
    
    // Let's go to 10 bars = 40 quarter notes = 960 clock ticks
    simulateClockTicks(960, 120.0);
    
    // Should have emitted notes at boundaries: 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40
    QVERIFY(beatSentSpy.count() >= 11);
    
    // Verify timing consistency - check intervals between emissions
    QList<int> emissionPositions = getEmittedQuarterNotePositions(beatSentSpy);
    
    // Verify all emissions are at correct boundaries
    for (int i = 0; i < emissionPositions.size(); ++i) {
        int expectedPos = i * 4;
        QCOMPARE(emissionPositions[i], expectedPos);
    }
    
    // Verify no missing boundaries - should have at least 11 emissions
    QVERIFY(emissionPositions.size() >= 11);
    QCOMPARE(emissionPositions[10], 40); // 11th emission at bar 10
}

void SyncControllerTest::testConsistentTimingOverManyBars() {
    QSignalSpy beatSentSpy(m_syncController, &SyncController::beatSent);
    
    m_syncController->setBPM(120.0);
    m_syncController->start(false);
    
    // Test over 20 bars = 80 quarter notes = 1920 clock ticks
    // This should result in 21 note emissions (0, 4, 8, ..., 80)
    simulateClockTicks(1920, 120.0);
    
    QVERIFY(beatSentSpy.count() >= 21);
    
    // Verify consistent 4-quarter-note intervals (whole note boundaries)
    QList<int> positions = getEmittedQuarterNotePositions(beatSentSpy);
    
    // Check that intervals are consistent
    for (int i = 1; i < positions.size(); ++i) {
        int interval = positions[i] - positions[i-1];
        QCOMPARE(interval, 4); // Should always be 4 quarter notes (1 bar)
    }
    
    // Verify we reached at least bar 20 (80 quarter notes)
    QVERIFY(positions.last() >= 80);
}

void SyncControllerTest::testNoteEmissionAtBar10() {
    QSignalSpy beatSentSpy(m_syncController, &SyncController::beatSent);
    
    m_syncController->setBPM(120.0);
    m_syncController->start(false);
    
    // Bar 10 = 40 quarter notes = 960 clock ticks
    simulateClockTicks(960, 120.0);
    
    // Find emission at bar 10 boundary (40 quarter notes)
    QList<int> positions = getEmittedQuarterNotePositions(beatSentSpy);
    bool foundBar10 = positions.contains(40);
    
    QVERIFY(foundBar10);
    
    // Verify current position is correct
    double currentPos = m_syncController->getCurrentPositionQuarterNotes();
    QVERIFY(currentPos >= 40.0);
    QVERIFY(currentPos < 41.0); // Should be in bar 10
}

void SyncControllerTest::testNoteEmissionAtBar20() {
    QSignalSpy beatSentSpy(m_syncController, &SyncController::beatSent);
    
    m_syncController->setBPM(120.0);
    m_syncController->start(false);
    
    // Bar 20 = 80 quarter notes = 1920 clock ticks
    simulateClockTicks(1920, 120.0);
    
    // Find emission at bar 20 boundary (80 quarter notes)
    QList<int> positions = getEmittedQuarterNotePositions(beatSentSpy);
    bool foundBar20 = positions.contains(80);
    
    QVERIFY(foundBar20);
    
    // Verify timing hasn't drifted - check that all intervals are still 4 quarter notes (1 bar)
    // All intervals should be exactly 4
    for (int i = 1; i < positions.size(); ++i) {
        QCOMPARE(positions[i] - positions[i-1], 4);
    }
}

void SyncControllerTest::testBoundaryDetectionAccuracy() {
    QSignalSpy beatSentSpy(m_syncController, &SyncController::beatSent);
    QSignalSpy positionSpy(m_syncController, &SyncController::positionChanged);
    
    m_syncController->setBPM(120.0);
    m_syncController->start(false);
    
    // Test with precise positioning around boundaries
    // Advance to just before a boundary (e.g., 3.9 quarter notes)
    simulateClockTicksToPosition(3.9, 120.0);
    
    // Advance one more tick to cross boundary
    m_syncController->handleMIDIClock();
    
    // Should have emitted note at boundary (4 quarter notes)
    // Note: Due to predictive emission, it might emit slightly before
    QVERIFY(beatSentSpy.count() > 0);
    
    // Verify position tracking is accurate
    double currentPos = m_syncController->getCurrentPositionQuarterNotes();
    QVERIFY(currentPos >= 3.9);
    QVERIFY(currentPos <= 4.1); // Allow small tolerance
}

void SyncControllerTest::testNoDelaysAfterBar10() {
    // This is the key test: ensure no delays occur after bar 10
    QSignalSpy beatSentSpy(m_syncController, &SyncController::beatSent);
    
    m_syncController->setBPM(120.0);
    m_syncController->start(false);
    
    // Advance to bar 10 (40 quarter notes = 960 ticks)
    simulateClockTicks(960, 120.0);
    
    QList<int> positionsBeforeBar10 = getEmittedQuarterNotePositions(beatSentSpy);
    int countBeforeBar10 = positionsBeforeBar10.size();
    
    // Continue to bar 15 (60 quarter notes = 1440 ticks total, 480 more ticks)
    simulateClockTicks(480, 120.0);
    
    QList<int> positionsAfterBar10 = getEmittedQuarterNotePositions(beatSentSpy);
    int countAfterBar10 = positionsAfterBar10.size();
    
    // Verify we got additional emissions after bar 10
    QVERIFY(countAfterBar10 > countBeforeBar10);
    
    // Verify all intervals remain consistent (4 quarter notes = 1 bar)
    for (int i = 1; i < positionsAfterBar10.size(); ++i) {
        int interval = positionsAfterBar10[i] - positionsAfterBar10[i-1];
        QCOMPARE(interval, 4);
    }
    
    // Verify we have emissions at expected boundaries: 44, 48, 52, 56, 60 (bars 11, 12, 13, 14, 15)
    QVERIFY(positionsAfterBar10.contains(44));
    QVERIFY(positionsAfterBar10.contains(48));
    QVERIFY(positionsAfterBar10.contains(52));
    QVERIFY(positionsAfterBar10.contains(56));
    
    // Verify timing is consistent - no gaps or delays
    // The last emission before bar 10 should be at 40, next should be at 44
    int lastBeforeBar10 = positionsBeforeBar10.last();
    QCOMPARE(lastBeforeBar10, 40);
    
    // Find the first emission after bar 10
    int firstAfterBar10 = -1;
    for (int pos : positionsAfterBar10) {
        if (pos > 40) {
            firstAfterBar10 = pos;
            break;
        }
    }
    QCOMPARE(firstAfterBar10, 44); // Should be exactly at next boundary (bar 11), no delay
}

QTEST_MAIN(SyncControllerTest)
#include "SyncControllerTest.moc"

