/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013-2014 Martin Ling <martin-sigrok@earth.li>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**

@mainpage API Reference

Introduction
------------

The libsigrokcxx API provides an object-oriented C++ interface to the
functionality in libsigrok, including automatic memory and resource management.

It is built on top of the public libsigrok C API, and is designed to be used as
a standalone alternative API. Programs should not mix usage of the C and C++
APIs; the C++ interface code needs to have full control of all C API calls for
resources to be managed correctly.

Memory management
-----------------

All runtime objects created through the C++ API are passed and accessed via
shared pointers, using the C++11 std::shared_ptr implementation. This means
that a reference count is kept for each object.

Shared pointers can be copied and assigned in a user's program, automatically
updating their reference count and deleting objects when they are no longer in
use. The C++ interface code also keeps track of internal dependencies between
libsigrok resources, and ensures that objects are not prematurely deleted when
their resources are in use by other objects.

This means that management of libsigrokcxx objects and their underlying
libsigrok resources can be treated as fully automatic. As long as all shared
pointers to objects are deleted or reassigned when no longer in use, all
underlying resources will be released at the right time.

Getting started
---------------

Usage of the C++ API needs to begin with a call to sigrok::Context::create().
This will create the global libsigrok context and returns a shared pointer to
the sigrok::Context object. Methods on this object provide access to the
hardware drivers, input and output formats supported by the library, as well
as means of creating other objects such as sessions and triggers.

Error handling
--------------

When any libsigrok C API call returns an error, a sigrok::Error exception is
raised, which provides access to the error code and description.

*/

#ifndef LIBSIGROKCXX_HPP
#define LIBSIGROKCXX_HPP

#include <libsigrok/libsigrok.h>
#include <glibmm.h>

#include <stdexcept>
#include <memory>
#include <vector>
#include <map>
#include <set>

namespace sigrok
{

using namespace std;

/* Forward declarations */
class SR_API Error;
class SR_API Context;
class SR_API Driver;
class SR_API Device;
class SR_API HardwareDevice;
class SR_API Channel;
class SR_API EventSource;
class SR_API Session;
class SR_API ConfigKey;
class SR_API InputFormat;
class SR_API OutputFormat;
class SR_API OutputFlag;
class SR_API LogLevel;
class SR_API ChannelGroup;
class SR_API Trigger;
class SR_API TriggerStage;
class SR_API TriggerMatch;
class SR_API TriggerMatchType;
class SR_API ChannelType;
class SR_API Packet;
class SR_API PacketPayload;
class SR_API PacketType;
class SR_API Quantity;
class SR_API Unit;
class SR_API QuantityFlag;
class SR_API Input;
class SR_API InputDevice;
class SR_API Output;
class SR_API DataType;
class SR_API Option;
class SR_API UserDevice;

/** Exception thrown when an error code is returned by any libsigrok call. */
class SR_API Error: public exception
{
public:
	Error(int result);
	~Error() throw();
	const int result;
	const char *what() const throw();
};

/* Base template for classes whose resources are owned by a parent object. */
template <class Class, class Parent, typename Struct>
class SR_API ParentOwned
{
protected:
	/*  Parent object which owns this child object's underlying structure.

		This shared pointer will be null when this child is unused, but
		will be assigned to point to the parent before any shared pointer
		to this child is handed out to the user.

		When the reference count of this child falls to zero, this shared
		pointer to its parent is reset by a custom deleter on the child's
		shared pointer.

		This strategy ensures that the destructors for both the child and
		the parent are called at the correct time, i.e. only when all
		references to both the parent and all its children are gone. */
	shared_ptr<Parent> _parent;

	/* Weak pointer for shared_from_this() implementation. */
	weak_ptr<Class> _weak_this;

public:
	/* Get parent object that owns this object. */
	shared_ptr<Parent> parent()
	{
		return _parent;
	}

	/* Note, this implementation will create a new smart_ptr if none exists. */
	shared_ptr<Class> shared_from_this()
	{
		shared_ptr<Class> shared;

		if (!(shared = _weak_this.lock()))
		{
			shared = shared_ptr<Class>((Class *) this, reset_parent);
			_weak_this = shared;
		}

		return shared;
	}

	shared_ptr<Class> get_shared_pointer(shared_ptr<Parent> parent)
	{
		if (!parent)
			throw Error(SR_ERR_BUG);
		this->_parent = parent;
		return shared_from_this();
	}

	shared_ptr<Class> get_shared_pointer(Parent *parent)
	{
		if (!parent)
			throw Error(SR_ERR_BUG);
		return get_shared_pointer(parent->shared_from_this());
	}
protected:
	static void reset_parent(Class *object)
	{
		if (!object->_parent)
			throw Error(SR_ERR_BUG);
		object->_parent.reset();
	}

	Struct *_structure;

	ParentOwned<Class, Parent, Struct>(Struct *structure) :
		_structure(structure)
	{
	}
};

/* Base template for classes whose resources are owned by the user. */
template <class Class, typename Struct>
class SR_API UserOwned : public enable_shared_from_this<Class>
{
public:
	shared_ptr<Class> shared_from_this()
	{
		auto shared = enable_shared_from_this<Class>::shared_from_this();
		if (!shared)
			throw Error(SR_ERR_BUG);
		return shared;
	}
protected:
	Struct *_structure;

	UserOwned<Class, Struct>(Struct *structure) :
		_structure(structure)
	{
	}

	/* Deleter needed to allow shared_ptr use with protected destructor. */
	class Deleter
	{
	public:
		void operator()(Class *object) { delete object; }
	};
};

/** Type of log callback */
typedef function<void(const LogLevel *, string message)> LogCallbackFunction;

/** Resource reader delegate. */
class SR_API ResourceReader
{
public:
	ResourceReader() {}
	virtual ~ResourceReader();
private:
	/** Resource open hook. */
	virtual void open(struct sr_resource *res, string name) = 0;
	/** Resource close hook. */
	virtual void close(struct sr_resource *res) = 0;
	/** Resource read hook. */
	virtual size_t read(const struct sr_resource *res, void *buf, size_t count) = 0;

	static SR_PRIV int open_callback(struct sr_resource *res,
			const char *name, void *cb_data);
	static SR_PRIV int close_callback(struct sr_resource *res,
			void *cb_data);
	static SR_PRIV ssize_t read_callback(const struct sr_resource *res,
			void *buf, size_t count, void *cb_data);
	friend class Context;
};

/** The global libsigrok context */
class SR_API Context : public UserOwned<Context, struct sr_context>
{
public:
	/** Create new context */
	static shared_ptr<Context> create();
	/** libsigrok package version. */
	string package_version();
	/** libsigrok library version. */
	string lib_version();
	/** Available hardware drivers, indexed by name. */
	map<string, shared_ptr<Driver> > drivers();
	/** Available input formats, indexed by name. */
	map<string, shared_ptr<InputFormat> > input_formats();
	/** Available output formats, indexed by name. */
	map<string, shared_ptr<OutputFormat> > output_formats();
	/** Current log level. */
	const LogLevel *log_level();
	/** Set the log level.
	 * @param level LogLevel to use. */
	void set_log_level(const LogLevel *level);
	/** Set the log callback.
	 * @param callback Callback of the form callback(LogLevel, string). */
	void set_log_callback(LogCallbackFunction callback);
	/** Set the log callback to the default handler. */
	void set_log_callback_default();
	/** Install a delegate for reading resource files.
	 * @param reader The resource reader delegate, or nullptr to unset. */
	void set_resource_reader(ResourceReader *reader);
	/** Create a new session. */
	shared_ptr<Session> create_session();
	/** Create a new user device. */
	shared_ptr<UserDevice> create_user_device(
		string vendor, string model, string version);
	/** Create a header packet. */
	shared_ptr<Packet> create_header_packet(Glib::TimeVal start_time);
	/** Create a meta packet. */
	shared_ptr<Packet> create_meta_packet(
		map<const ConfigKey *, Glib::VariantBase> config);
	/** Create a logic packet. */
	shared_ptr<Packet> create_logic_packet(
		void *data_pointer, size_t data_length, unsigned int unit_size);
	/** Create an analog packet. */
	shared_ptr<Packet> create_analog_packet(
		vector<shared_ptr<Channel> > channels,
		float *data_pointer, unsigned int num_samples, const Quantity *mq,
		const Unit *unit, vector<const QuantityFlag *> mqflags);
	/** Load a saved session.
	 * @param filename File name string. */
	shared_ptr<Session> load_session(string filename);
	/** Create a new trigger.
	 * @param name Name string for new trigger. */
	shared_ptr<Trigger> create_trigger(string name);
	/** Open an input file.
	 * @param filename File name string. */
	shared_ptr<Input> open_file(string filename);
	/** Open an input stream based on header data.
	 * @param header Initial data from stream. */
	shared_ptr<Input> open_stream(string header);
	map<string, string> serials(shared_ptr<Driver> driver);
protected:
	map<string, Driver *> _drivers;
	map<string, InputFormat *> _input_formats;
	map<string, OutputFormat *> _output_formats;
	Session *_session;
	LogCallbackFunction _log_callback;
	Context();
	~Context();
	friend class Deleter;
	friend class Session;
	friend class Driver;
};

enum Capability {
	GET = SR_CONF_GET,
	SET = SR_CONF_SET,
	LIST = SR_CONF_LIST
};

/** An object that can be configured. */
class SR_API Configurable
{
public:
	/** Read configuration for the given key.
	 * @param key ConfigKey to read. */
	Glib::VariantBase config_get(const ConfigKey *key);
	/** Set configuration for the given key to a specified value.
	 * @param key ConfigKey to set.
	 * @param value Value to set. */
	void config_set(const ConfigKey *key, Glib::VariantBase value);
	/** Enumerate available values for the given configuration key.
	 * @param key ConfigKey to enumerate values for. */
	Glib::VariantContainerBase config_list(const ConfigKey *key);
	/** Enumerate available keys, according to a given index key. */
	map<const ConfigKey *, set<Capability> > config_keys(const ConfigKey *key);
	/** Check for a key in the list from a given index key. */
	bool config_check(const ConfigKey *key, const ConfigKey *index_key);
protected:
	Configurable(
		struct sr_dev_driver *driver,
		struct sr_dev_inst *sdi,
		struct sr_channel_group *channel_group);
	virtual ~Configurable();
	struct sr_dev_driver *config_driver;
	struct sr_dev_inst *config_sdi;
	struct sr_channel_group *config_channel_group;
};

/** A hardware driver provided by the library */
class SR_API Driver :
	public ParentOwned<Driver, Context, struct sr_dev_driver>,
	public Configurable
{
public:
	/** Name of this driver. */
	string name();
	/** Long name for this driver. */
	string long_name();
	/** Scan for devices and return a list of devices found.
	 * @param options Mapping of (ConfigKey, value) pairs. */
	vector<shared_ptr<HardwareDevice> > scan(
		map<const ConfigKey *, Glib::VariantBase> options =
			map<const ConfigKey *, Glib::VariantBase>());
protected:
	bool _initialized;
	vector<HardwareDevice *> _devices;
	Driver(struct sr_dev_driver *structure);
	~Driver();
	friend class Context;
	friend class HardwareDevice;
	friend class ChannelGroup;
};

/** A generic device, either hardware or virtual */
class SR_API Device : public Configurable
{
public:
	/** Vendor name for this device. */
	string vendor();
	/** Model name for this device. */
	string model();
	/** Version string for this device. */
	string version();
	/** Serial number for this device. */
	string serial_number();
	/** Connection ID for this device. */
	string connection_id();
	/** List of the channels available on this device. */
	vector<shared_ptr<Channel> > channels();
	/** Channel groups available on this device, indexed by name. */
	map<string, shared_ptr<ChannelGroup> > channel_groups();
	/** Open device. */
	void open();
	/** Close device. */
	void close();
protected:
	Device(struct sr_dev_inst *structure);
	~Device();
	virtual shared_ptr<Device> get_shared_from_this() = 0;
	shared_ptr<Channel> get_channel(struct sr_channel *ptr);
	struct sr_dev_inst *_structure;
	map<struct sr_channel *, Channel *> _channels;
	map<string, ChannelGroup *> _channel_groups;
	/** Deleter needed to allow shared_ptr use with protected destructor. */
	class Deleter
	{
	public:
		void operator()(Device *device) { delete device; }
	};
	friend class Deleter;
	friend class Session;
	friend class Channel;
	friend class ChannelGroup;
	friend class Output;
	friend class Analog;
};

/** A real hardware device, connected via a driver */
class SR_API HardwareDevice :
	public UserOwned<HardwareDevice, struct sr_dev_inst>,
	public Device
{
public:
	/** Driver providing this device. */
	shared_ptr<Driver> driver();
protected:
	HardwareDevice(shared_ptr<Driver> driver, struct sr_dev_inst *structure);
	~HardwareDevice();
	shared_ptr<Device> get_shared_from_this();
	shared_ptr<Driver> _driver;
	/** Deleter needed to allow shared_ptr use with protected destructor. */
	class Deleter
	{
	public:
		void operator()(HardwareDevice *device) { delete device; }
	};
	friend class Deleter;
	friend class Driver;
	friend class ChannelGroup;
};

/** A virtual device, created by the user */
class SR_API UserDevice :
	public UserOwned<UserDevice, struct sr_dev_inst>,
	public Device
{
public:
	/** Add a new channel to this device. */
	shared_ptr<Channel> add_channel(unsigned int index, const ChannelType *type, string name);
protected:
	UserDevice(string vendor, string model, string version);
	~UserDevice();
	shared_ptr<Device> get_shared_from_this();
	/** Deleter needed to allow shared_ptr use with protected destructor. */
	class Deleter
	{
	public:
		void operator()(UserDevice *device) { delete device; }
	};
	friend class Context;
	friend class Deleter;
};

/** A channel on a device */
class SR_API Channel :
	public ParentOwned<Channel, Device, struct sr_channel>
{
public:
	/** Current name of this channel. */
	string name();
	/** Set the name of this channel. *
	 * @param name Name string to set. */
	void set_name(string name);
	/** Type of this channel. */
	const ChannelType *type();
	/** Enabled status of this channel. */
	bool enabled();
	/** Set the enabled status of this channel.
	 * @param value Boolean value to set. */
	void set_enabled(bool value);
	/** Get the index number of this channel. */
	unsigned int index();
protected:
	Channel(struct sr_channel *structure);
	~Channel();
	const ChannelType * const _type;
	friend class Device;
	friend class UserDevice;
	friend class ChannelGroup;
	friend class Session;
	friend class TriggerStage;
	friend class Context;
};

/** A group of channels on a device, which share some configuration */
class SR_API ChannelGroup :
	public ParentOwned<ChannelGroup, Device, struct sr_channel_group>,
	public Configurable
{
public:
	/** Name of this channel group. */
	string name();
	/** List of the channels in this group. */
	vector<shared_ptr<Channel> > channels();
protected:
	ChannelGroup(Device *device, struct sr_channel_group *structure);
	~ChannelGroup();
	vector<Channel *> _channels;
	friend class Device;
};

/** A trigger configuration */
class SR_API Trigger : public UserOwned<Trigger, struct sr_trigger>
{
public:
	/** Name of this trigger configuration. */
	string name();
	/** List of the stages in this trigger. */
	vector<shared_ptr<TriggerStage> > stages();
	/** Add a new stage to this trigger. */
	shared_ptr<TriggerStage> add_stage();
protected:
	Trigger(shared_ptr<Context> context, string name);
	~Trigger();
	shared_ptr<Context> _context;
	vector<TriggerStage *> _stages;
	friend class Deleter;
	friend class Context;
	friend class Session;
};

/** A stage in a trigger configuration */
class SR_API TriggerStage :
	public ParentOwned<TriggerStage, Trigger, struct sr_trigger_stage>
{
public:
	/** Index number of this stage. */
	int number();
	/** List of match conditions on this stage. */
	vector<shared_ptr<TriggerMatch> > matches();
	/** Add a new match condition to this stage.
	 * @param channel Channel to match on.
	 * @param type TriggerMatchType to apply. */
	void add_match(shared_ptr<Channel> channel, const TriggerMatchType *type);
	/** Add a new match condition to this stage.
	 * @param channel Channel to match on.
	 * @param type TriggerMatchType to apply.
	 * @param value Threshold value. */
	void add_match(shared_ptr<Channel> channel, const TriggerMatchType *type, float value);
protected:
	vector<TriggerMatch *> _matches;
	TriggerStage(struct sr_trigger_stage *structure);
	~TriggerStage();
	friend class Trigger;
};

/** A match condition in a trigger configuration  */
class SR_API TriggerMatch :
	public ParentOwned<TriggerMatch, TriggerStage, struct sr_trigger_match>
{
public:
	/** Channel this condition matches on. */
	shared_ptr<Channel> channel();
	/** Type of match. */
	const TriggerMatchType *type();
	/** Threshold value. */
	float value();
protected:
	TriggerMatch(struct sr_trigger_match *structure, shared_ptr<Channel> channel);
	~TriggerMatch();
	shared_ptr<Channel> _channel;
	friend class TriggerStage;
};

/** Type of session stopped callback */
typedef function<void()> SessionStoppedCallback;

/** Type of datafeed callback */
typedef function<void(shared_ptr<Device>, shared_ptr<Packet>)>
	DatafeedCallbackFunction;

/* Data required for C callback function to call a C++ datafeed callback */
class SR_PRIV DatafeedCallbackData
{
public:
	void run(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *pkt);
protected:
	DatafeedCallbackFunction _callback;
	DatafeedCallbackData(Session *session,
		DatafeedCallbackFunction callback);
	Session *_session;
	friend class Session;
};

/** Type of source callback */
typedef function<bool(Glib::IOCondition)>
	SourceCallbackFunction;

/* Data required for C callback function to call a C++ source callback */
class SR_PRIV SourceCallbackData
{
public:
	bool run(int revents);
protected:
	SourceCallbackData(shared_ptr<EventSource> source);
	shared_ptr<EventSource> _source;
	friend class Session;
};

/** An I/O event source */
class SR_API EventSource
{
public:
	/** Create an event source from a file descriptor.
	 * @param fd File descriptor.
	 * @param events GLib IOCondition event mask.
	 * @param timeout Timeout in milliseconds.
	 * @param callback Callback of the form callback(events) */
	static shared_ptr<EventSource> create(int fd, Glib::IOCondition events,
		int timeout, SourceCallbackFunction callback);
	/** Create an event source from a GLib PollFD
	 * @param pollfd GLib PollFD
	 * @param timeout Timeout in milliseconds.
	 * @param callback Callback of the form callback(events) */
	static shared_ptr<EventSource> create(Glib::PollFD pollfd, int timeout,
		SourceCallbackFunction callback);
	/** Create an event source from a GLib IOChannel
	 * @param channel GLib IOChannel.
	 * @param events GLib IOCondition event mask.
	 * @param timeout Timeout in milliseconds.
	 * @param callback Callback of the form callback(events) */
	static shared_ptr<EventSource> create(
		Glib::RefPtr<Glib::IOChannel> channel, Glib::IOCondition events,
		int timeout, SourceCallbackFunction callback);
protected:
	EventSource(int timeout, SourceCallbackFunction callback);
	~EventSource();
	enum source_type {
		SOURCE_FD,
		SOURCE_POLLFD,
		SOURCE_IOCHANNEL
	} _type;
	int _fd;
	Glib::PollFD _pollfd;
	Glib::RefPtr<Glib::IOChannel> _channel;
	Glib::IOCondition _events;
	int _timeout;
	SourceCallbackFunction _callback;
	/** Deleter needed to allow shared_ptr use with protected destructor. */
	class Deleter
	{
	public:
		void operator()(EventSource *source) { delete source; }
	};
	friend class Deleter;
	friend class Session;
	friend class SourceCallbackData;
};

/** A virtual device associated with a stored session */
class SR_API SessionDevice :
	public ParentOwned<SessionDevice, Session, struct sr_dev_inst>,
	public Device
{
protected:
	SessionDevice(struct sr_dev_inst *sdi);
	~SessionDevice();
	shared_ptr<Device> get_shared_from_this();
	/** Deleter needed to allow shared_ptr use with protected destructor. */
	class Deleter
	{
	public:
		void operator()(SessionDevice *device) { delete device; }
	};
	friend class Deleter;
	friend class Session;
};

/** A sigrok session */
class SR_API Session : public UserOwned<Session, struct sr_session>
{
public:
	/** Add a device to this session.
	 * @param device Device to add. */
	void add_device(shared_ptr<Device> device);
	/** List devices attached to this session. */
	vector<shared_ptr<Device> > devices();
	/** Remove all devices from this session. */
	void remove_devices();
	/** Add a datafeed callback to this session.
	 * @param callback Callback of the form callback(Device, Packet). */
	void add_datafeed_callback(DatafeedCallbackFunction callback);
	/** Remove all datafeed callbacks from this session. */
	void remove_datafeed_callbacks();
	/** Add an I/O event source.
	 * @param source EventSource to add. */
	void add_source(shared_ptr<EventSource> source);
	/** Remove an event source.
	 * @param source EventSource to remove. */
	void remove_source(shared_ptr<EventSource> source);
	/** Start the session. */
	void start();
	/** Run the session event loop. */
	void run();
	/** Stop the session. */
	void stop();
	/** Return whether the session is running. */
	bool is_running() const;
	/** Set callback to be invoked on session stop. */
	void set_stopped_callback(SessionStoppedCallback callback);
	/** Get current trigger setting. */
	shared_ptr<Trigger> trigger();
	/** Get the context. */
	shared_ptr<Context> context();
	/** Set trigger setting.
	 * @param trigger Trigger object to use. */
	void set_trigger(shared_ptr<Trigger> trigger);
	/** Get filename this session was loaded from. */
	string filename();
protected:
	Session(shared_ptr<Context> context);
	Session(shared_ptr<Context> context, string filename);
	~Session();
	shared_ptr<Device> get_device(const struct sr_dev_inst *sdi);
	const shared_ptr<Context> _context;
	map<const struct sr_dev_inst *, SessionDevice *> _owned_devices;
	map<const struct sr_dev_inst *, shared_ptr<Device> > _other_devices;
	vector<DatafeedCallbackData *> _datafeed_callbacks;
	SessionStoppedCallback _stopped_callback;
	map<shared_ptr<EventSource>, SourceCallbackData *> _source_callbacks;
	string _filename;
	shared_ptr<Trigger> _trigger;
	friend class Deleter;
	friend class Context;
	friend class DatafeedCallbackData;
	friend class SessionDevice;
};

/** A packet on the session datafeed */
class SR_API Packet : public UserOwned<Packet, const struct sr_datafeed_packet>
{
public:
	/** Type of this packet. */
	const PacketType *type();
	/** Payload of this packet. */
	shared_ptr<PacketPayload> payload();
protected:
	Packet(shared_ptr<Device> device,
		const struct sr_datafeed_packet *structure);
	~Packet();
	shared_ptr<Device> _device;
	PacketPayload *_payload;
	friend class Deleter;
	friend class Session;
	friend class Output;
	friend class DatafeedCallbackData;
	friend class Header;
	friend class Meta;
	friend class Logic;
	friend class Analog;
	friend class Context;
};

/** Abstract base class for datafeed packet payloads */
class SR_API PacketPayload
{
protected:
	PacketPayload();
	virtual ~PacketPayload() = 0;
	virtual shared_ptr<PacketPayload> get_shared_pointer(Packet *parent) = 0;
	/** Deleter needed to allow shared_ptr use with protected destructor. */
	class Deleter
	{
	public:
		void operator()(PacketPayload *payload) { delete payload; }
	};
	friend class Deleter;
	friend class Packet;
	friend class Output;
};

/** Payload of a datafeed header packet */
class SR_API Header :
	public ParentOwned<Header, Packet, const struct sr_datafeed_header>,
	public PacketPayload
{
public:
	/* Feed version number. */
	int feed_version();
	/* Start time of this session. */
	Glib::TimeVal start_time();
protected:
	Header(const struct sr_datafeed_header *structure);
	~Header();
	shared_ptr<PacketPayload> get_shared_pointer(Packet *parent);
	friend class Packet;
};

/** Payload of a datafeed metadata packet */
class SR_API Meta :
	public ParentOwned<Meta, Packet, const struct sr_datafeed_meta>,
	public PacketPayload
{
public:
	/* Mapping of (ConfigKey, value) pairs. */
	map<const ConfigKey *, Glib::VariantBase> config();
protected:
	Meta(const struct sr_datafeed_meta *structure);
	~Meta();
	shared_ptr<PacketPayload> get_shared_pointer(Packet *parent);
	map<const ConfigKey *, Glib::VariantBase> _config;
	friend class Packet;
};

/** Payload of a datafeed packet with logic data */
class SR_API Logic :
	public ParentOwned<Logic, Packet, const struct sr_datafeed_logic>,
	public PacketPayload
{
public:
	/* Pointer to data. */
	void *data_pointer();
	/* Data length in bytes. */
	size_t data_length();
	/* Size of each sample in bytes. */
	unsigned int unit_size();
protected:
	Logic(const struct sr_datafeed_logic *structure);
	~Logic();
	shared_ptr<PacketPayload> get_shared_pointer(Packet *parent);
	friend class Packet;
};

/** Payload of a datafeed packet with analog data */
class SR_API Analog :
	public ParentOwned<Analog, Packet, const struct sr_datafeed_analog>,
	public PacketPayload
{
public:
	/** Pointer to data. */
	float *data_pointer();
	/** Number of samples in this packet. */
	unsigned int num_samples();
	/** Channels for which this packet contains data. */
	vector<shared_ptr<Channel> > channels();
	/** Measured quantity of the samples in this packet. */
	const Quantity *mq();
	/** Unit of the samples in this packet. */
	const Unit *unit();
	/** Measurement flags associated with the samples in this packet. */
	vector<const QuantityFlag *> mq_flags();
protected:
	Analog(const struct sr_datafeed_analog *structure);
	~Analog();
	shared_ptr<PacketPayload> get_shared_pointer(Packet *parent);
	friend class Packet;
};

/** An input format supported by the library */
class SR_API InputFormat :
	public ParentOwned<InputFormat, Context, const struct sr_input_module>
{
public:
	/** Name of this input format. */
	string name();
	/** Description of this input format. */
	string description();
	/** A list of preferred file name extensions for this file format.
         * @note This list is a recommendation only. */
	vector<string> extensions();
	/** Options supported by this input format. */
	map<string, shared_ptr<Option> > options();
	/** Create an input using this input format.
	 * @param options Mapping of (option name, value) pairs. */
	shared_ptr<Input> create_input(map<string, Glib::VariantBase> options =
		map<string, Glib::VariantBase>());
protected:
	InputFormat(const struct sr_input_module *structure);
	~InputFormat();
	friend class Context;
	friend class InputDevice;
};

/** An input instance (an input format applied to a file or stream) */
class SR_API Input : public UserOwned<Input, const struct sr_input>
{
public:
	/** Virtual device associated with this input. */
	shared_ptr<InputDevice> device();
	/** Send next stream data.
	 * @param data Next stream data.
	 * @param length Length of data. */
	void send(void *data, size_t length);
	/** Signal end of input data. */
	void end();
protected:
	Input(shared_ptr<Context> context, const struct sr_input *structure);
	~Input();
	shared_ptr<Context> _context;
	InputDevice *_device;
	friend class Deleter;
	friend class Context;
	friend class InputFormat;
};

/** A virtual device associated with an input */
class SR_API InputDevice :
	public ParentOwned<InputDevice, Input, struct sr_dev_inst>,
	public Device
{
protected:
	InputDevice(shared_ptr<Input> input, struct sr_dev_inst *sdi);
	~InputDevice();
	shared_ptr<Device> get_shared_from_this();
	shared_ptr<Input> _input;
	friend class Input;
};

/** An option used by an output format */
class SR_API Option : public UserOwned<Option, const struct sr_option>
{
public:
	/** Short name of this option suitable for command line usage. */
	string id();
	/** Short name of this option suitable for GUI usage. */
	string name();
	/** Description of this option in a sentence. */
	string description();
	/** Default value for this option. */
	Glib::VariantBase default_value();
	/** Possible values for this option, if a limited set. */
	vector<Glib::VariantBase> values();
protected:
	Option(const struct sr_option *structure,
		shared_ptr<const struct sr_option *> structure_array);
	~Option();
	shared_ptr<const struct sr_option *> _structure_array;
	friend class Deleter;
	friend class InputFormat;
	friend class OutputFormat;
};

/** An output format supported by the library */
class SR_API OutputFormat :
	public ParentOwned<OutputFormat, Context, const struct sr_output_module>
{
public:
	/** Name of this output format. */
	string name();
	/** Description of this output format. */
	string description();
	/** A list of preferred file name extensions for this file format.
         * @note This list is a recommendation only. */
	vector<string> extensions();
	/** Options supported by this output format. */
	map<string, shared_ptr<Option> > options();
	/** Create an output using this format.
	 * @param device Device to output for.
	 * @param options Mapping of (option name, value) pairs. */
	shared_ptr<Output> create_output(
		shared_ptr<Device> device,
		map<string, Glib::VariantBase> options =
			map<string, Glib::VariantBase>());
	/** Create an output using this format.
	 * @param filename Name of destination file.
	 * @param device Device to output for.
	 * @param options Mapping of (option name, value) pairs. */
	shared_ptr<Output> create_output(string filename,
		shared_ptr<Device> device,
		map<string, Glib::VariantBase> options =
			map<string, Glib::VariantBase>());
	/**
	 * Checks whether a given flag is set.
	 * @param flag Flag to check
	 * @return true if flag is set for this module
	 * @see sr_output_flags
	 */
	bool test_flag(const OutputFlag *flag);
protected:
	OutputFormat(const struct sr_output_module *structure);
	~OutputFormat();
	friend class Context;
	friend class Output;
};

/** An output instance (an output format applied to a device) */
class SR_API Output : public UserOwned<Output, const struct sr_output>
{
public:
	/** Update output with data from the given packet.
	 * @param packet Packet to handle. */
	string receive(shared_ptr<Packet> packet);
protected:
	Output(shared_ptr<OutputFormat> format, shared_ptr<Device> device);
	Output(shared_ptr<OutputFormat> format,
		shared_ptr<Device> device, map<string, Glib::VariantBase> options);
	Output(string filename, shared_ptr<OutputFormat> format,
		shared_ptr<Device> device, map<string, Glib::VariantBase> options);
	~Output();
	const shared_ptr<OutputFormat> _format;
	const shared_ptr<Device> _device;
	const map<string, Glib::VariantBase> _options;
	friend class Deleter;
	friend class OutputFormat;
};

/** Base class for objects which wrap an enumeration value from libsigrok */
template <class Class, typename Enum> class SR_API EnumValue
{
public:
	/** The integer constant associated with this value. */
	int id() const
	{
		return static_cast<int>(_id);
	}
	/** The name associated with this value. */
	string name() const
	{
		return _name;
	}
	/** Get value associated with a given integer constant. */
	static const Class *get(int id)
	{
		auto key = static_cast<Enum>(id);
		if (_values.find(key) == _values.end())
			throw Error(SR_ERR_ARG);
		return _values.at(key);
	}
	/** Get possible values. */
	static std::vector<const Class *> values()
	{
		std::vector<const Class *> result;
		for (auto entry : _values)
			result.push_back(entry.second);
		return result;
	}
protected:
	EnumValue(Enum id, const char name[]) : _id(id), _name(name)
	{
	}
	~EnumValue()
	{
	}
	static const std::map<const Enum, const Class * const> _values;
	const Enum _id;
	const string _name;
};

#include <libsigrokcxx/enums.hpp>

}

#endif
