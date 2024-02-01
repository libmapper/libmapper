
/**
 * libmapper instances example
 * Adapted from Processing.org Mouse Functions example.
 */

import mapper.*;
import mapper.signal.*;

Circle bover = null;
boolean locked = false;

Circle circles[] = new Circle[5];
int bs = 15;
int count = 0;

mapper.Device dev = new mapper.Device("TestInstances");
mapper.Signal sigPos = null;

private void prepareExitHandler () {
  Runtime.getRuntime().addShutdownHook(new Thread(new Runnable() {
    public void run () {
      System.out.println("SHUTDOWN HOOK");
      // application exit code here
      dev.free();
    }
  }));
}

void setup() 
{
  prepareExitHandler();

  size(300, 300);
  colorMode(HSB, 256, 256, 256);
  ellipseMode(CENTER);
  textAlign(CENTER, CENTER);
  frameRate(10);
  
  mapper.signal.Listener listener = new mapper.signal.Listener() {
    public void onEvent(Signal.Instance inst, mapper.signal.Event e, float[] v, Time time) {
      //System.out.println("onEvent() for "
      //                   + inst.properties().get("name") + " instance "
      //                   + inst.id() + ": " + e.value());
      switch (e) {
        case UPDATE:
          if (inst.id() < circles.length)
            circles[(int)inst.id()].pos = v;
          break;
      }
  }};

  sigPos = dev.addSignal(Direction.INCOMING, "position", 2, Type.FLOAT, "pixels",
                         new float[] {0, 0}, new float[] {width, height},
                         circles.length, listener);
  sigPos.properties().put(Property.EPHEMERAL, false);

  for (int i=0; i < circles.length; i++) {
    circles[i] = new Circle(new float[] {(float)Math.random()*(width-bs*2)+bs,
                            (float)Math.random()*(height-bs*2)+bs},
                            (float)Math.random()*256);
    sigPos.instance(circles[i].id);
  }

  while (!dev.ready())
    dev.poll(100);

  surface.setTitle("" + dev.properties().get("name"));
}

void stop()
{
  dev.free();
}

void draw() 
{
  dev.poll(0);

  background(0);

  Circle sel = null;
  for (Circle c : circles) {
    if (c.testMouse() && sel == null)
      sel = c;
    c.display();
  }

  bover = sel;
}

void mousePressed() {
  if (bover!=null) {
    locked = true;
    bover.pressed();
  } else {
    locked = false;
  }
}

void mouseDragged() {
  if (locked) {
    bover.dragged();
  }
}

void mouseReleased() {
  locked = false;
}

class Circle
{
  float[] pos;
  float[] delta;
  //float bx;
  //float by;
  //float bdifx = 0.0;
  //float bdify = 0.0;
  float hue;
  int id = 0;
  boolean changed = false;

  Circle(float[] _pos, float _hue) {
    pos = new float[] {_pos[0], _pos[1]};
    delta = new float[] {0.0, 0.0};
    hue = _hue;
    id = ++count;
  }

  boolean testMouse() {
    // Test if the cursor is over the circle
    float dx = mouseX - pos[0];
    float dy = mouseY - pos[1];
    if (Math.sqrt(dx * dx + dy * dy) < bs) {
      bover = this;
      if (locked) {
        stroke(hue, 256, 153);
        fill(hue, 256, 256);
      }
      else {
        stroke(hue, 0, 256);
        fill(hue, 256, 153);
      }
      return true;
    }

    stroke(hue, 256, 153);
    fill(hue, 256, 256);
    return false;
  }

  void pressed() {
    fill(hue, 256, 153);
    delta[0] = mouseX - pos[0];
    delta[1] = mouseY - pos[1];
  }

  void dragged() {
    pos[0] = mouseX - delta[0];
    pos[1] = mouseY - delta[1];
    sigPos.instance(id).setValue(pos);
  }

  void display() {
    ellipse(pos[0], pos[1], bs * 2, bs * 2);
    fill(0, 0, 256);
    text("" + id, pos[0], pos[1]);
  }
}
