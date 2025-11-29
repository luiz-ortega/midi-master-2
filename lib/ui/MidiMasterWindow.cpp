#include "MidiMasterWindow.h"
#include <QApplication>
#include <QMessageBox>
#include <QCoreApplication>
#include <QTimer>

MidiMasterWindow::MidiMasterWindow(QWidget *parent)
    : QWidget(parent)
    , m_engine(nullptr)
    , m_syncController(nullptr)
    , m_noteOn(false)
    , m_midiChannel(0)
    , m_midiNote(60)
    , m_midiVelocity(100)
{
    setupUI();
    initializeMIDI();
}

MidiMasterWindow::~MidiMasterWindow() {
    if (m_syncController) {
        delete m_syncController;
    }
    if (m_engine) {
        delete m_engine;
    }
}

void MidiMasterWindow::setupUI() {
    setWindowTitle("MidiMaster2 - DAW Sync Controller");
    setMinimumSize(800, 600);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // MIDI Port Selection Group
    QGroupBox *portGroup = new QGroupBox("MIDI Ports", this);
    QVBoxLayout *portGroupLayout = new QVBoxLayout(portGroup);
    
    // Output Port Selection
    QHBoxLayout *outputPortLayout = new QHBoxLayout();
    outputPortLayout->addWidget(new QLabel("Output:", this));
    portCombo = new QComboBox(this);
    portCombo->setMinimumWidth(300);
    portCombo->setEditable(false);
    portCombo->setToolTip("Select MIDI output port (IAC Driver recommended). Configure IAC Driver in Audio MIDI Setup.app if empty.");
    outputPortLayout->addWidget(portCombo);
    QPushButton *refreshOutputBtn = new QPushButton("Refresh", this);
    connect(refreshOutputBtn, &QPushButton::clicked, this, &MidiMasterWindow::onRefreshOutput);
    outputPortLayout->addWidget(refreshOutputBtn);
    outputPortLayout->addStretch();
    connect(portCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MidiMasterWindow::onPortChanged);
    portGroupLayout->addLayout(outputPortLayout);
    
    // Input Port Selection
    QHBoxLayout *inputPortLayout = new QHBoxLayout();
    inputPortLayout->addWidget(new QLabel("Input (DAW Sync):", this));
    inputPortCombo = new QComboBox(this);
    inputPortCombo->setMinimumWidth(300);
    inputPortCombo->setEditable(false);
    inputPortCombo->setToolTip("Select MIDI input port to receive DAW sync messages (Start, Stop, Clock). Use IAC Driver to sync with your DAW. Configure IAC Driver in Audio MIDI Setup.app if empty.");
    inputPortLayout->addWidget(inputPortCombo);
    QPushButton *refreshInputBtn = new QPushButton("Refresh", this);
    connect(refreshInputBtn, &QPushButton::clicked, this, &MidiMasterWindow::onRefreshInput);
    inputPortLayout->addWidget(refreshInputBtn);
    inputPortLayout->addStretch();
    connect(inputPortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MidiMasterWindow::onInputPortChanged);
    portGroupLayout->addLayout(inputPortLayout);
    
    mainLayout->addWidget(portGroup);
    
    // Sync Settings Group
    QGroupBox *syncGroup = new QGroupBox("DAW Synchronization (MIDI Clock)", this);
    QVBoxLayout *syncLayout = new QVBoxLayout(syncGroup);
    
    QHBoxLayout *bpmLayout = new QHBoxLayout();
    bpmLayout->addWidget(new QLabel("BPM:", this));
    bpmSpin = new QSpinBox(this);
    bpmSpin->setRange(20, 300);
    bpmSpin->setValue(120);
    bpmSpin->setSuffix(" BPM");
    connect(bpmSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MidiMasterWindow::onBPMValueChanged);
    bpmLayout->addWidget(bpmSpin);
    bpmLayout->addStretch();
    syncLayout->addLayout(bpmLayout);
    
    mainLayout->addWidget(syncGroup);
    
    // Control Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    startStopBtn = new QPushButton("Start", this);
    startStopBtn->setMinimumHeight(40);
    connect(startStopBtn, &QPushButton::clicked, this, &MidiMasterWindow::onStartStop);
    buttonLayout->addWidget(startStopBtn);
    
    QPushButton *testNoteBtn = new QPushButton("Test Note", this);
    testNoteBtn->setMinimumHeight(40);
    connect(testNoteBtn, &QPushButton::clicked, this, &MidiMasterWindow::onTestNote);
    buttonLayout->addWidget(testNoteBtn);
    
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
    
    // Status Label
    statusLabel = new QLabel("Ready", this);
    statusLabel->setStyleSheet("QLabel { background-color: #e0e0e0; padding: 5px; }");
    mainLayout->addWidget(statusLabel);
}

void MidiMasterWindow::initializeMIDI() {
    m_engine = new MidiEngine(this);
    
    // Connect MIDI engine signals
    connect(m_engine, &MidiEngine::outputPortChanged, this, &MidiMasterWindow::onEngineOutputPortChanged);
    connect(m_engine, &MidiEngine::inputPortChanged, this, &MidiMasterWindow::onEngineInputPortChanged);
    connect(m_engine, &MidiEngine::outputPortsRefreshed, this, &MidiMasterWindow::onOutputPortsRefreshed);
    connect(m_engine, &MidiEngine::inputPortsRefreshed, this, &MidiMasterWindow::onInputPortsRefreshed);
    connect(m_engine, &MidiEngine::error, this, &MidiMasterWindow::onMidiError);
    
    // Create sync controller
    m_syncController = new SyncController(m_engine, this);
    
    // Connect sync controller signals
    connect(m_syncController, &SyncController::runningChanged, this, [this](bool running) {
        startStopBtn->setText(running ? "Stop" : "Start");
        if (statusLabel) {
            statusLabel->setText(running ? "Sync running" : "Ready");
        }
    });
    
    connect(m_syncController, &SyncController::bpmChanged, this, &MidiMasterWindow::onBPMChanged);
    connect(m_syncController, &SyncController::clockTick, this, &MidiMasterWindow::onSyncControllerClockTick);
    connect(m_syncController, &SyncController::beatSent, this, &MidiMasterWindow::onSyncControllerBeatSent);
    
    // Connect MIDI input signals
    connect(m_engine, &MidiEngine::midiStartReceived, this, &MidiMasterWindow::onSyncControllerStartReceived);
    connect(m_engine, &MidiEngine::midiStopReceived, this, &MidiMasterWindow::onSyncControllerStopReceived);
    connect(m_engine, &MidiEngine::midiContinueReceived, this, &MidiMasterWindow::onSyncControllerContinueReceived);
    connect(m_engine, &MidiEngine::midiClockReceived, this, &MidiMasterWindow::onSyncControllerClockReceived);
    
    // Initialize engine
    if (m_engine->initialize()) {
        // Process events to ensure signals are delivered
        QCoreApplication::processEvents();
        
        // Get available ports (signals should have been emitted during initialization)
        m_availableOutputPorts = m_engine->getOutputPorts();
        m_availableInputPorts = m_engine->getInputPorts();
        
        // Populate combo boxes directly (signal handlers should also update them, but this ensures immediate display)
        portCombo->clear();
        for (const QString &port : m_availableOutputPorts) {
            portCombo->addItem(port);
        }
        
        inputPortCombo->clear();
        for (const QString &port : m_availableInputPorts) {
            inputPortCombo->addItem(port);
        }
        
        // If ports are still empty, try refreshing explicitly
        if (m_availableOutputPorts.isEmpty() || m_availableInputPorts.isEmpty()) {
            m_engine->refreshPorts();
            QCoreApplication::processEvents();
            
            // Update again after refresh
            m_availableOutputPorts = m_engine->getOutputPorts();
            m_availableInputPorts = m_engine->getInputPorts();
            
            portCombo->clear();
            for (const QString &port : m_availableOutputPorts) {
                portCombo->addItem(port);
            }
            
            inputPortCombo->clear();
            for (const QString &port : m_availableInputPorts) {
                inputPortCombo->addItem(port);
            }
        }
        
        // Auto-select loopback port
        QString autoPort = findAutoSelectPort(m_availableOutputPorts);
        if (!autoPort.isEmpty()) {
            int index = m_availableOutputPorts.indexOf(autoPort);
            if (index >= 0) {
                portCombo->setCurrentIndex(index);
            }
        }
        
        QString autoInputPort = findAutoSelectPort(m_availableInputPorts);
        if (!autoInputPort.isEmpty()) {
            int index = m_availableInputPorts.indexOf(autoInputPort);
            if (index >= 0) {
                inputPortCombo->setCurrentIndex(index);
            }
        }
        
        // Additional DAW sync signal handling
        connect(m_engine, &MidiEngine::midiSongPositionPointerReceived, this, &MidiMasterWindow::onSyncControllerSongPositionPointer);
        connect(m_engine, &MidiEngine::unknownMessageReceived, this, &MidiMasterWindow::onSyncControllerUnknownMessage);
        
        // Connect position tracking
        connect(m_syncController, &SyncController::positionChanged, this, &MidiMasterWindow::onSyncControllerPositionChanged);
    }
}

void MidiMasterWindow::onPortChanged(int index) {
    if (!m_engine) return;
    
    if (index >= 0 && index < m_availableOutputPorts.size()) {
        QString portName = m_availableOutputPorts[index];
        m_engine->openOutputPort(portName);
    }
}

void MidiMasterWindow::onInputPortChanged(int index) {
    if (!m_engine) {
        return;
    }
    
    // If index is invalid or points to the "No ports available" message, close any open port
    if (index < 0 || index >= m_availableInputPorts.size()) {
        qDebug() << "Invalid input port selection (index:" << index << "), closing any open port";
        m_engine->closeInputPort();
        if (statusLabel) {
            statusLabel->setText("⚠️ No valid input port selected");
            statusLabel->setStyleSheet("QLabel { background-color: #fff3cd; padding: 5px; color: #856404; }");
        }
        return;
    }
    
    QString portName = m_availableInputPorts[index];
    if (m_engine->openInputPort(portName)) {
        if (statusLabel && statusLabel->text().contains("No valid")) {
            statusLabel->setText("Ready");
            statusLabel->setStyleSheet("QLabel { background-color: #e0e0e0; padding: 5px; }");
        }
    }
}

void MidiMasterWindow::onStartStop() {
    if (!m_engine || !m_syncController) {
        QMessageBox::warning(this, "Error", "MIDI engine or sync controller not initialized.");
        return;
    }
    
    if (m_engine->currentOutputPort().isEmpty()) {
        QMessageBox::warning(this, "No Port", "Please select a MIDI output port first.");
        return;
    }

    if (m_syncController->isRunning()) {
        m_syncController->stop(true);
        if (statusLabel) {
            statusLabel->setText("Stopping sync...");
        }
    } else {
        m_syncController->start(true);
        if (statusLabel) {
            statusLabel->setText("Starting sync...");
        }
    }
}

void MidiMasterWindow::onTestNote() {
    if (!m_engine || m_engine->currentOutputPort().isEmpty()) {
        QMessageBox::warning(this, "No Port", "Please select a MIDI output port first.");
        return;
    }

    try {
        const int channel = 0;
        const int note = 60;
        const int velocity = 100;
        
        m_engine->sendNoteOn(channel, note, velocity);
        if (statusLabel) {
            statusLabel->setText("Test note sent");
        }
        
        QTimer::singleShot(500, this, [this, channel, note]() {
            if (m_engine) {
                m_engine->sendNoteOff(channel, note, 0);
                if (statusLabel) {
                    statusLabel->setText("Test note released");
                }
            }
        });
    } catch (const std::exception& e) {
        Q_UNUSED(e);
        // Ignore errors
    }
}

void MidiMasterWindow::onRefreshOutput() {
    if (!m_engine) return;
    m_engine->refreshPorts();
}

void MidiMasterWindow::onRefreshInput() {
    if (!m_engine) return;
    m_engine->refreshPorts();
}

void MidiMasterWindow::onEngineOutputPortChanged(const QString &portName) {
    // Port opened successfully
    Q_UNUSED(portName);
}

void MidiMasterWindow::onEngineInputPortChanged(const QString &portName) {
    // Port opened successfully
    Q_UNUSED(portName);
}

void MidiMasterWindow::onOutputPortsRefreshed() {
    if (!m_engine) {
        return;
    }
    
    // Preserve current selection before refreshing
    QString currentPort = m_engine->currentOutputPort();
    
    // Debug: Log what port is actually open
    if (!currentPort.isEmpty()) {
        qDebug() << "Current open output port:" << currentPort;
    }
    
    m_availableOutputPorts = m_engine->getOutputPorts();
    portCombo->clear();
    
    if (m_availableOutputPorts.isEmpty()) {
        // No ports available - likely IAC Driver not configured
        portCombo->addItem("No MIDI ports available - Configure IAC Driver");
        portCombo->setEnabled(false);
        if (statusLabel) {
            statusLabel->setText("⚠️ No MIDI ports found. Please configure IAC Driver in Audio MIDI Setup.app");
            statusLabel->setStyleSheet("QLabel { background-color: #fff3cd; padding: 5px; color: #856404; }");
        }
    } else {
        portCombo->setEnabled(true);
        for (const QString &port : m_availableOutputPorts) {
            portCombo->addItem(port);
        }
        
        // Restore selection if port is still available
        if (!currentPort.isEmpty()) {
            int index = m_availableOutputPorts.indexOf(currentPort);
            if (index >= 0) {
                portCombo->setCurrentIndex(index);
            } else {
                // Port is open but not in the filtered list (might be Network MIDI)
                // Add it to the combo box so user can see what's selected
                qDebug() << "Warning: Open port" << currentPort << "not in available ports list (may be filtered)";
                portCombo->addItem(currentPort + " (open)");
                portCombo->setCurrentIndex(portCombo->count() - 1);
            }
        }
        
        // Reset status label if ports are now available
        if (statusLabel && statusLabel->text().contains("No MIDI ports")) {
            statusLabel->setText("Ready");
            statusLabel->setStyleSheet("QLabel { background-color: #e0e0e0; padding: 5px; }");
        }
    }
}

void MidiMasterWindow::onInputPortsRefreshed() {
    if (!m_engine) {
        return;
    }
    
    // Preserve current selection before refreshing
    QString currentPort = m_engine->currentInputPort();
    
    m_availableInputPorts = m_engine->getInputPorts();
    
    inputPortCombo->clear();
    
    if (m_availableInputPorts.isEmpty()) {
        // No ports available - likely IAC Driver not configured
        inputPortCombo->addItem("No MIDI ports available - Configure IAC Driver");
        inputPortCombo->setEnabled(false);
        if (statusLabel && statusLabel->text().contains("Ready")) {
            statusLabel->setText("⚠️ No MIDI input ports found. Please configure IAC Driver in Audio MIDI Setup.app");
            statusLabel->setStyleSheet("QLabel { background-color: #fff3cd; padding: 5px; color: #856404; }");
        }
    } else {
        inputPortCombo->setEnabled(true);
        for (int i = 0; i < m_availableInputPorts.size(); ++i) {
            const QString &port = m_availableInputPorts[i];
            inputPortCombo->addItem(port);
        }
        
        // Restore selection if port is still available
        if (!currentPort.isEmpty()) {
            int index = m_availableInputPorts.indexOf(currentPort);
            if (index >= 0) {
                inputPortCombo->setCurrentIndex(index);
            }
        }
    }
}

void MidiMasterWindow::onMidiError(const QString &message) {
    // Ignore errors to keep UI responsive
    Q_UNUSED(message);
}

void MidiMasterWindow::onSyncControllerStartReceived() {
    m_syncController->handleDAWStart();
}

void MidiMasterWindow::onSyncControllerStopReceived() {
    // Handle DAW stop directly without going through onStartStop()
    // onStartStop() checks for output port which can cause popup issues
    // and is not needed when DAW sends stop command
    if (m_syncController) {
        m_syncController->handleDAWStop();
        // Update UI state to reflect stop
        if (startStopBtn) {
            startStopBtn->setText("Start");
        }
        if (statusLabel) {
            statusLabel->setText("DAW stopped");
        }
    }
}

void MidiMasterWindow::onSyncControllerContinueReceived() {
    m_syncController->handleDAWContinue();
}

void MidiMasterWindow::onSyncControllerClockReceived() {
    m_syncController->handleMIDIClock();
}

void MidiMasterWindow::onSyncControllerClockTick() {
    // Clock tick handled by sync controller
}

void MidiMasterWindow::onSyncControllerBeatSent(int quarterNote) {
}

void MidiMasterWindow::onSyncControllerSongPositionPointer(int positionBeats, double positionQuarterNotes) {
    if (positionBeats > 0 || positionQuarterNotes > 0.0) {
        m_syncController->handleSongPositionPointer(positionBeats, positionQuarterNotes);
    }
}

void MidiMasterWindow::onSyncControllerPositionChanged(int beats, double quarterNotes) {
    // Position tracking handled within the sync controller
    Q_UNUSED(beats);
    Q_UNUSED(quarterNotes);
}

void MidiMasterWindow::onSyncControllerUnknownMessage(int status) {
    // Unknown messages are ignored
    Q_UNUSED(status);
}

void MidiMasterWindow::onBPMValueChanged(int value) {
    if (m_syncController) {
        m_syncController->setBPM(value);
    }
}

void MidiMasterWindow::onBPMChanged(double bpm) {
    // Update BPM spin box when BPM changes (e.g., from DAW sync)
    if (bpmSpin) {
        // Block signals temporarily to avoid recursive updates
        bpmSpin->blockSignals(true);
        bpmSpin->setValue(static_cast<int>(qRound(bpm)));
        bpmSpin->blockSignals(false);
    }
}

QString MidiMasterWindow::findAutoSelectPort(const QStringList &ports) {
    // Priority: IAC Driver > Virtual ports > Other CoreMIDI ports
    // Network MIDI is excluded to avoid UDP buffering issues that cause timing problems
    
    // First priority: IAC Driver (CoreMIDI-based, best for loopback and DAW communication)
    // IAC drivers provide lowest latency and most reliable timing
    for (const QString &port : ports) {
        if (port.contains("IAC", Qt::CaseInsensitive)) {
            qDebug() << "Auto-selected IAC Driver port:" << port;
            return port;
        }
    }
    
    // Second priority: Virtual MIDI ports (CoreMIDI-based, good alternative to IAC)
    for (const QString &port : ports) {
        if (port.contains("Virtual", Qt::CaseInsensitive)) {
            qDebug() << "Auto-selected Virtual MIDI port:" << port;
            return port;
        }
    }
    
    // Third priority: Other CoreMIDI loopback ports (but exclude Network/rtpMIDI)
    // Network MIDI uses UDP which has buffering and can cause rushed/dragged notes
    for (const QString &port : ports) {
        if (port.contains("Loopback", Qt::CaseInsensitive) &&
            !port.contains("Network", Qt::CaseInsensitive) &&
            !port.contains("rtpMIDI", Qt::CaseInsensitive) &&
            !port.contains("rtp.MIDI", Qt::CaseInsensitive)) {
            qDebug() << "Auto-selected Loopback port (non-Network):" << port;
            return port;
        }
    }
    
    // Final fallback: Any CoreMIDI port (excluding Network MIDI)
    // Filter out Network MIDI ports to avoid timing issues
    QStringList coreMIDIPorts;
    for (const QString &port : ports) {
        bool isNetworkPort = false;
        bool ok;
        int portNum = port.toInt(&ok);
        if (ok && portNum >= 21928 && portNum <= 21948) {
            isNetworkPort = true;
        }
        if (port.contains("Network", Qt::CaseInsensitive) ||
            port.contains("rtpMIDI", Qt::CaseInsensitive) ||
            port.contains("rtp.MIDI", Qt::CaseInsensitive)) {
            isNetworkPort = true;
        }
        if (!isNetworkPort) {
            coreMIDIPorts.append(port);
        }
    }
    
    // Return first CoreMIDI port if available, or empty string if none
    if (!coreMIDIPorts.isEmpty()) {
        qDebug() << "Auto-selected CoreMIDI port:" << coreMIDIPorts.first();
        return coreMIDIPorts.first();
    }
    
    // Last resort: if all ports were filtered, return empty (user must configure manually)
    qDebug() << "WARNING: No suitable CoreMIDI ports found. All ports may be Network MIDI.";
    return QString();
}

