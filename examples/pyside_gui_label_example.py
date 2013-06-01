#!/usr/bin/env python

import sys
from PySide.QtCore import *
from PySide.QtGui import *
import mapper

def h(dev, link, sig, con, action):
    id = con['src_name']
    id = int(id[7])
    if action == mapper.MDEV_LOCAL_ESTABLISHED:
        gui.labels[id].setText(link['dest_name']+con['dest_name'])
    else:
        gui.labels[id].setText('slider%i' %id)

dev = mapper.device("pysideGUI")
dev.set_connection_callback(h)
sigs = []
for i in range(3):
    sigs.append(dev.add_output('/slider%i' %i, 1, 'f', None, 0, 1))

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
        for i in range(3):
            self.sliders.append(QSlider(Qt.Orientation.Horizontal, self))
            self.sliders[i].setRange(0, 100)
            self.sliders[i].setGeometry(5, 100+i*75, 290, 20)
            self.labels.append(QLabel('slider%i' %i, self))
            self.labels[i].setGeometry(5, 75+i*75, 290, 15)

        self.sliders[0].valueChanged.connect(lambda x: sigs[0].update(x*0.01))
        self.sliders[1].valueChanged.connect(lambda x: sigs[1].update(x*0.01))
        self.sliders[2].valueChanged.connect(lambda x: sigs[2].update(x*0.01))

        self.timer = QBasicTimer()
        self.timer.start(500, self)

    def timerEvent(self, event):
        if event.timerId() == self.timer.timerId():
            dev.poll()

        else:
            QtGui.QFrame.timerEvent(self, event)

app = QApplication(sys.argv)
gui = gui()
gui.show()
sys.exit(app.exec_())
