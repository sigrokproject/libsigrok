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

#include <sstream>

namespace sigrok
{

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

/** Helper function to convert between map<string, VariantBase> and GHashTable */
static GHashTable *map_to_hash_variant(map<string, Glib::VariantBase> input)
{
	auto output = g_hash_table_new_full(
		g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_variant_unref);
	for (auto entry : input)
		g_hash_table_insert(output,
			g_strdup(entry.first.c_str()),
			entry.second.gobj_copy());
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
	UserOwned(structure),
	session(NULL)
{
	check(sr_init(&structure));

	struct sr_dev_driver **driver_list = sr_driver_list();
	if (driver_list)
		for (int i = 0; driver_list[i]; i++)
			drivers[driver_list[i]->name] =
				new Driver(driver_list[i]);
	const struct sr_input_module **input_list = sr_input_list();
	if (input_list)
		for (int i = 0; input_list[i]; i++)
			input_formats[sr_input_id_get(input_list[i])] =
				new InputFormat(input_list[i]);
	const struct sr_output_module **output_list = sr_output_list();
	if (output_list)
		for (int i = 0; output_list[i]; i++)
			output_formats[sr_output_id_get(output_list[i])] =
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
		result[name] = driver->get_shared_pointer(this);
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
		result[name] = input_format->get_shared_pointer(this);
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
		result[name] = output_format->get_shared_pointer(this);
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

shared_ptr<Input> Context::open_file(string filename)
{
	const struct sr_input *input;

	check( sr_input_scan_file(filename.c_str(), &input));
	return shared_ptr<Input>(
		new Input(shared_from_this(), input), Input::Deleter());
}

shared_ptr<Input> Context::open_stream(string header)
{
	const struct sr_input *input;

	auto gstr = g_string_new(header.c_str());
	auto ret = sr_input_scan_buffer(gstr, &input);
	g_string_free(gstr, true);
	check(ret);
	return shared_ptr<Input>(
		new Input(shared_from_this(), input), Input::Deleter());
}

Driver::Driver(struct sr_dev_driver *structure) :
	ParentOwned(structure),
	Configurable(structure, NULL, NULL),
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
		result.push_back(device->get_shared_pointer(parent));
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

Glib::VariantContainerBase Configurable::config_list(const ConfigKey *key)
{
	GVariant *data;
	check(sr_config_list(
		config_driver, config_sdi, config_channel_group,
		key->get_id(), &data));
	return Glib::VariantContainerBase(data);
}

vector<const ConfigKey *> Configurable::config_keys(const ConfigKey *key)
{
	GVariant *gvar_opts;
	gsize num_opts;
	const int32_t *opts;
	vector<const ConfigKey *> result;

	check(sr_config_list(
		config_driver, config_sdi, config_channel_group,
		key->get_id(), &gvar_opts));

	opts = (const int32_t *) g_variant_get_fixed_array(
		gvar_opts, &num_opts, sizeof(int32_t));

	for (gsize i = 0; i < num_opts; i++)
		result.push_back(ConfigKey::get(opts[i]));

	g_variant_unref(gvar_opts);

	return result;
}

bool Configurable::config_check(const ConfigKey *key,
	const ConfigKey *index_key)
{
	GVariant *gvar_opts;
	gsize num_opts;
	const int32_t *opts;

	if (sr_config_list(config_driver, config_sdi, config_channel_group,
			index_key->get_id(), &gvar_opts) != SR_OK)
		return false;

	opts = (const int32_t *) g_variant_get_fixed_array(
		gvar_opts, &num_opts, sizeof(int32_t));

	for (gsize i = 0; i < num_opts; i++)
	{
		if (opts[i] == key->get_id())
		{
			g_variant_unref(gvar_opts);
			return true;
		}
	}

	g_variant_unref(gvar_opts);

	return false;
}

Device::Device(struct sr_dev_inst *structure) :
	Configurable(structure->driver, structure, NULL),
	structure(structure)
{
	for (GSList *entry = structure->channels; entry; entry = entry->next)
	{
		auto channel = (struct sr_channel *) entry->data;
		channels[channel] = new Channel(channel);
	}

	for (GSList *entry = structure->channel_groups; entry; entry = entry->next)
	{
		auto group = (struct sr_channel_group *) entry->data;
		channel_groups[group->name] = new ChannelGroup(this, group);
	}
}

Device::~Device()
{
	for (auto entry : channels)
		delete entry.second;
	for (auto entry : channel_groups)
		delete entry.second;
}

string Device::get_description()
{
	ostringstream s;

	vector<string> parts =
		{get_vendor(), get_model(), get_version()};

	for (string part : parts)
		if (part.length() > 0)
			s << part;

	return s.str();
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
	for (auto entry : channels)
		result.push_back(entry.second->get_shared_pointer(get_shared_from_this()));
	return result;
}

shared_ptr<Channel> Device::get_channel(struct sr_channel *ptr)
{
	return channels[ptr]->get_shared_pointer(get_shared_from_this());
}

map<string, shared_ptr<ChannelGroup>>
Device::get_channel_groups()
{
	map<string, shared_ptr<ChannelGroup>> result;
	for (auto entry: channel_groups)
	{
		auto name = entry.first;
		auto channel_group = entry.second;
		result[name] = channel_group->get_shared_pointer(get_shared_from_this());
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
	ParentOwned(structure),
	Device(structure),
	driver(driver)
{
}

HardwareDevice::~HardwareDevice()
{
}

shared_ptr<Device> HardwareDevice::get_shared_from_this()
{
	return static_pointer_cast<Device>(shared_from_this());
}

shared_ptr<Driver> HardwareDevice::get_driver()
{
	return driver->get_shared_pointer(parent);
}

Channel::Channel(struct sr_channel *structure) :
	ParentOwned(structure),
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

unsigned int Channel::get_index()
{
	return structure->index;
}

ChannelGroup::ChannelGroup(Device *device,
		struct sr_channel_group *structure) :
	ParentOwned(structure),
	Configurable(device->structure->driver, device->structure, structure)
{
	for (GSList *entry = structure->channels; entry; entry = entry->next)
		channels.push_back(device->channels[(struct sr_channel *)entry->data]);
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
		result.push_back(channel->get_shared_pointer(parent));
	return result;
}

Trigger::Trigger(shared_ptr<Context> context, string name) : 
	UserOwned(sr_trigger_new(name.c_str())),
	context(context)
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
		result.push_back(stage->get_shared_pointer(this));
	return result;
}

shared_ptr<TriggerStage> Trigger::add_stage()
{
	auto stage = new TriggerStage(sr_trigger_stage_add(structure));
	stages.push_back(stage);
	return stage->get_shared_pointer(this);
}

TriggerStage::TriggerStage(struct sr_trigger_stage *structure) : 
	ParentOwned(structure)
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
		result.push_back(match->get_shared_pointer(this));
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
	ParentOwned(structure), channel(channel)
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
	auto packet = shared_ptr<Packet>(new Packet(device, pkt), Packet::Deleter());
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
	UserOwned(structure),
	context(context), saving(false)
{
	check(sr_session_new(&structure));
	context->session = this;
}

Session::Session(shared_ptr<Context> context, string filename) :
	UserOwned(structure),
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

void Session::append(shared_ptr<Packet> packet)
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

				check(sr_config_get(packet->device->structure->driver,
					packet->device->structure, NULL, SR_CONF_SAMPLERATE,
					&samplerate));

				save_samplerate = g_variant_get_uint64(samplerate);

				g_variant_unref(samplerate);
			}

			if (!save_initialized)
			{
				vector<shared_ptr<Channel>> save_channels;

				for (auto channel : packet->device->get_channels())
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

void Session::append(void *data, size_t length, unsigned int unit_size)
{
	check(sr_session_append(structure, save_filename.c_str(),
		(uint8_t *) data, unit_size, length));
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

Packet::Packet(shared_ptr<Device> device,
	const struct sr_datafeed_packet *structure) :
	UserOwned(structure),
	device(device)
{
	switch (structure->type)
	{
		case SR_DF_HEADER:
			payload = new Header(
				static_cast<const struct sr_datafeed_header *>(
					structure->payload));
			break;
		case SR_DF_META:
			payload = new Meta(
				static_cast<const struct sr_datafeed_meta *>(
					structure->payload));
			break;
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
			payload = nullptr;
			break;
	}
}

Packet::~Packet()
{
	if (payload)
		delete payload;
}

const PacketType *Packet::get_type()
{
	return PacketType::get(structure->type);
}

shared_ptr<PacketPayload> Packet::get_payload()
{
	if (payload)
		return payload->get_shared_pointer(this);
	else
		throw Error(SR_ERR_NA);
}

PacketPayload::PacketPayload()
{
}

PacketPayload::~PacketPayload()
{
}

Header::Header(const struct sr_datafeed_header *structure) :
	ParentOwned(structure),
	PacketPayload()
{
}

Header::~Header()
{
}

shared_ptr<PacketPayload> Header::get_shared_pointer(Packet *parent)
{
	return static_pointer_cast<PacketPayload>(
		ParentOwned::get_shared_pointer(parent));
}

int Header::get_feed_version()
{
	return structure->feed_version;
}

Glib::TimeVal Header::get_start_time()
{
	return Glib::TimeVal(
		structure->starttime.tv_sec,
		structure->starttime.tv_usec);
}

Meta::Meta(const struct sr_datafeed_meta *structure) :
	ParentOwned(structure),
	PacketPayload()
{
}

Meta::~Meta()
{
}

shared_ptr<PacketPayload> Meta::get_shared_pointer(Packet *parent)
{
	return static_pointer_cast<PacketPayload>(
		ParentOwned::get_shared_pointer(parent));
}

map<const ConfigKey *, Glib::VariantBase> Meta::get_config()
{
	map<const ConfigKey *, Glib::VariantBase> result;
	for (auto l = structure->config; l; l = l->next)
	{
		auto config = (struct sr_config *) l->data;
		result[ConfigKey::get(config->key)] = Glib::VariantBase(config->data);
	}
	return result;
}

Logic::Logic(const struct sr_datafeed_logic *structure) :
	ParentOwned(structure),
	PacketPayload()
{
}

Logic::~Logic()
{
}

shared_ptr<PacketPayload> Logic::get_shared_pointer(Packet *parent)
{
	return static_pointer_cast<PacketPayload>(
		ParentOwned::get_shared_pointer(parent));
}

void *Logic::get_data_pointer()
{
	return structure->data;
}

size_t Logic::get_data_length()
{
	return structure->length;
}

unsigned int Logic::get_unit_size()
{
	return structure->unitsize;
}

Analog::Analog(const struct sr_datafeed_analog *structure) :
	ParentOwned(structure),
	PacketPayload()
{
}

Analog::~Analog()
{
}

shared_ptr<PacketPayload> Analog::get_shared_pointer(Packet *parent)
{
	return static_pointer_cast<PacketPayload>(
		ParentOwned::get_shared_pointer(parent));
}

float *Analog::get_data_pointer()
{
	return structure->data;
}

unsigned int Analog::get_num_samples()
{
	return structure->num_samples;
}

vector<shared_ptr<Channel>> Analog::get_channels()
{
	vector<shared_ptr<Channel>> result;
	for (auto l = structure->channels; l; l = l->next)
		result.push_back(parent->device->get_channel(
			(struct sr_channel *)l->data));
	return result;
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

InputFormat::InputFormat(const struct sr_input_module *structure) :
	ParentOwned(structure)
{
}

InputFormat::~InputFormat()
{
}

string InputFormat::get_name()
{
	return valid_string(sr_input_id_get(structure));
}

string InputFormat::get_description()
{
	return valid_string(sr_input_description_get(structure));
}

map<string, shared_ptr<Option>> InputFormat::get_options()
{
	const struct sr_option **options = sr_input_options_get(structure);
	auto option_array = shared_ptr<const struct sr_option *>(
		options, sr_input_options_free);
	map<string, shared_ptr<Option>> result;
	for (int i = 0; options[i]; i++)
		result[options[i]->id] = shared_ptr<Option>(
			new Option(options[i], option_array), Option::Deleter());
	return result;
}

shared_ptr<Input> InputFormat::create_input(
	map<string, Glib::VariantBase> options)
{
	auto input = sr_input_new(structure, map_to_hash_variant(options));
	if (!input)
		throw Error(SR_ERR_ARG);
	return shared_ptr<Input>(
		new Input(parent->shared_from_this(), input), Input::Deleter());
}

Input::Input(shared_ptr<Context> context, const struct sr_input *structure) :
	UserOwned(structure),
	context(context),
	device(nullptr)
{
}

shared_ptr<InputDevice> Input::get_device()
{
	if (!device)
	{
		auto sdi = sr_input_dev_inst_get(structure);
		if (!sdi)
			throw Error(SR_ERR_NA);
		device = new InputDevice(shared_from_this(), sdi);
	}

	return device->get_shared_pointer(shared_from_this());
}

void Input::send(string data)
{
	auto gstr = g_string_new(data.c_str());
	auto ret = sr_input_send(structure, gstr);
	g_string_free(gstr, false);
	check(ret);
}

Input::~Input()
{
	if (device)
		delete device;
	check(sr_input_free(structure));
}

InputDevice::InputDevice(shared_ptr<Input> input,
		struct sr_dev_inst *structure) :
	ParentOwned(structure),
	Device(structure),
	input(input)
{
}

InputDevice::~InputDevice()
{
}

shared_ptr<Device> InputDevice::get_shared_from_this()
{
	return static_pointer_cast<Device>(shared_from_this());
}

Option::Option(const struct sr_option *structure,
		shared_ptr<const struct sr_option *> structure_array) :
	UserOwned(structure),
	structure_array(structure_array)
{
}

Option::~Option()
{
}

string Option::get_id()
{
	return valid_string(structure->id);
}

string Option::get_name()
{
	return valid_string(structure->name);
}

string Option::get_description()
{
	return valid_string(structure->desc);
}

Glib::VariantBase Option::get_default_value()
{
	return Glib::VariantBase(structure->def, true);
}

vector<Glib::VariantBase> Option::get_values()
{
	vector<Glib::VariantBase> result;
	for (auto l = structure->values; l; l = l->next)
		result.push_back(Glib::VariantBase((GVariant *) l->data, true));
	return result;
}

OutputFormat::OutputFormat(const struct sr_output_module *structure) :
	ParentOwned(structure)
{
}

OutputFormat::~OutputFormat()
{
}

string OutputFormat::get_name()
{
	return valid_string(sr_output_id_get(structure));
}

string OutputFormat::get_description()
{
	return valid_string(sr_output_description_get(structure));
}

map<string, shared_ptr<Option>> OutputFormat::get_options()
{
	const struct sr_option **options = sr_output_options_get(structure);
	auto option_array = shared_ptr<const struct sr_option *>(
		options, sr_output_options_free);
	map<string, shared_ptr<Option>> result;
	for (int i = 0; options[i]; i++)
		result[options[i]->id] = shared_ptr<Option>(
			new Option(options[i], option_array), Option::Deleter());
	return result;
}

shared_ptr<Output> OutputFormat::create_output(
	shared_ptr<Device> device, map<string, Glib::VariantBase> options)
{
	return shared_ptr<Output>(
		new Output(shared_from_this(), device, options),
		Output::Deleter());
}

Output::Output(shared_ptr<OutputFormat> format,
		shared_ptr<Device> device, map<string, Glib::VariantBase> options) :
	UserOwned(sr_output_new(format->structure,
		map_to_hash_variant(options), device->structure)),
	format(format), device(device), options(options)
{
}

Output::~Output()
{
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
