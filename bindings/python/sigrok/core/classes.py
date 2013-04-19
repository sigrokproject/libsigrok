##
## This file is part of the sigrok project.
##
## Copyright (C) 2013 Martin Ling <martin-sigrok@earth.li>
##
## This program is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program.  If not, see <http://www.gnu.org/licenses/>.
##

from functools import partial
from fractions import Fraction
from .lowlevel import *
from . import lowlevel
import itertools

__all__ = ['Error', 'Context', 'Driver', 'Device', 'Session', 'Packet', 'Log',
    'LogLevel', 'PacketType', 'Quantity', 'Unit', 'QuantityFlag']

class Error(Exception):

    def __str__(self):
        return sr_strerror(self.args[0])

def check(result):
    if result != SR_OK:
        raise Error(result)

def config_key(name):
    if not name.lower() == name:
        raise AttributeError
    key_name = "SR_CONF_" + name.upper()
    return getattr(lowlevel, key_name)

def gvariant_to_python(value):
    type_string = g_variant_get_type_string(value)
    if type_string == 't':
        return g_variant_get_uint64(value)
    if type_string == 'b':
        return g_variant_get_bool(value)
    if type_string == 'd':
        return g_variant_get_double(value)
    if type_string == 's':
        return g_variant_get_string(value, None)
    if type_string == '(tt)':
        return Fraction(
            g_variant_get_uint64(g_variant_get_child_value(value, 0)),
            g_variant_get_uint64(g_variant_get_child_value(value, 1)))
    raise NotImplementedError(
        "Can't convert GVariant type '%s' to a Python type." % type_string)

def python_to_gvariant(value):
    if isinstance(value, int):
        return g_variant_new_uint64(value)
    if isinstance(value, bool):
        return g_variant_new_boolean(value)
    if isinstance(value, float):
        return g_variant_new_double(value)
    if isinstance(value, str):
        return g_variant_new_string(value)
    if isinstance(value, Fraction):
        array = new_gvariant_ptr_array(2)
        gvariant_ptr_array_setitem(array, 0, value.numerator)
        gvariant_ptr_array_setitem(array, 1, value.denominator)
        result = g_variant_new_tuple(array, 2)
        delete_gvariant_ptr_array(array)
        return result
    raise NotImplementedError(
        "Can't convert Python '%s' to a GVariant." % type(value))

def callback_wrapper(session, callback, device_ptr, packet_ptr):
    device = session.context._devices[int(device_ptr.this)]
    packet = Packet(session, packet_ptr)
    callback(device, packet)

class Context(object):

    def __init__(self):
        context_ptr_ptr = new_sr_context_ptr_ptr()
        check(sr_init(context_ptr_ptr))
        self.struct = sr_context_ptr_ptr_value(context_ptr_ptr)
        self._drivers = None
        self._devices = {}
        self.session = None

    def __del__(self):
        sr_exit(self.struct)

    @property
    def drivers(self):
        if not self._drivers:
            self._drivers = {}
            driver_list = sr_driver_list()
            for i in itertools.count():
                driver_ptr = sr_dev_driver_ptr_array_getitem(driver_list, i)
                if driver_ptr:
                    self._drivers[driver_ptr.name] = Driver(self, driver_ptr)
                else:
                    break
        return self._drivers

class Driver(object):

    def __init__(self, context, struct):
        self.context = context
        self.struct = struct
        self._initialized = False

    @property
    def name(self):
        return self.struct.name

    def scan(self):
        if not self._initialized:
            check(sr_driver_init(self.context.struct, self.struct))
            self._initialized = True
        devices = []
        device_list = sr_driver_scan(self.struct, None)
        device_list_item = device_list
        while device_list_item:
            ptr = device_list_item.data
            device_ptr = gpointer_to_sr_dev_inst_ptr(ptr)
            devices.append(Device(self, device_ptr))
            device_list_item = device_list_item.next
        g_slist_free(device_list)
        return devices

class Device(object):

    def __new__(cls, driver, struct):
        address = int(struct.this)
        if address not in driver.context._devices:
            device = super(Device, cls).__new__(cls, driver, struct)
            driver.context._devices[address] = device
        return driver.context._devices[address]

    def __init__(self, driver, struct):
        self.driver = driver
        self.struct = struct

    def __getattr__(self, name):
        key = config_key(name)
        data = new_gvariant_ptr_ptr()
        try:
            check(sr_config_get(self.driver.struct, key, data, self.struct))
        except Error as error:
            if error.errno == SR_ERR_NA:
                raise NotImplementedError(
                    "Device does not implement %s" % name)
            else:
                raise AttributeError
        value = gvariant_ptr_ptr_value(data)
        return gvariant_to_python(value)

    def __setattr__(self, name, value):
        try:
            key = config_key(name)
        except AttributeError:
            super(Device, self).__setattr__(name, value)
            return
        check(sr_config_set(self.struct, key, python_to_gvariant(value)))

    @property
    def vendor(self):
        return self.struct.vendor

    @property
    def model(self):
        return self.struct.model

    @property
    def version(self):
        return self.struct.version

class Session(object):

    def __init__(self, context):
        assert context.session is None
        self.context = context
        self.struct = sr_session_new()
        context.session = self

    def __del__(self):
        check(sr_session_destroy())

    def add_device(self, device):
        check(sr_session_dev_add(device.struct))

    def add_callback(self, callback):
        wrapper = partial(callback_wrapper, self, callback)
        check(sr_session_datafeed_python_callback_add(wrapper))

    def start(self):
        check(sr_session_start())

    def run(self):
        check(sr_session_run())

    def stop(self):
        check(sr_session_stop())

class Packet(object):

    def __init__(self, session, struct):
        self.session = session
        self.struct = struct
        self._payload = None

    @property
    def type(self):
        return PacketType(self.struct.type)

    @property
    def payload(self):
        if self._payload is None:
            pointer = self.struct.payload
            if self.type == PacketType.LOGIC:
                self._payload = Logic(self,
                    void_ptr_to_sr_datafeed_logic_ptr(pointer))
            elif self.type == PacketType.ANALOG:
                self._payload = Analog(self,
                    void_ptr_to_sr_datafeed_analog_ptr(pointer))
            else:
                raise NotImplementedError(
                    "No Python mapping for packet type %ѕ" % self.struct.type)
        return self._payload

class Logic(object):

    def __init__(self, packet, struct):
        self.packet = packet
        self.struct = struct
        self._data = None

    @property
    def data(self):
        if self._data is None:
            self._data = cdata(self.struct.data, self.struct.length)
        return self._data

class Analog(object):

    def __init__(self, packet, struct):
        self.packet = packet
        self.struct = struct
        self._data = None

    @property
    def num_samples(self):
        return self.struct.num_samples

    @property
    def mq(self):
        return Quantity(self.struct.mq)

    @property
    def unit(self):
        return Unit(self.struct.unit)

    @property
    def mqflags(self):
        return QuantityFlag.set_from_mask(self.struct.mqflags)

    @property
    def data(self):
        if self._data is None:
            self._data = float_array.frompointer(self.struct.data)
        return self._data

class Log(object):

    @property
    def level(self):
        return LogLevel(sr_log_loglevel_get())

    @level.setter
    def level(self, l):
        check(sr_log_loglevel_set(l.id))

    @property
    def domain(self):
        return sr_log_logdomain_get()

    @domain.setter
    def domain(self, d):
        check(sr_log_logdomain_set(d))

class EnumValue(object):

    _enum_values = {}

    def __new__(cls, id):
        if cls not in cls._enum_values:
            cls._enum_values[cls] = {}
        if id not in cls._enum_values[cls]:
            value = super(EnumValue, cls).__new__(cls)
            value.id = id
            cls._enum_values[cls][id] = value
        return cls._enum_values[cls][id]

class LogLevel(EnumValue):
    pass

class PacketType(EnumValue):
    pass

class Quantity(EnumValue):
    pass

class Unit(EnumValue):
    pass

class QuantityFlag(EnumValue):

    @classmethod
    def set_from_mask(cls, mask):
        result = set()
        while mask:
            new_mask = mask & (mask - 1)
            result.add(cls(mask ^ new_mask))
            mask = new_mask
        return result

for symbol_name in dir(lowlevel):
    for prefix, cls in [
        ('SR_LOG_', LogLevel),
        ('SR_DF_', PacketType),
        ('SR_MQ_', Quantity),
        ('SR_UNIT_', Unit),
        ('SR_MQFLAG_', QuantityFlag)]:
        if symbol_name.startswith(prefix):
            name = symbol_name[len(prefix):]
            value = getattr(lowlevel, symbol_name)
            setattr(cls, name, cls(value))
