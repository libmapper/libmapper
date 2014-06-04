
/**
 * libmapper instances example
 * Adapted from Processing.org Mouse Functions example.
 */

import Mapper.*;

Circle bover = null;
boolean locked = false;

Circle circles[] = new Circle[5];
int bs = 15;
int count = 0;

Mapper.Device dev = new Mapper.Device("TestInstances");

Mapper.Device.Signal sig_x_in = null;
Mapper.Device.Signal sig_y_in = null;
Mapper.Device.Signal sig_x_out = null;
Mapper.Device.Signal sig_y_out = null;

void setup() 
{
  size(300, 300);
  colorMode(HSB, 256, 256, 256);
  ellipseMode(CENTER);
  textAlign(CENTER, CENTER);
  frameRate(10);

  /* Note: null for the InputListener, since we are specifying
   * this later per-instance. */
  sig_x_in = dev.addInput("x", 1, 'i', "pixels",
                          new PropertyValue(0), new PropertyValue(width), null);
  sig_y_in = dev.addInput("y", 1, 'i', "pixels",
                          new PropertyValue(0), new PropertyValue(height), null);

  sig_x_out = dev.addOutput("x", 1, 'i', "pixels",
                            new PropertyValue(0), new PropertyValue(width));
  sig_y_out = dev.addOutput("y", 1, 'i', "pixels",
                            new PropertyValue(0), new PropertyValue(height));

  Mapper.InstanceEventListener evin = new Mapper.InstanceEventListener() {
    public void onEvent(Mapper.Device.Signal sig,
                        int instanceId,
                        int event,
                        TimeTag tt) {
      sig_x_in.setInstanceCallback(instanceId, circles[instanceId-1].lx);
      sig_y_in.setInstanceCallback(instanceId, circles[instanceId-1].ly);
    };
  };

  sig_x_in.setInstanceEventCallback(evin,
    Mapper.InstanceEventListener.IN_ALL);

  sig_x_in.reserveInstances(circles.length);
  sig_y_in.reserveInstances(circles.length);
  sig_x_out.reserveInstances(circles.length);
  sig_y_out.reserveInstances(circles.length);

  for (int i=0; i < circles.length; i++) {
    circles[i] = new Circle(Math.random()*(width-bs*2)+bs,
                            Math.random()*(height-bs*2)+bs,
                            Math.random()*256);
  }

  while (!dev.ready())
    dev.poll(100);

  frame.setTitle(dev.name());
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
  if(bover!=null) {
    locked = true;
    bover.pressed();
  } else {
    locked = false;
  }
}

void mouseDragged() {
  if(locked) {
    bover.dragged();
  }
}

void mouseReleased() {
  locked = false;
}

class Circle
{
  float bx;
  float by;
  float bdifx = 0.0;
  float bdify = 0.0;
  float hue;
  int id = 0;
  Mapper.InputListener lx, ly;

  Circle(double _bx, double _by, double _hue) {
    bx = (float)_bx;
    by = (float)_by;
    hue = (float)_hue;
    id = ++count;

    /* Add listeners for our instance */
    lx = new Mapper.InputListener() {
          void onInput(Mapper.Device.Signal sig,
                       int instanceId, int[] v, TimeTag tt) {
            if (v!=null)
              bx = v[0];
          }};

    ly = new Mapper.InputListener() {
          void onInput(Mapper.Device.Signal sig,
                       int instanceId, int[] v, TimeTag tt) {
            if (v!=null)
              by = v[0];
          }};
}

  boolean testMouse() {
    // Test if the cursor is over the circle
    float dx = mouseX - bx;
    float dy = mouseY - by;
    if (Math.sqrt(dx*dx+dy*dy) < bs) {
      bover = this;
      if(locked) {
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
    bdifx = mouseX-bx;
    bdify = mouseY-by;
  }

  void dragged() {
    bx = mouseX-bdifx;
    by = mouseY-bdify;
  }

  void display() {
    ellipse(bx, by, bs*2, bs*2);
    fill(0, 0, 256);
    text(""+id, bx, by);

    sig_x_out.updateInstance(id, (int)bx);
    sig_y_out.updateInstance(id, (int)by);
  }
}

