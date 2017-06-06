package org.sigrok.core.interfaces;

import org.sigrok.core.classes.LogLevel;

public interface LogCallback
{
    public void run(LogLevel loglevel, String message);
}
