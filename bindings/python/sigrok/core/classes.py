##
## This file is part of the libsigrok project.
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
from collections import OrderedDict
from .lowlevel import *
from . import lowlevel
import itertools

__all__ = ['Error', 'Context', 'Driver', 'Device', 'Session', 'Packet', 'Log',
    'LogLevel', 'PacketType', 'Quantity', 'Unit', 'QuantityFlag', 'ConfigKey',
    'ChannelType', 'Channel', 'ChannelGroup', 'InputFormat', 'OutputFormat',
    'InputFile', 'Output']

class Error(Exception):

    def __str__(self):
        return sr_strerror(self.args[0])

def check(result):
    if result != SR_OK:
        raise Error(result)

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
        gvariant_ptr_array_setitem(array, 0,
            g_variant_new_uint64(value.numerator))
        gvariant_ptr_array_setitem(array, 1,
            g_variant_new_uint64(value.denominator))
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
        self._input_formats = None
        self._output_formats = None
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

    @property
    def input_formats(self):
        if not self._input_formats:
            self._input_formats = OrderedDict()
            input_list = sr_input_list()
            for i in itertools.count():
                input_ptr = sr_input_format_ptr_array_getitem(input_list, i)
                if input_ptr:
                    self._input_formats[input_ptr.id] = InputFormat(self, input_ptr)
                else:
                    break
        return self._input_formats

    @property
    def output_formats(self):
        if not self._output_formats:
            self._output_formats = {}
            output_list = sr_output_list()
            for i in itertools.count():
                output_ptr = sr_output_format_ptr_array_getitem(output_list, i)
                if output_ptr:
                    self._output_formats[output_ptr.id] = OutputFormat(self, output_ptr)
                else:
                    break
        return self._output_formats

class Driver(object):

    def __init__(self, context, struct):
        self.context = context
        self.struct = struct
        self._initialized = False

    @property
    def name(self):
        return self.struct.name

    def scan(self, **kwargs):
        if not self._initialized:
            check(sr_driver_init(self.context.struct, self.struct))
            self._initialized = True
        options = []
        for name, value in kwargs.items():
            key = getattr(ConfigKey, name)
            src = sr_config()
            src.key = key.id
            src.data = python_to_gvariant(value)
            options.append(src.this)
        option_list = python_to_gslist(options)
        device_list = sr_driver_scan(self.struct, option_list)
        g_slist_free(option_list)
        devices = [HardwareDevice(self, gpointer_to_sr_dev_inst_ptr(ptr))
            for ptr in gslist_to_python(device_list)]
        g_slist_free(device_list)
        return devices

class Device(object):

    def __new__(cls, struct, context):
        address = int(struct.this)
        if address not in context._devices:
            device = super(Device, cls).__new__(cls)
            device.struct = struct
            device.context = context
            device._channels = None
            device._channel_groups = None
            context._devices[address] = device
        return context._devices[address]

    @property
    def vendor(self):
        return self.struct.vendor

    @property
    def model(self):
        return self.struct.model

    @property
    def version(self):
        return self.struct.version

    @property
    def channels(self):
        if self._channels is None:
            self._channels = {}
            channel_list = self.struct.channels
            while (channel_list):
                channel_ptr = void_ptr_to_sr_channel_ptr(channel_list.data)
                self._channels[channel_ptr.name] = Channel(self, channel_ptr)
                channel_list = channel_list.next
        return self._channels

    @property
    def channel_groups(self):
        if self._channel_groups is None:
            self._channel_groups = {}
            channel_group_list = self.struct.channel_groups
            while (channel_group_list):
                channel_group_ptr = void_ptr_to_sr_channel_group_ptr(
                    channel_group_list.data)
                self._channel_groups[channel_group_ptr.name] = ChannelGroup(self,
                    channel_group_ptr)
                channel_group_list = channel_group_list.next
        return self._channel_groups

class HardwareDevice(Device):

    def __new__(cls, driver, struct):
        device = Device.__new__(cls, struct, driver.context)
        device.driver = driver
        return device

    def __getattr__(self, name):
        key = getattr(ConfigKey, name)
        data = new_gvariant_ptr_ptr()
        try:
            check(sr_config_get(self.driver.struct, self.struct, None,
                key.id, data))
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
            key = getattr(ConfigKey, name)
        except AttributeError:
            super(Device, self).__setattr__(name, value)
            return
        check(sr_config_set(self.struct, None, key.id, python_to_gvariant(value)))

class Channel(object):

    def __init__(self, device, struct):
        self.device = device
        self.struct = struct

    @property
    def type(self):
        return ChannelType(self.struct.type)

    @property
    def enabled(self):
        return self.struct.enabled

    @property
    def name(self):
        return self.struct.name

class ChannelGroup(object):

    def __init__(self, device, struct):
        self.device = device
        self.struct = struct
        self._channels = None

    def __iter__(self):
        return iter(self.channels)

    def __getattr__(self, name):
        key = config_key(name)
        data = new_gvariant_ptr_ptr()
        try:
            check(sr_config_get(self.device.driver.struct, self.device.struct,
                self.struct, key.id, data))
        except Error as error:
            if error.errno == SR_ERR_NA:
                raise NotImplementedError(
                    "Channel group does not implement %s" % name)
            else:
                raise AttributeError
        value = gvariant_ptr_ptr_value(data)
        return gvariant_to_python(value)

    def __setattr__(self, name, value):
        try:
            key = config_key(name)
        except AttributeError:
            super(ChannelGroup, self).__setattr__(name, value)
            return
        check(sr_config_set(self.device.struct, self.struct,
            key.id, python_to_gvariant(value)))

    @property
    def name(self):
        return self.struct.name

    @property
    def channels(self):
        if self._channels is None:
            self._channels = []
            channel_list = self.struct.channels
            while (channel_list):
                channel_ptr = void_ptr_to_sr_channel_ptr(channel_list.data)
                self._channels.append(Channel(self, channel_ptr))
                channel_list = channel_list.next
        return self._channels

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

    def open_device(self, device):
        check(sr_dev_open(device.struct))

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
                    "No Python mapping for packet type %s" % self.struct.type)
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

class InputFormat(object):

    def __init__(self, context, struct):
        self.context = context
        self.struct = struct

    @property
    def id(self):
        return self.struct.id

    @property
    def description(self):
        return self.struct.description

    def format_match(self, filename):
        return bool(self.struct.call_format_match(filename))

class InputFile(object):

    def __init__(self, format, filename, **kwargs):
        self.format = format
        self.filename = filename
        self.struct = sr_input()
        self.struct.format = self.format.struct
        self.struct.param = g_hash_table_new_full(
            g_str_hash_ptr, g_str_equal_ptr, g_free_ptr, g_free_ptr)
        for key, value in kwargs.items():
            g_hash_table_insert(self.struct.param, g_strdup(key), g_strdup(str(value)))
        check(self.format.struct.call_init(self.struct, self.filename))
        self.device = InputFileDevice(self)

    def load(self):
        check(self.format.struct.call_loadfile(self.struct, self.filename))

    def __del__(self):
        g_hash_table_destroy(self.struct.param)

class InputFileDevice(Device):

    def __new__(cls, file):
        device = Device.__new__(cls, file.struct.sdi, file.format.context)
        device.file = file
        return device

class OutputFormat(object):

    def __init__(self, context, struct):
        self.context = context
        self.struct = struct

    @property
    def id(self):
        return self.struct.id

    @property
    def description(self):
        return self.struct.description

class Output(object):

    def __init__(self, format, device, param=None):
        self.format = format
        self.device = device
        self.param = param
        self.struct = sr_output()
        self.struct.format = self.format.struct
        self.struct.sdi = self.device.struct
        self.struct.param = param
        check(self.format.struct.call_init(self.struct))

    def receive(self, packet):

        output_buf_ptr = new_uint8_ptr_ptr()
        output_len_ptr = new_uint64_ptr()
        using_obsolete_api = False

        if self.format.struct.event and packet.type in (
                PacketType.TRIGGER, PacketType.FRAME_BEGIN,
                PacketType.FRAME_END, PacketType.END):
            check(self.format.struct.call_event(self.struct, packet.type.id,
                output_buf_ptr, output_len_ptr))
            using_obsolete_api = True
        elif self.format.struct.data and packet.type.id == self.format.struct.df_type:
            check(self.format.struct.call_data(self.struct,
                packet.payload.struct.data, packet.payload.struct.length,
                output_buf_ptr, output_len_ptr))
            using_obsolete_api = True

        if using_obsolete_api:
            output_buf = uint8_ptr_ptr_value(output_buf_ptr)
            output_len = uint64_ptr_value(output_len_ptr)
            result = cdata(output_buf, output_len)
            g_free(output_buf)
            return result

        if self.format.struct.receive:
            out_ptr = new_gstring_ptr_ptr()
            check(self.format.struct.call_receive(self.struct, self.device.struct,
                packet.struct, out_ptr))
            out = gstring_ptr_ptr_value(out_ptr)
            if out:
                result = out.str
                g_string_free(out, True)
                return result

        return None

    def __del__(self):
        check(self.format.struct.call_cleanup(self.struct))

class ConfigInfo(object):

    def __new__(cls, key):
        struct = sr_config_info_get(key.id)
        if not struct:
            return None
        obj = super(ConfigInfo, cls).__new__(cls)
        obj.key = key
        obj.struct = struct
        return obj

    @property
    def datatype(self):
        return DataType(self.struct.datatype)

    @property
    def id(self):
        return self.struct.id

    @property
    def name(self):
        return self.struct.name

    @property
    def description(self):
        return self.struct.description

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

class ConfigKey(EnumValue):
    pass

class DataType(EnumValue):
    pass

class ChannelType(EnumValue):
    pass

for symbol_name in dir(lowlevel):
    for prefix, cls in [
        ('SR_LOG_', LogLevel),
        ('SR_DF_', PacketType),
        ('SR_MQ_', Quantity),
        ('SR_UNIT_', Unit),
        ('SR_MQFLAG_', QuantityFlag),
        ('SR_CONF_', ConfigKey),
        ('SR_T_', DataType),
        ('SR_CHANNEL_', ChannelType)]:
        if symbol_name.startswith(prefix):
            name = symbol_name[len(prefix):]
            value = getattr(lowlevel, symbol_name)
            obj = cls(value)
            setattr(cls, name, obj)
            if cls is ConfigKey:
                obj.info = ConfigInfo(obj)
                if obj.info:
                    setattr(cls, obj.info.id, obj)
                else:
                    setattr(cls, name.lower(), obj)
