#ifndef SYNCCONTROLLERTEST_H
#define SYNCCONTROLLERTEST_H

#include <QtTest/QtTest>
#include "SyncController.h"

class SyncControllerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();
    
    // Test cases
    void testNoteEmissionAtBoundaries();
    void testNoDelaysAfterManyBars();
    void testConsistentTimingOverManyBars();
    void testNoteEmissionAtBar10();
    void testNoteEmissionAtBar20();
    void testBoundaryDetectionAccuracy();
    void testNoDelaysAfterBar10();

private:
    SyncController *m_syncController;
    
    // Helper methods
    void simulateClockTicks(int count, double bpm = 120.0);
    void simulateClockTicksToPosition(double targetQuarterNotes, double bpm = 120.0);
    QList<int> getEmittedQuarterNotePositions(QSignalSpy &beatSentSpy);
};

#endif // SYNCCONTROLLERTEST_H

