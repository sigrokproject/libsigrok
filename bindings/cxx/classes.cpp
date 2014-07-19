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

#include "libsigrok/libsigrok.hpp"

namespace sigrok
{

/** Custom shared_ptr deleter for children owned by their parent object. */
template <class T> void reset_parent(T *child)
{
	child->parent.reset();
}

/** Helper function to translate C errors to C++ exceptions. */
static void check(int result)
{
	if (result != SR_OK)
		throw Error(result);
}

/** Helper function to obtain valid strings from possibly null input. */
static const char *valid_string(const char *input)
{
	if (input != NULL)
		return input;
	else
		return "";
}

/** Helper function to convert between map<string, string> and GHashTable */

static GHashTable *map_to_hash(map<string, string> input)
{
	auto output = g_hash_table_new_full(
		g_str_hash, g_str_equal, g_free, g_free);
	for (auto entry : input)
		g_hash_table_insert(output,
			g_strdup(entry.first.c_str()),
			g_strdup(entry.second.c_str()));
    return output;
}

Error::Error(int result) : result(result)
{
}

const char *Error::what() const throw()
{
	return sr_strerror(result);
}

Error::~Error() throw()
{
}

shared_ptr<Context> Context::create()
{
	return shared_ptr<Context>(new Context(), Context::Deleter());
}

Context::Context() :
	session(NULL)
{
	check(sr_init(&structure));
	struct sr_dev_driver **driver_list = sr_driver_list();
	if (driver_list)
		for (int i = 0; driver_list[i]; i++)
			drivers[driver_list[i]->name] =
				new Driver(driver_list[i]);
	struct sr_input_format **input_list = sr_input_list();
	if (input_list)
		for (int i = 0; input_list[i]; i++)
			input_formats[input_list[i]->id] =
				new InputFormat(input_list[i]);
	struct sr_output_format **output_list = sr_output_list();
	if (output_list)
		for (int i = 0; output_list[i]; i++)
			output_formats[output_list[i]->id] =
				new OutputFormat(output_list[i]);
}

string Context::get_package_version()
{
	return sr_package_version_string_get();
}

string Context::get_lib_version()
{
	return sr_lib_version_string_get();
}

map<string, shared_ptr<Driver>> Context::get_drivers()
{
	map<string, shared_ptr<Driver>> result;
	for (auto entry: drivers)
	{
		auto name = entry.first;
		auto driver = entry.second;
		driver->parent = static_pointer_cast<Context>(shared_from_this());
		result[name] = shared_ptr<Driver>(driver, reset_parent<Driver>);
	}
	return result;
}

map<string, shared_ptr<InputFormat>> Context::get_input_formats()
{
	map<string, shared_ptr<InputFormat>> result;
	for (auto entry: input_formats)
	{
		auto name = entry.first;
		auto input_format = entry.second;
		input_format->parent = static_pointer_cast<Context>(shared_from_this());
		result[name] = shared_ptr<InputFormat>(input_format,
			reset_parent<InputFormat>);
	}
	return result;
}

map<string, shared_ptr<OutputFormat>> Context::get_output_formats()
{
	map<string, shared_ptr<OutputFormat>> result;
	for (auto entry: output_formats)
	{
		auto name = entry.first;
		auto output_format = entry.second;
		output_format->parent = static_pointer_cast<Context>(shared_from_this());
		result[name] = shared_ptr<OutputFormat>(output_format,
			reset_parent<OutputFormat>);
	}
	return result;
}

Context::~Context()
{
	for (auto entry : drivers)
		delete entry.second;
	for (auto entry : input_formats)
		delete entry.second;
	for (auto entry : output_formats)
		delete entry.second;
	check(sr_exit(structure));
}

const LogLevel *Context::get_log_level()
{
	return LogLevel::get(sr_log_loglevel_get());
}

void Context::set_log_level(const LogLevel *level)
{
	check(sr_log_loglevel_set(level->get_id()));
}

string Context::get_log_domain()
{
	return valid_string(sr_log_logdomain_get());
}

void Context::set_log_domain(string value)
{
	check(sr_log_logdomain_set(value.c_str()));
}

static int call_log_callback(void *cb_data, int loglevel, const char *format, va_list args)
{
	va_list args_copy;
	va_copy(args_copy, args);
	int length = vsnprintf(NULL, 0, format, args_copy);
	va_end(args_copy);
	char *buf = (char *) g_malloc(length + 1);
	vsprintf(buf, format, args);
	string message(buf, length);
	g_free(buf);

	LogCallbackFunction callback = *((LogCallbackFunction *) cb_data);

	try
	{
		callback(LogLevel::get(loglevel), message);
	}
	catch (Error e)
	{
		return e.result;
	}

	return SR_OK;
}

void Context::set_log_callback(LogCallbackFunction callback)
{
	log_callback = callback;
	check(sr_log_callback_set(call_log_callback, &log_callback));
} 

void Context::set_log_callback_default()
{
	check(sr_log_callback_set_default());
	log_callback = nullptr;
} 

shared_ptr<Session> Context::create_session()
{
	return shared_ptr<Session>(
		new Session(shared_from_this()), Session::Deleter());
}

shared_ptr<Session> Context::load_session(string filename)
{
	return shared_ptr<Session>(
		new Session(shared_from_this(), filename), Session::Deleter());
}

shared_ptr<Trigger> Context::create_trigger(string name)
{
	return shared_ptr<Trigger>(
		new Trigger(shared_from_this(), name), Trigger::Deleter());
}

Driver::Driver(struct sr_dev_driver *structure) :
	StructureWrapper<Context, struct sr_dev_driver>(structure),
	initialized(false)
{
}

Driver::~Driver()
{
	for (auto device : devices)
		delete device;
}

string Driver::get_name()
{
	return valid_string(structure->name);
}

string Driver::get_long_name()
{
	return valid_string(structure->longname);
}

vector<shared_ptr<HardwareDevice>> Driver::scan(
	map<const ConfigKey *, Glib::VariantBase> options)
{
	/* Initialise the driver if not yet done. */
	if (!initialized)
	{
		check(sr_driver_init(parent->structure, structure));
		initialized = true;
	}

	/* Clear all existing instances. */
	for (auto device : devices)
		delete device;
	devices.clear();

	/* Translate scan options to GSList of struct sr_config pointers. */
	GSList *option_list = NULL;
	for (auto entry : options)
	{
		auto key = entry.first;
		auto value = entry.second;
		auto config = g_new(struct sr_config, 1);
		config->key = key->get_id();
		config->data = value.gobj();
		option_list = g_slist_append(option_list, config);
	}

	/* Run scan. */
	GSList *device_list = sr_driver_scan(structure, option_list);

	/* Free option list. */
	g_slist_free_full(option_list, g_free);

	/* Create device objects. */
	for (GSList *device = device_list; device; device = device->next)
	{
		auto sdi = (struct sr_dev_inst *) device->data;
		devices.push_back(new HardwareDevice(this, sdi));
	}

	/* Free GSList returned from scan. */
	g_slist_free(device_list);

	/* Create list of shared pointers to device instances for return. */
	vector<shared_ptr<HardwareDevice>> result;
	for (auto device : devices)
	{
		device->parent = parent->shared_from_this();
		result.push_back(shared_ptr<HardwareDevice>(device,
			reset_parent<HardwareDevice>));
	}
	return result;
}

Configurable::Configurable(
		struct sr_dev_driver *driver,
		struct sr_dev_inst *sdi,
		struct sr_channel_group *cg) :
	config_driver(driver),
	config_sdi(sdi),
	config_channel_group(cg)
{
}

Configurable::~Configurable()
{
}

Glib::VariantBase Configurable::config_get(const ConfigKey *key)
{
	GVariant *data;
	check(sr_config_get(
		config_driver, config_sdi, config_channel_group,
		key->get_id(), &data));
	return Glib::VariantBase(data);
}

void Configurable::config_set(const ConfigKey *key, Glib::VariantBase value)
{
	check(sr_config_set(
		config_sdi, config_channel_group,
		key->get_id(), value.gobj()));
}

Glib::VariantBase Configurable::config_list(const ConfigKey *key)
{
	GVariant *data;
	check(sr_config_list(
		config_driver, config_sdi, config_channel_group,
		key->get_id(), &data));
	return Glib::VariantBase(data);
}

Device::Device(struct sr_dev_inst *structure) :
	Configurable(structure->driver, structure, NULL),
	StructureWrapper<Context, struct sr_dev_inst>(structure)
{
	for (GSList *entry = structure->channels; entry; entry = entry->next)
	{
		auto channel = (struct sr_channel *) entry->data;
		channels.push_back(new Channel(channel));
	}
}

Device::~Device()
{
	for (auto channel : channels)
		delete channel;
}

string Device::get_vendor()
{
	return valid_string(structure->vendor);
}

string Device::get_model()
{
	return valid_string(structure->model);
}

string Device::get_version()
{
	return valid_string(structure->version);
}

vector<shared_ptr<Channel>> Device::get_channels()
{
	vector<shared_ptr<Channel>> result;
	for (auto channel : channels)
	{
		channel->parent = static_pointer_cast<Device>(shared_from_this());
		result.push_back(shared_ptr<Channel>(channel, reset_parent<Channel>));
	}
	return result;
}

void Device::open()
{
	check(sr_dev_open(structure));
}

void Device::close()
{
	check(sr_dev_close(structure));
}

HardwareDevice::HardwareDevice(Driver *driver, struct sr_dev_inst *structure) :
	Device(structure),
	driver(driver)
{
	for (GSList *entry = structure->channel_groups; entry; entry = entry->next)
	{
		auto group = (struct sr_channel_group *) entry->data;
		channel_groups[group->name] = new ChannelGroup(this, group);
	}
}

HardwareDevice::~HardwareDevice()
{
	for (auto entry : channel_groups)
		delete entry.second;
}

shared_ptr<Driver> HardwareDevice::get_driver()
{
	return static_pointer_cast<Driver>(driver->shared_from_this());
}

map<string, shared_ptr<ChannelGroup>>
HardwareDevice::get_channel_groups()
{
	map<string, shared_ptr<ChannelGroup>> result;
	for (auto entry: channel_groups)
	{
		auto name = entry.first;
		auto channel_group = entry.second;
		channel_group->parent =
			static_pointer_cast<HardwareDevice>(shared_from_this());
		result[name] = shared_ptr<ChannelGroup>(channel_group,
			reset_parent<ChannelGroup>);
	}
	return result;
}

Channel::Channel(struct sr_channel *structure) :
	StructureWrapper<Device, struct sr_channel>(structure),
	type(ChannelType::get(structure->type))
{
}

Channel::~Channel()
{
}

string Channel::get_name()
{
	return valid_string(structure->name);
}

void Channel::set_name(string name)
{
	check(sr_dev_channel_name_set(parent->structure, structure->index, name.c_str()));
}

const ChannelType *Channel::get_type()
{
	return ChannelType::get(structure->type);
}

bool Channel::get_enabled()
{
	return structure->enabled;
}

void Channel::set_enabled(bool value)
{
	check(sr_dev_channel_enable(parent->structure, structure->index, value));
}

ChannelGroup::ChannelGroup(HardwareDevice *device,
		struct sr_channel_group *structure) :
	StructureWrapper<HardwareDevice, struct sr_channel_group>(structure),
	Configurable(device->structure->driver, device->structure, structure)
{
	for (GSList *entry = structure->channels; entry; entry = entry->next)
	{
		auto channel = (struct sr_channel *) entry->data;
		for (auto device_channel : device->channels)
			if (channel == device_channel->structure)
				channels.push_back(device_channel);
	}
}

ChannelGroup::~ChannelGroup()
{
}

string ChannelGroup::get_name()
{
	return valid_string(structure->name);
}

vector<shared_ptr<Channel>> ChannelGroup::get_channels()
{
	vector<shared_ptr<Channel>> result;
	for (auto channel : channels)
	{
		channel->parent = static_pointer_cast<Device>(parent->shared_from_this());
		result.push_back(shared_ptr<Channel>(channel, reset_parent<Channel>));
	}
	return result;
}

Trigger::Trigger(shared_ptr<Context> context, string name) : 
	structure(sr_trigger_new(name.c_str())), context(context)
{
	for (auto stage = structure->stages; stage; stage = stage->next)
		stages.push_back(new TriggerStage((struct sr_trigger_stage *) stage->data));
}

Trigger::~Trigger()
{
	for (auto stage: stages)
		delete stage;

	sr_trigger_free(structure);
}

string Trigger::get_name()
{
	return structure->name;
}

vector<shared_ptr<TriggerStage>> Trigger::get_stages()
{
	vector<shared_ptr<TriggerStage>> result;
	for (auto stage : stages)
	{
		stage->parent = static_pointer_cast<Trigger>(shared_from_this());
		result.push_back(shared_ptr<TriggerStage>(stage, reset_parent<TriggerStage>));
	}
	return result;
}

shared_ptr<TriggerStage> Trigger::add_stage()
{
	auto stage = new TriggerStage(sr_trigger_stage_add(structure));
	stages.push_back(stage);
	stage->parent = static_pointer_cast<Trigger>(shared_from_this());
	return shared_ptr<TriggerStage>(stage, reset_parent<TriggerStage>);
}

TriggerStage::TriggerStage(struct sr_trigger_stage *structure) : 
	StructureWrapper<Trigger, struct sr_trigger_stage>(structure)
{
}

TriggerStage::~TriggerStage()
{
	for (auto match : matches)
		delete match;
}
	
int TriggerStage::get_number()
{
	return structure->stage;
}

vector<shared_ptr<TriggerMatch>> TriggerStage::get_matches()
{
	vector<shared_ptr<TriggerMatch>> result;
	for (auto match : matches)
	{
		match->parent = static_pointer_cast<TriggerStage>(shared_from_this());
		result.push_back(shared_ptr<TriggerMatch>(match, reset_parent<TriggerMatch>));
	}
	return result;
}

void TriggerStage::add_match(shared_ptr<Channel> channel, const TriggerMatchType *type, float value)
{
	check(sr_trigger_match_add(structure, channel->structure, type->get_id(), value));
	matches.push_back(new TriggerMatch(
		(struct sr_trigger_match *) g_slist_last(structure->matches)->data, channel));
}

void TriggerStage::add_match(shared_ptr<Channel> channel, const TriggerMatchType *type)
{
	add_match(channel, type, NAN);
}

TriggerMatch::TriggerMatch(struct sr_trigger_match *structure, shared_ptr<Channel> channel) :
	StructureWrapper<TriggerStage, struct sr_trigger_match>(structure), channel(channel)
{
}

TriggerMatch::~TriggerMatch()
{
}

shared_ptr<Channel> TriggerMatch::get_channel()
{
	return channel;
}

const TriggerMatchType *TriggerMatch::get_type()
{
	return TriggerMatchType::get(structure->match);
}

float TriggerMatch::get_value()
{
	return structure->value;
}

DatafeedCallbackData::DatafeedCallbackData(Session *session,
		DatafeedCallbackFunction callback) :
	callback(callback), session(session)
{
}

void DatafeedCallbackData::run(const struct sr_dev_inst *sdi,
	const struct sr_datafeed_packet *pkt)
{
	auto device = session->devices[sdi];
	auto packet = shared_ptr<Packet>(new Packet(pkt), Packet::Deleter());
	callback(device, packet);
}

SourceCallbackData::SourceCallbackData(shared_ptr<EventSource> source) :
	source(source)
{
}

bool SourceCallbackData::run(int revents)
{
	return source->callback((Glib::IOCondition) revents);
}

shared_ptr<EventSource> EventSource::create(int fd, Glib::IOCondition events,
	int timeout, SourceCallbackFunction callback)
{
	auto result = new EventSource(timeout, callback);
	result->type = EventSource::SOURCE_FD;
	result->fd = fd;
	result->events = events;
	return shared_ptr<EventSource>(result, EventSource::Deleter());
}

shared_ptr<EventSource> EventSource::create(Glib::PollFD pollfd, int timeout,
	SourceCallbackFunction callback)
{
	auto result = new EventSource(timeout, callback);
	result->type = EventSource::SOURCE_POLLFD;
	result->pollfd = pollfd;
	return shared_ptr<EventSource>(result, EventSource::Deleter());
}

shared_ptr<EventSource> EventSource::create(Glib::RefPtr<Glib::IOChannel> channel,
	Glib::IOCondition events, int timeout, SourceCallbackFunction callback)
{
	auto result = new EventSource(timeout, callback);
	result->type = EventSource::SOURCE_IOCHANNEL;
	result->channel = channel;
	result->events = events;
	return shared_ptr<EventSource>(result, EventSource::Deleter());
}

EventSource::EventSource(int timeout, SourceCallbackFunction callback) :
	timeout(timeout), callback(callback)
{
}

EventSource::~EventSource()
{
}

Session::Session(shared_ptr<Context> context) :
	context(context), saving(false)
{
	check(sr_session_new(&structure));
	context->session = this;
}

Session::Session(shared_ptr<Context> context, string filename) :
	context(context), saving(false)
{
	check(sr_session_load(filename.c_str(), &structure));
	context->session = this;
}

Session::~Session()
{
	check(sr_session_destroy(structure));

	for (auto callback : datafeed_callbacks)
		delete callback;

	for (auto entry : source_callbacks)
		delete entry.second;
}

void Session::add_device(shared_ptr<Device> device)
{
	check(sr_session_dev_add(structure, device->structure));
	devices[device->structure] = device;
}

vector<shared_ptr<Device>> Session::get_devices()
{
	GSList *dev_list;
	check(sr_session_dev_list(structure, &dev_list));
	vector<shared_ptr<Device>> result;
	for (GSList *dev = dev_list; dev; dev = dev->next)
	{
		auto sdi = (struct sr_dev_inst *) dev->data;
		if (devices.count(sdi) == 0)
			devices[sdi] = shared_ptr<Device>(
				new Device(sdi), Device::Deleter());
		result.push_back(devices[sdi]);
	}
	return result;
}

void Session::remove_devices()
{
	devices.clear();
	check(sr_session_dev_remove_all(structure));
}

void Session::start()
{
	check(sr_session_start(structure));
}

void Session::run()
{
	check(sr_session_run(structure));
}

void Session::stop()
{
	check(sr_session_stop(structure));
}

void Session::begin_save(string filename)
{
	saving = true;
	save_initialized = false;
	save_filename = filename;
	save_samplerate = 0;
}

void Session::append(shared_ptr<Device> device, shared_ptr<Packet> packet)
{
	if (!saving)
		throw Error(SR_ERR);

	switch (packet->structure->type)
	{
		case SR_DF_META:
		{
			auto meta = (const struct sr_datafeed_meta *)
				packet->structure->payload;

			for (auto l = meta->config; l; l = l->next)
			{
				auto config = (struct sr_config *) l->data;
				if (config->key == SR_CONF_SAMPLERATE)
					save_samplerate = g_variant_get_uint64(config->data);
			}

			break;
		}
		case SR_DF_LOGIC:
		{
			if (save_samplerate == 0)
			{
				GVariant *samplerate;

				check(sr_config_get(device->structure->driver,
					device->structure, NULL, SR_CONF_SAMPLERATE, &samplerate));

				save_samplerate = g_variant_get_uint64(samplerate);

				g_variant_unref(samplerate);
			}

			if (!save_initialized)
			{
				vector<shared_ptr<Channel>> save_channels;

				for (auto channel : device->get_channels())
					if (channel->structure->enabled &&
							channel->structure->type == SR_CHANNEL_LOGIC)
						save_channels.push_back(channel);

				auto channels = g_new(char *, save_channels.size());

				int i = 0;
				for (auto channel : save_channels)
						channels[i++] = channel->structure->name;
				channels[i] = NULL;

				int ret = sr_session_save_init(structure, save_filename.c_str(),
						save_samplerate, channels);

				g_free(channels);

				if (ret != SR_OK)
					throw Error(ret);

				save_initialized = true;
			}

			auto logic = (const struct sr_datafeed_logic *)
				packet->structure->payload;

			check(sr_session_append(structure, save_filename.c_str(),
				(uint8_t *) logic->data, logic->unitsize,
				logic->length / logic->unitsize));
		}
	}
}

static void datafeed_callback(const struct sr_dev_inst *sdi,
	const struct sr_datafeed_packet *pkt, void *cb_data)
{
	auto callback = static_cast<DatafeedCallbackData *>(cb_data);
	callback->run(sdi, pkt);
}
	
void Session::add_datafeed_callback(DatafeedCallbackFunction callback)
{
	auto cb_data = new DatafeedCallbackData(this, callback);
	check(sr_session_datafeed_callback_add(structure, datafeed_callback, cb_data));
	datafeed_callbacks.push_back(cb_data);
}

void Session::remove_datafeed_callbacks(void)
{
	check(sr_session_datafeed_callback_remove_all(structure));
	for (auto callback : datafeed_callbacks)
		delete callback;
	datafeed_callbacks.clear();
}

static int source_callback(int fd, int revents, void *cb_data)
{
	(void) fd;
	auto callback = (SourceCallbackData *) cb_data;
	return callback->run(revents);
}

void Session::add_source(shared_ptr<EventSource> source)
{
	if (source_callbacks.count(source) == 1)
		throw Error(SR_ERR_ARG);

	auto cb_data = new SourceCallbackData(source);

	switch (source->type)
	{
		case EventSource::SOURCE_FD:
			check(sr_session_source_add(structure, source->fd, source->events,
				source->timeout, source_callback, cb_data));
			break;
		case EventSource::SOURCE_POLLFD:
			check(sr_session_source_add_pollfd(structure,
				source->pollfd.gobj(), source->timeout, source_callback,
				cb_data));
			break;
		case EventSource::SOURCE_IOCHANNEL:
			check(sr_session_source_add_channel(structure,
				source->channel->gobj(), source->events, source->timeout,
				source_callback, cb_data));
			break;
	}

	source_callbacks[source] = cb_data;
}

void Session::remove_source(shared_ptr<EventSource> source)
{
	if (source_callbacks.count(source) == 0)
		throw Error(SR_ERR_ARG);

	switch (source->type)
	{
		case EventSource::SOURCE_FD:
			check(sr_session_source_remove(structure, source->fd));
			break;
		case EventSource::SOURCE_POLLFD:
			check(sr_session_source_remove_pollfd(structure,
				source->pollfd.gobj()));
			break;
		case EventSource::SOURCE_IOCHANNEL:
			check(sr_session_source_remove_channel(structure,
				source->channel->gobj()));
			break;
	}

	delete source_callbacks[source];

	source_callbacks.erase(source);
}

shared_ptr<Trigger> Session::get_trigger()
{
	return trigger;
}

void Session::set_trigger(shared_ptr<Trigger> trigger)
{
	check(sr_session_trigger_set(structure, trigger->structure));
	this->trigger = trigger;
}

Packet::Packet(const struct sr_datafeed_packet *structure) :
	structure(structure)
{
	switch (structure->type)
	{
		case SR_DF_LOGIC:
			payload = new Logic(
				static_cast<const struct sr_datafeed_logic *>(
					structure->payload));
			break;
		case SR_DF_ANALOG:
			payload = new Analog(
				static_cast<const struct sr_datafeed_analog *>(
					structure->payload));
			break;
		default:
			payload = NULL;
			break;
	}
}

Packet::~Packet()
{
	delete payload;
}

PacketPayload *Packet::get_payload()
{
	return payload;
}

PacketPayload::PacketPayload()
{
}

PacketPayload::~PacketPayload()
{
}

Logic::Logic(const struct sr_datafeed_logic *structure) : PacketPayload(),
	structure(structure),
	data(static_cast<uint8_t *>(structure->data),
		static_cast<uint8_t *>(structure->data) + structure->length) {}

Logic::~Logic()
{
}

void *Logic::get_data()
{
	return structure->data;
}

size_t Logic::get_data_size()
{
	return structure->length;
}

Analog::Analog(const struct sr_datafeed_analog *structure) :
	PacketPayload(),
	structure(structure)
{
}

Analog::~Analog()
{
}

void *Analog::get_data()
{
	return structure->data;
}

size_t Analog::get_data_size()
{
	return structure->num_samples * sizeof(float);
}

unsigned int Analog::get_num_samples()
{
	return structure->num_samples;
}

const Quantity *Analog::get_mq()
{
	return Quantity::get(structure->mq);
}

const Unit *Analog::get_unit()
{
	return Unit::get(structure->unit);
}

vector<const QuantityFlag *> Analog::get_mq_flags()
{
	return QuantityFlag::flags_from_mask(structure->mqflags);
}

InputFormat::InputFormat(struct sr_input_format *structure) :
	StructureWrapper<Context, struct sr_input_format>(structure)
{
}

InputFormat::~InputFormat()
{
}

string InputFormat::get_name()
{
	return valid_string(structure->id);
}

string InputFormat::get_description()
{
	return valid_string(structure->description);
}

bool InputFormat::format_match(string filename)
{
	return structure->format_match(filename.c_str());
}

shared_ptr<InputFileDevice> InputFormat::open_file(string filename,
		map<string, string> options)
{
	auto input = g_new(struct sr_input, 1);
	input->param = map_to_hash(options);

	/** Run initialisation. */
	check(structure->init(input, filename.c_str()));

	/** Create virtual device. */
	return shared_ptr<InputFileDevice>(new InputFileDevice(
		static_pointer_cast<InputFormat>(shared_from_this()), input, filename),
		InputFileDevice::Deleter());
}

InputFileDevice::InputFileDevice(shared_ptr<InputFormat> format,
		struct sr_input *input, string filename) :
	Device(input->sdi),
	input(input),
	format(format),
	filename(filename)
{
}

InputFileDevice::~InputFileDevice()
{
	g_hash_table_unref(input->param);
	g_free(input);
}

void InputFileDevice::load()
{
	check(format->structure->loadfile(input, filename.c_str()));
}

OutputFormat::OutputFormat(struct sr_output_format *structure) :
	StructureWrapper<Context, struct sr_output_format>(structure)
{
}

OutputFormat::~OutputFormat()
{
}

string OutputFormat::get_name()
{
	return valid_string(structure->id);
}

string OutputFormat::get_description()
{
	return valid_string(structure->description);
}

shared_ptr<Output> OutputFormat::create_output(
	shared_ptr<Device> device, map<string, string> options)
{
	return shared_ptr<Output>(
		new Output(
			static_pointer_cast<OutputFormat>(shared_from_this()),
				device, options),
		Output::Deleter());
}

Output::Output(shared_ptr<OutputFormat> format,
		shared_ptr<Device> device, map<string, string> options) :
	structure(sr_output_new(format->structure,
		map_to_hash(options), device->structure)),
	format(format), device(device), options(options)
{
}

Output::~Output()
{
	g_hash_table_unref(structure->params);
	check(sr_output_free(structure));
}

string Output::receive(shared_ptr<Packet> packet)
{
	GString *out;
	check(sr_output_send(structure, packet->structure, &out));
	if (out)
	{
		auto result = string(out->str, out->str + out->len);
		g_string_free(out, true);
		return result;
	}
	else
	{
		return string();
	}
}

#include "enums.cpp"

}
