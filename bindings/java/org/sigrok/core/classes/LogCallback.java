package org.sigrok.core.classes;

public interface LogCallback 
{
    public void run(LogLevel loglevel, String message);
}
