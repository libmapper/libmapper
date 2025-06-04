#!/usr/bin/env python

import sys, os
from PySide6 import QtGui, QtWidgets, QtCore
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

icon_path = os.getcwd() + '/../icons/libmapper_logo_flat_black.png'
print('icon path: ', icon_path)

class DragDropButton(QtWidgets.QPushButton):
    def __init__(self):
        super(DragDropButton, self).__init__()

        self.setStyleSheet(" background-color:transparent; border-style:none; border-width:1px;")
        self.setIcon(QtGui.QIcon(icon_path))
        self.setIconSize(QtCore.QSize(30, 30))

    def mouseMoveEvent(self, e):
        mimeData = QtCore.QMimeData()
        mimeData.setText('libmapper://signal ' + self.parent().get_sig_name() + ' @id ' + self.parent().get_sig_id())
        drag = QtGui.QDrag(self)
        drag.setMimeData(mimeData)

        dropAction = drag.exec(QtCore.Qt.CopyAction | QtCore.Qt.LinkAction)

class MappableSlider(QtWidgets.QWidget):
    def __init__(self, dev, name):
        super(MappableSlider, self).__init__()

        self.setAcceptDrops(True)

        grid = QtWidgets.QGridLayout()

        self.slider = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal)
        self.slider.setRange(0, 100)

        self.sig = dev.add_signal(mpr.Signal.Direction.INCOMING, name, 1, mpr.Type.FLOAT, None, 0, 1)
        self.sig.set_callback(lambda x, e, i, v, t: self.set_position(v * 100), mpr.Signal.Event.UPDATE)

        self.slider.valueChanged.connect(lambda x: self.sig.set_value(x * 0.01))

        self.label = QtWidgets.QLabel(name)

        grid.addWidget(self.label, 0, 0)
        grid.addWidget(self.slider, 1, 0)
        button = DragDropButton()
        grid.addWidget(button, 0, 1, 2, 1, QtCore.Qt.AlignmentFlag.AlignRight)

        grid.setColumnStretch(0, 1)
        grid.setColumnStretch(1, 0)

        self.setLayout(grid)

    def set_name(self, name = None):
        if not name:
            # reset to default
            name = self.sig[mpr.Property.NAME]
        self.label.setText(name)

    def set_position(self, pos):
        self.slider.setSliderPosition(pos)

    def get_sig_name(self):
        return self.sig.device()[mpr.Property.NAME] + '/' + self.sig[mpr.Property.NAME]

    def get_sig_id(self):
        return str(self.sig[mpr.Property.ID])

    def dragEnterEvent(self, e):
        if e.mimeData().hasFormat("text/plain"):
            e.accept()

    def dropEvent(self, e):
        text = e.mimeData().text()

        if text.startswith('libmapper://signal '):
            e.setDropAction(QtCore.Qt.CopyAction)
            e.accept()

            graph = self.sig.graph()

            # try extracting id first
            text = text[19:].split(' @id ')
            if len(text) == 2:
                id = int(text[1])
                print('id:', id)
                s = graph.signals().filter(mpr.Property.ID, id)
                if s:
                    s = s.next()
                    if s:
                        print('found signal by id')
                        mpr.Map(s, self.sig).push()
                        return;
            text = text[0]

            # fall back to using device and signal names
            names = text.split('/')
            if len(names) != 2:
                print('error retrieving device and signal names')
                return

            d = graph.devices().filter(mpr.Property.NAME, names[0])
            if not d:
                print('error: could not find device with name', names[0])
                return
            s = d.next().signals().filter(mpr.Property.NAME, names[1])
            if not s:
                print('error: could not find signal with name', names)
                return
            print('found signal by name')
            s = s.next()
            mpr.Map(s, self.sig).push()

class gui(QtWidgets.QMainWindow):
    def __init__(self):
        super(gui, self).__init__()
        self.setGeometry(300, 300, 300, 300)
        self.setWindowTitle('registering device...')

        self.ready = False
        self.setEnabled(False)

        blurb = QtWidgets.QLabel('These sliders will be dynamically labeled with the name of destination signals to which they are connected. You can also use drag-and-drop mapping with compatible devices', self)
        blurb.setWordWrap(True)

        grid = QtWidgets.QGridLayout()
        grid.setSpacing(10)

        grid.addWidget(blurb)

        self.graph = mpr.Graph()
        self.dev = mpr.Device('Sliders', self.graph)
        self.labels = []
        self.sliders = []
        for i in range(numsliders):
            name = 'slider%i' %i
            slider = MappableSlider(self.dev, name)
            self.sliders.append(slider)
            grid.addWidget(slider)

        centralWidget = QtWidgets.QWidget()
        centralWidget.setLayout(grid)
        self.setCentralWidget(centralWidget)

        self.dev.graph().add_callback(self.map_handler, mpr.Type.MAP)

        self.timer = QtCore.QBasicTimer()
        self.timer.start(10, self)

    def timerEvent(self, event):
        if event.timerId() == self.timer.timerId():
            self.dev.poll()
            if not self.ready and self.dev.get_is_ready():
                self.ready = True
                self.setEnabled(True)
                self.setWindowTitle(self.dev[mpr.Property.NAME])
        else:
            QtGui.QFrame.timerEvent(self, event)

    def find_slider(self, sig):
        for slider in self.sliders:
            if slider.sig == sig:
                return slider
        return None

    def map_handler(self, type, map, event):
        if not map[mpr.Property.IS_LOCAL]:
            # ignore
            return

        if event == mpr.Graph.Event.MODIFIED:
            # only interested in new and removed events
            return

        dst = map.signals(mpr.Map.Location.DESTINATION).next()
        if dst[mpr.Property.IS_LOCAL]:
            # we are the destination
            slider = self.find_slider(dst)
            if not slider:
                return
            if event == mpr.Graph.Event.REMOVED or event == mpr.Graph.Event.EXPIRED:
                slider.set_name()
            elif event == mpr.Graph.Event.NEW:
                src_names = []
                for src in map.signals(mpr.Map.Location.SOURCE):
                    src_names.append(src.device()['name'] + ':' + src['name'])
                label = '-> ' + ','.join(src_names)
                slider.set_name(label)

        else:
            # we are (one of) the source(s)
            for src in map.signals(mpr.Map.Location.SOURCE):
                slider = self.find_slider(src)
                if not slider:
                    continue
                if event == mpr.Graph.Event.REMOVED or event == mpr.Graph.Event.EXPIRED:
                    slider.set_name()
                elif event == mpr.Graph.Event.NEW:
                    dst = map.signals(mpr.Map.Location.DESTINATION).next()
                    label = dst.device()['name'] + ':' + dst['name'] + ' ->'
                    slider.set_name(label)

    def remove_dev(self):
        print('called remove_dev')
        self.dev.free()

app = QtWidgets.QApplication(sys.argv)
gui = gui()

import atexit
atexit.register(gui.remove_dev)

gui.show()
sys.exit(app.exec())
