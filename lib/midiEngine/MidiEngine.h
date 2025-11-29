#ifndef MIDIENGINE_H
#define MIDIENGINE_H

#include <RtMidi.h>
#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QMutex>
#include <QStringList>
#include <QByteArray>
#include <QQueue>
#include <memory>
#include <chrono>
#include <vector>

class MidiEngine : public QObject {
    Q_OBJECT

public:
    explicit MidiEngine(QObject *parent = nullptr);
    ~MidiEngine();

    bool initialize();
    void shutdown();

    QStringList getOutputPorts();
    QStringList getInputPorts();
    
    bool openOutputPort(const QString &portName);
    bool openInputPort(const QString &portName);
    
    void closeOutputPort();
    void closeInputPort();
    
    QString currentOutputPort() const;
    QString currentInputPort() const;
    
    void sendNoteOn(int channel, int note, int velocity);
    void sendNoteOff(int channel, int note, int velocity);
    void sendSystemMessage(int status);
    void sendSongPositionPointer(int position);
    
    void refreshPorts();

signals:
    void outputPortChanged(const QString &portName);
    void inputPortChanged(const QString &portName);
    void outputPortsRefreshed();
    void inputPortsRefreshed();
    void error(const QString &message);
    
    // MIDI input events
    void midiStartReceived();
    void midiStopReceived();
    void midiContinueReceived();
    void midiClockReceived();
    void midiSongPositionPointerReceived(int positionBeats, double positionQuarterNotes);
    void unknownMessageReceived(int status);

private:
    // MIDI Output (using RTMidi - switched from Drumstick to fix IAC Driver detection)
    std::unique_ptr<RtMidiOut> m_rtMidiOut;
    QStringList m_availableOutputPorts;
    QString m_currentOutputPortName;
    int m_currentOutputPortIndex;
    
    // MIDI Input (using RTMidi for better real-time performance)
    std::unique_ptr<RtMidiIn> m_rtMidiIn;
    QStringList m_availableInputPorts;
    QString m_currentInputPortName;
    int m_currentInputPortIndex;
    
    // SPP parsing state
    bool m_expectingSPPData;
    quint8 m_sppLSB;
    quint8 m_sppMSB;
    
    // Thread-safe state management
    mutable QMutex m_stateMutex;
    
    // Thread-safe message queue for RTMidi callback
    QMutex m_messageQueueMutex;
    QQueue<QByteArray> m_messageQueue;
    QTimer* m_messageProcessorTimer;
    
    // RTMidi callback handler (static, converts to instance method)
    static void rtMidiCallback(double deltatime, std::vector<unsigned char> *message, void *userData);
    
private slots:
    // Process queued MIDI messages (called on main thread via timer)
    void processQueuedMessages();
    
    // Handle raw MIDI bytes for SPP parsing
    void handleRawMIDIByte(quint8 byte);
};

#endif // MIDIENGINE_H

