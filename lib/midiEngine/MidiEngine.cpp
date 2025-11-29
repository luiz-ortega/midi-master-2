#include "MidiEngine.h"
#include <QCoreApplication>
#include <QDebug>
#include <QMutexLocker>
#include <QMetaObject>
#include <QQueue>
#include <QTimer>
#include <QThread>
#include <stdexcept>

MidiEngine::MidiEngine(QObject *parent)
    : QObject(parent)
    , m_currentOutputPortIndex(-1)
    , m_currentInputPortIndex(-1)
    , m_expectingSPPData(false)
    , m_sppLSB(0)
    , m_sppMSB(0)
    , m_messageProcessorTimer(nullptr)
{
    // Create timer to process queued MIDI messages on main thread
    m_messageProcessorTimer = new QTimer(this);
    m_messageProcessorTimer->setInterval(1); // Process every 1ms for low latency
    m_messageProcessorTimer->setSingleShot(false);
    connect(m_messageProcessorTimer, &QTimer::timeout, this, &MidiEngine::processQueuedMessages);
    
    // Note: RTMidi initialization is deferred to initialize() method
    // to ensure UI is ready to receive error signals
}

MidiEngine::~MidiEngine() {
    shutdown();
}

bool MidiEngine::initialize() {
    try {
        // Initialize RTMidi output (same approach as input - RTMidi correctly detects IAC Driver)
        if (!m_rtMidiOut) {
            try {
                m_rtMidiOut = std::make_unique<RtMidiOut>();
            } catch (const RtMidiError &rtmidiError) {
                return false;
            } catch (...) {
                return false;
            }
        }
        
        try {
            unsigned int portCount = m_rtMidiOut->getPortCount();
            
            // Add all MIDI ports including Network MIDI
            m_availableOutputPorts.clear();
            for (unsigned int i = 0; i < portCount; ++i) {
                try {
                    std::string portNameStr = m_rtMidiOut->getPortName(i);
                    QString portName = QString::fromStdString(portNameStr);
                    
                    m_availableOutputPorts.append(portName);
                } catch (const RtMidiError &rtmidiError) {
                    // Ignore errors getting individual port names
                }
            }
            
            // Emit signal so UI can populate the output combo box
            emit outputPortsRefreshed();
        } catch (const RtMidiError &rtmidiError) {
            emit outputPortsRefreshed(); // Still emit signal with empty list
        }
        
        // Initialize RTMidi input (get available ports)
        if (!m_rtMidiIn) {
            try {
                m_rtMidiIn = std::make_unique<RtMidiIn>();
            } catch (const RtMidiError &rtmidiError) {
                emit inputPortsRefreshed();
                return true; // Output is more critical, continue initialization
            } catch (...) {
                emit inputPortsRefreshed();
                return true; // Output is more critical, continue initialization
            }
        }
        
        // CRITICAL: Check if a port is already open (possibly from a previous session)
        // If it's open but not in our filtered list (e.g., Network MIDI), close it
        if (m_rtMidiIn->isPortOpen()) {
            // Close any open port - we'll only open valid ports from the filtered list
            try {
                m_rtMidiIn->cancelCallback();
                m_rtMidiIn->closePort();
                m_currentInputPortIndex = -1;
                m_currentInputPortName.clear();
            } catch (const RtMidiError &rtmidiError) {
                // Ignore errors during cleanup
            }
        }
        
        try {
            unsigned int portCount = m_rtMidiIn->getPortCount();
            
            // Add all MIDI input ports including Network MIDI
            m_availableInputPorts.clear();
            for (unsigned int i = 0; i < portCount; ++i) {
                try {
                    std::string portNameStr = m_rtMidiIn->getPortName(i);
                    QString portName = QString::fromStdString(portNameStr);
                    
                    m_availableInputPorts.append(portName);
                } catch (const RtMidiError &rtmidiError) {
                    // Ignore errors getting individual port names
                }
            }
            
            // Don't open port yet - wait for user selection
            m_currentInputPortIndex = -1;
            m_currentInputPortName.clear();
            
            // Emit signal so UI can populate the combo box
            emit inputPortsRefreshed();
        } catch (const RtMidiError &rtmidiError) {
            // Still emit signal with empty list so UI knows to show empty state
            emit inputPortsRefreshed();
            return true; // Output is more critical
        }
        
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

void MidiEngine::shutdown() {
    // Close RTMidi input
    if (m_rtMidiIn && m_rtMidiIn->isPortOpen()) {
        try {
            m_rtMidiIn->closePort();
        } catch (const RtMidiError &rtmidiError) {
            // Ignore errors during shutdown
        }
    }
    
    // Close RTMidi output
    if (m_rtMidiOut && m_rtMidiOut->isPortOpen()) {
        try {
            m_rtMidiOut->closePort();
            m_currentOutputPortIndex = -1;
            m_currentOutputPortName.clear();
        } catch (const RtMidiError &rtmidiError) {
            // Ignore errors during shutdown
        }
    }
}

QStringList MidiEngine::getOutputPorts() {
    return m_availableOutputPorts;
}

QStringList MidiEngine::getInputPorts() {
    return m_availableInputPorts;
}

bool MidiEngine::openOutputPort(const QString &portName) {
    if (!m_rtMidiOut) {
        return false;
    }
    
    // Close previous port if open
    if (m_rtMidiOut->isPortOpen()) {
        try {
            m_rtMidiOut->closePort();
        } catch (const RtMidiError &rtmidiError) {
            // Ignore errors
        }
    }
    
    // Find the port index
    int portIndex = -1;
    for (int i = 0; i < m_availableOutputPorts.size(); ++i) {
        if (m_availableOutputPorts[i] == portName) {
            portIndex = i;
            break;
        }
    }
    
    if (portIndex < 0) {
        return false;
    }
    
    // Open the port
    try {
        m_rtMidiOut->openPort(portIndex);
        m_currentOutputPortIndex = portIndex;
        m_currentOutputPortName = portName;
        
        emit outputPortChanged(portName);
        
        return true;
    } catch (const RtMidiError &rtmidiError) {
        m_currentOutputPortIndex = -1;
        m_currentOutputPortName.clear();
        return false;
    }
}

bool MidiEngine::openInputPort(const QString &portName) {
    if (!m_rtMidiIn) {
        return false;
    }
    
    // Close previous port if open
    if (m_rtMidiIn->isPortOpen()) {
        try {
            // Cancel any existing callback before closing
            m_rtMidiIn->cancelCallback();
            m_rtMidiIn->closePort();
        } catch (const RtMidiError &rtmidiError) {
            // Ignore errors
        }
    }
    
    // Cancel any existing callback before setting a new one
    try {
        m_rtMidiIn->cancelCallback();
    } catch (const RtMidiError &rtmidiError) {
        // Ignore errors if no callback was set
    }
    
    // Find the port index
    int portIndex = -1;
    for (int i = 0; i < m_availableInputPorts.size(); ++i) {
        if (m_availableInputPorts[i] == portName) {
            portIndex = i;
            break;
        }
    }
    
    if (portIndex < 0) {
        return false;
    }
    
    // Open the port
    try {
        m_rtMidiIn->openPort(portIndex);
        
        // Configure RTMidi to receive all message types (including system realtime)
        m_rtMidiIn->ignoreTypes(false, false, false);
        
        // Cancel any existing callback before setting a new one
        try {
            m_rtMidiIn->cancelCallback();
        } catch (const RtMidiError &rtmidiError) {
            // Ignore errors if no callback was set - this is fine
        }
        
        // Set callback function
        m_rtMidiIn->setCallback(&MidiEngine::rtMidiCallback, this);
    
        // Start message processor timer
        if (m_messageProcessorTimer) {
            m_messageProcessorTimer->start();
        }
        
        m_currentInputPortIndex = portIndex;
        m_currentInputPortName = portName;
        
        emit inputPortChanged(portName);
        
        return true;
    } catch (const RtMidiError &rtmidiError) {
        m_currentInputPortIndex = -1;
        m_currentInputPortName.clear();
        return false;
    }
}

void MidiEngine::closeOutputPort() {
    if (m_rtMidiOut && m_rtMidiOut->isPortOpen()) {
        try {
            m_rtMidiOut->closePort();
            m_currentOutputPortIndex = -1;
            m_currentOutputPortName.clear();
        } catch (const RtMidiError &rtmidiError) {
            // Ignore errors
        }
    }
}

void MidiEngine::closeInputPort() {
    if (m_rtMidiIn && m_rtMidiIn->isPortOpen()) {
        try {
            // Stop message processor
            if (m_messageProcessorTimer) {
                m_messageProcessorTimer->stop();
            }
            
            // Clear any queued messages
            {
                QMutexLocker locker(&m_messageQueueMutex);
                m_messageQueue.clear();
            }
            
            m_rtMidiIn->cancelCallback();
            m_rtMidiIn->closePort();
            m_currentInputPortIndex = -1;
            m_currentInputPortName.clear();
        } catch (const RtMidiError &rtmidiError) {
            // Ignore errors
        }
    }
}

QString MidiEngine::currentOutputPort() const {
    return m_currentOutputPortName;
}

QString MidiEngine::currentInputPort() const {
    return m_currentInputPortName;
}

void MidiEngine::sendNoteOn(int channel, int note, int velocity) {
    if (!m_rtMidiOut || !m_rtMidiOut->isPortOpen()) return;
    try {
        std::vector<unsigned char> message;
        message.push_back(static_cast<unsigned char>(0x90 | (channel & 0x0F))); // Note On
        message.push_back(static_cast<unsigned char>(note & 0x7F));
        message.push_back(static_cast<unsigned char>(velocity & 0x7F));
        m_rtMidiOut->sendMessage(&message);
    } catch (const RtMidiError &rtmidiError) {
        // Ignore errors
    }
}

void MidiEngine::sendNoteOff(int channel, int note, int velocity) {
    if (!m_rtMidiOut || !m_rtMidiOut->isPortOpen()) return;
    try {
        std::vector<unsigned char> message;
        message.push_back(static_cast<unsigned char>(0x80 | (channel & 0x0F))); // Note Off
        message.push_back(static_cast<unsigned char>(note & 0x7F));
        message.push_back(static_cast<unsigned char>(velocity & 0x7F));
        m_rtMidiOut->sendMessage(&message);
    } catch (const RtMidiError &rtmidiError) {
        // Ignore errors
    }
}

void MidiEngine::sendSystemMessage(int status) {
    if (!m_rtMidiOut || !m_rtMidiOut->isPortOpen()) return;
    try {
        std::vector<unsigned char> message;
        message.push_back(static_cast<unsigned char>(status & 0xFF));
        m_rtMidiOut->sendMessage(&message);
    } catch (const RtMidiError &rtmidiError) {
        // Ignore errors
    }
}

void MidiEngine::sendSongPositionPointer(int position) {
    if (!m_rtMidiOut || !m_rtMidiOut->isPortOpen()) return;
    try {
        // SPP is 14-bit: position in 16th notes (MIDI beats)
        // Send as 0xF2 followed by LSB (7 bits) and MSB (7 bits)
        quint8 lsb = static_cast<quint8>(position & 0x7F);
        quint8 msb = static_cast<quint8>((position >> 7) & 0x7F);
        
        std::vector<unsigned char> message;
        message.push_back(0xF2); // Song Position Pointer
        message.push_back(lsb);
        message.push_back(msb);
        m_rtMidiOut->sendMessage(&message);
    } catch (const RtMidiError &rtmidiError) {
        // Ignore errors
    }
}

void MidiEngine::refreshPorts() {
    // Refresh RTMidi output ports (same approach as input)
    if (!m_rtMidiOut) {
        try {
            m_rtMidiOut = std::make_unique<RtMidiOut>();
        } catch (const RtMidiError &rtmidiError) {
            emit outputPortsRefreshed();
            // Continue with input refresh
        } catch (...) {
            emit outputPortsRefreshed();
            // Continue with input refresh
        }
    }
    
    if (m_rtMidiOut) {
        // CRITICAL: Save the currently open port name before refreshing
        QString previouslyOpenOutputPort = m_currentOutputPortName;
        
        try {
            unsigned int portCount = m_rtMidiOut->getPortCount();
            
            // Add all MIDI output ports including Network MIDI
            m_availableOutputPorts.clear();
            for (unsigned int i = 0; i < portCount; ++i) {
                try {
                    std::string portNameStr = m_rtMidiOut->getPortName(i);
                    QString portName = QString::fromStdString(portNameStr);
                    m_availableOutputPorts.append(portName);
                } catch (const RtMidiError &rtmidiError) {
                    // Ignore errors getting individual port names
                }
            }
            
            // CRITICAL: Check if the previously open output port is still valid after refresh
            if (m_rtMidiOut->isPortOpen() && !previouslyOpenOutputPort.isEmpty()) {
                if (!m_availableOutputPorts.contains(previouslyOpenOutputPort)) {
                    try {
                        m_rtMidiOut->closePort();
                        m_currentOutputPortIndex = -1;
                        m_currentOutputPortName.clear();
                    } catch (const RtMidiError &rtmidiError) {
                        // Ignore errors
                    }
                }
            }
            
            emit outputPortsRefreshed();
        } catch (const RtMidiError &rtmidiError) {
            // Ignore errors during refresh
        }
    }
    
    // Refresh RTMidi input ports
    if (!m_rtMidiIn) {
        try {
            m_rtMidiIn = std::make_unique<RtMidiIn>();
        } catch (const RtMidiError &rtmidiError) {
            emit inputPortsRefreshed();
            return;
        } catch (...) {
            emit inputPortsRefreshed();
            return;
        }
    }
    
    // CRITICAL: Save the currently open port name before refreshing
    // We'll check if it's still valid after filtering
    QString previouslyOpenPort = m_currentInputPortName;
    
        try {
            unsigned int portCount = m_rtMidiIn->getPortCount();
            
            // Add all MIDI input ports including Network MIDI
            m_availableInputPorts.clear();
            for (unsigned int i = 0; i < portCount; ++i) {
                try {
                    std::string portNameStr = m_rtMidiIn->getPortName(i);
                    QString portName = QString::fromStdString(portNameStr);
                    m_availableInputPorts.append(portName);
                } catch (const RtMidiError &rtmidiError) {
                    // Ignore errors getting individual port names
                }
            }
        
        // CRITICAL: Check if the previously open port is still valid after refresh
        if (m_rtMidiIn->isPortOpen() && !previouslyOpenPort.isEmpty()) {
            if (!m_availableInputPorts.contains(previouslyOpenPort)) {
                try {
                    m_rtMidiIn->cancelCallback();
                    m_rtMidiIn->closePort();
                    m_currentInputPortIndex = -1;
                    m_currentInputPortName.clear();
                } catch (const RtMidiError &rtmidiError) {
                    // Ignore errors during cleanup
                }
            }
        }
        
        emit inputPortsRefreshed();
    } catch (const RtMidiError &rtmidiError) {
        // Still emit signal with empty list so UI knows to show empty state
        emit inputPortsRefreshed();
    }
}

// RTMidi callback function (static, called from RTMidi thread)
void MidiEngine::rtMidiCallback(double deltatime, std::vector<unsigned char> *message, void *userData) {
    MidiEngine *engine = static_cast<MidiEngine*>(userData);
    if (engine && message && !message->empty()) {
        // Convert to QByteArray for Qt compatibility
        QByteArray messageData(reinterpret_cast<const char*>(message->data()), 
                              static_cast<int>(message->size()));
        
        // Add to thread-safe queue
        {
            QMutexLocker locker(&engine->m_messageQueueMutex);
            engine->m_messageQueue.enqueue(messageData);
        }
    }
    Q_UNUSED(deltatime);
}

// Process queued MIDI messages (called on main thread via timer)
void MidiEngine::processQueuedMessages() {
    // Process all queued messages
    while (true) {
        QByteArray messageData;
        {
            QMutexLocker locker(&m_messageQueueMutex);
            if (m_messageQueue.isEmpty()) {
                break;
            }
            messageData = m_messageQueue.dequeue();
        }
        
        if (messageData.isEmpty()) continue;
        
        // Convert to vector for easier processing
        std::vector<unsigned char> message(messageData.begin(), messageData.end());
        
        unsigned char statusByte = message[0];
        
        QMutexLocker locker(&m_stateMutex);
        
        // Handle System Realtime messages (0xF8-0xFF)
        if (statusByte >= 0xF8 && statusByte <= 0xFF) {
            switch (statusByte) {
                case 0xF8: // MIDI Clock
                    emit midiClockReceived();
                    break;
                    
                case 0xFA: // Start
                    emit midiStartReceived();
                    break;
                    
                case 0xFB: // Continue
                    emit midiContinueReceived();
                    break;
                    
                case 0xFC: // Stop
                    emit midiStopReceived();
                    break;
                    
                case 0xFE: // Active Sensing
                    // Usually ignored
                    break;
                    
                case 0xFF: // System Reset
                    emit unknownMessageReceived(statusByte);
                    break;
                    
                default:
                    emit unknownMessageReceived(statusByte);
                    break;
            }
            continue; // Process next message
        }
        
        // Handle System Common messages (0xF0-0xF7)
        if (statusByte >= 0xF0 && statusByte <= 0xF7) {
            switch (statusByte) {
                case 0xF2: // Song Position Pointer
                    if (message.size() >= 3) {
                        // SPP data: LSB (message[1]) and MSB (message[2])
                        quint16 position = static_cast<quint16>(message[1]) | (static_cast<quint16>(message[2]) << 7);
                        double quarterNotes = position / 4.0; // SPP is in 16th notes, 4 per quarter note
                        emit midiSongPositionPointerReceived(position, quarterNotes);
                    }
                    break;
                    
                case 0xF0: // System Exclusive (start)
                    // Handle sysex if needed
                    break;
                    
                case 0xF1: // MIDI Time Code Quarter Frame
                case 0xF3: // Song Select
                case 0xF6: // Tune Request
                case 0xF7: // System Exclusive (end)
                    emit unknownMessageReceived(statusByte);
                    break;
                    
                default:
                    emit unknownMessageReceived(statusByte);
                    break;
            }
            continue; // Process next message
        }
    }
}

void MidiEngine::handleRawMIDIByte(quint8 byte) {
    // This method could be used if we had access to raw MIDI bytes
    // For now, it's a placeholder for future enhancement
    Q_UNUSED(byte);
}

