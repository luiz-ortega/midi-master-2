#ifndef MIDIMASTERWINDOW_H
#define MIDIMASTERWINDOW_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QSpinBox>
#include "../midiEngine/MidiEngine.h"
#include "../midiEngine/SyncController.h"

class MidiMasterWindow : public QWidget {
    Q_OBJECT

public:
    explicit MidiMasterWindow(QWidget *parent = nullptr);
    ~MidiMasterWindow();

private slots:
    void onPortChanged(int index);
    void onInputPortChanged(int index);
    void onStartStop();
    void onTestNote();
    void onRefreshOutput();
    void onRefreshInput();
    
    // MIDI engine signals
    void onEngineOutputPortChanged(const QString &portName);
    void onEngineInputPortChanged(const QString &portName);
    void onOutputPortsRefreshed();
    void onInputPortsRefreshed();
    void onMidiError(const QString &message);
    
    // Sync controller signals
    void onSyncControllerStartReceived();
    void onSyncControllerStopReceived();
    void onSyncControllerContinueReceived();
    void onSyncControllerClockReceived();
    void onSyncControllerClockTick();
    void onSyncControllerBeatSent(int quarterNote);
    void onSyncControllerUnknownMessage(int status);
    void onSyncControllerSongPositionPointer(int positionBeats, double positionQuarterNotes);
    void onSyncControllerPositionChanged(int beats, double quarterNotes);
    
    // BPM handling
    void onBPMValueChanged(int value);
    void onBPMChanged(double bpm);

private:
    void setupUI();
    void initializeMIDI();
    
    QString findAutoSelectPort(const QStringList &ports);
    
    MidiEngine *m_engine;
    SyncController *m_syncController;
    
    QComboBox* portCombo;
    QComboBox* inputPortCombo;
    QSpinBox* bpmSpin;
    QPushButton* startStopBtn;
    QLabel* statusLabel;
    
    QStringList m_availableOutputPorts;
    QStringList m_availableInputPorts;
    bool m_noteOn;
    int m_midiChannel;
    int m_midiNote;
    int m_midiVelocity;
};

#endif // MIDIMASTERWINDOW_H

