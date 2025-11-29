#include <QApplication>
#include "lib/ui/MidiMasterWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    MidiMasterWindow window;
    window.show();
    
    window.showFullScreen();
    
    return app.exec();
}
