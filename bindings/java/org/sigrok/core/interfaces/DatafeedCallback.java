package org.sigrok.core.interfaces;

import org.sigrok.core.classes.Device;
import org.sigrok.core.classes.Packet;

public interface DatafeedCallback
{
    public void run(Device device, Packet packet);
}
