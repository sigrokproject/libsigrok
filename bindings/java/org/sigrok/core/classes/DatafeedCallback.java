package org.sigrok.core.classes;

public interface DatafeedCallback
{
    public void run(Device device, Packet packet);
}
