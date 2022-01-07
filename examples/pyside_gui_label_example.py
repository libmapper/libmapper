#!/usr/bin/env python

import sys, os
from PySide2.QtCore import Qt, QBasicTimer
from PySide2.QtWidgets import QApplication, QMainWindow, QSlider, QLabel
try:
    import libmapper as mpr
except:
    try:
        # Try the "bindings/python" directory, relative to the location of this
        # program, which is where it should be if the module has not been installed.
        sys.path.append(
                        os.path.join(os.path.join(os.getcwd(),
                                                  os.path.dirname(sys.argv[0])),
                                     '../bindings/python'))
        import libmapper as mpr
    except:
        print('Error importing libmapper module.')
        sys.exit(1)

numsliders = 3
dev = mpr.Device("pysideGUI")
sigs = []
for i in range(numsliders):
    sigs.append(dev.add_signal(mpr.Direction.OUTGOING, 'slider%i' %i, 1, mpr.Type.FLOAT, None, 0, 1))

class gui(QMainWindow):
    def __init__(self):
        QMainWindow.__init__(self)
        self.setGeometry(300, 300, 300, 300)
        self.setFixedSize(300, 300)
        self.setWindowTitle('libmapper device gui example')
        blurb = QLabel('These sliders will be dynamically labeled with the name of destination signals to which they are connected.', self)
        blurb.setGeometry(5, 0, 290, 50)
        blurb.setWordWrap(True)

        self.labels = []
        self.sliders = []
        for i in range(numsliders):
            self.sliders.append(QSlider(Qt.Orientation.Horizontal, self))
            self.sliders[i].setRange(0, 100)
            self.sliders[i].setGeometry(5, 100+i*75, 290, 20)
            self.labels.append(QLabel('slider%i' %i, self))
            self.labels[i].setGeometry(5, 75+i*75, 290, 15)

        self.sliders[0].valueChanged.connect(lambda x: sigs[0].set_value(x*0.01))
        self.sliders[1].valueChanged.connect(lambda x: sigs[1].set_value(x*0.01))
        self.sliders[2].valueChanged.connect(lambda x: sigs[2].set_value(x*0.01))

        self.timer = QBasicTimer()
        self.timer.start(10, self)

    def setLabel(self, index, label):
        if index < 0 or index > numsliders:
            return;
        self.labels[index].setText(label)

    def timerEvent(self, event):
        if event.timerId() == self.timer.timerId():
            dev.poll()

        else:
            QtGui.QFrame.timerEvent(self, event)

def h(type, map, event):
    try:
        src = map.signals(mpr.Location.SOURCE).next()
        id = sigs.index(src)
        if id < 0:
            return
        if event == mpr.Graph.Event.NEW:
            dst = map.signals(mpr.Location.DESTINATION).next()
            gui.setLabel(id, dst['name'])
        elif event == mpr.Graph.Event.REMOVED or event == mpr.Graph.Event.EXPIRED:
            gui.setLabel(id, 'slider%i' %id)
    except Exception as e:
        raise e

dev.graph().add_callback(h, mpr.Type.MAP)

def remove_dev():
    print('called remove_dev')
    global dev
    del dev

import atexit
atexit.register(remove_dev)

app = QApplication(sys.argv)
gui = gui()
gui.show()
sys.exit(app.exec_())
