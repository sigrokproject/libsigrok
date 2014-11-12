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
#include <cmath>

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
	UserOwned(_structure),
	_session(NULL)
{
	check(sr_init(&_structure));

	struct sr_dev_driver **driver_list = sr_driver_list();
	if (driver_list)
		for (int i = 0; driver_list[i]; i++)
			_drivers[driver_list[i]->name] =
				new Driver(driver_list[i]);
	const struct sr_input_module **input_list = sr_input_list();
	if (input_list)
		for (int i = 0; input_list[i]; i++)
			_input_formats[sr_input_id_get(input_list[i])] =
				new InputFormat(input_list[i]);
	const struct sr_output_module **output_list = sr_output_list();
	if (output_list)
		for (int i = 0; output_list[i]; i++)
			_output_formats[sr_output_id_get(output_list[i])] =
				new OutputFormat(output_list[i]);
}

string Context::package_version()
{
	return sr_package_version_string_get();
}

string Context::lib_version()
{
	return sr_lib_version_string_get();
}

map<string, shared_ptr<Driver>> Context::drivers()
{
	map<string, shared_ptr<Driver>> result;
	for (auto entry: _drivers)
	{
		auto name = entry.first;
		auto driver = entry.second;
		result[name] = driver->get_shared_pointer(this);
	}
	return result;
}

map<string, shared_ptr<InputFormat>> Context::input_formats()
{
	map<string, shared_ptr<InputFormat>> result;
	for (auto entry: _input_formats)
	{
		auto name = entry.first;
		auto input_format = entry.second;
		result[name] = input_format->get_shared_pointer(this);
	}
	return result;
}

map<string, shared_ptr<OutputFormat>> Context::output_formats()
{
	map<string, shared_ptr<OutputFormat>> result;
	for (auto entry: _output_formats)
	{
		auto name = entry.first;
		auto output_format = entry.second;
		result[name] = output_format->get_shared_pointer(this);
	}
	return result;
}

Context::~Context()
{
	for (auto entry : _drivers)
		delete entry.second;
	for (auto entry : _input_formats)
		delete entry.second;
	for (auto entry : _output_formats)
		delete entry.second;
	check(sr_exit(_structure));
}

const LogLevel *Context::log_level()
{
	return LogLevel::get(sr_log_loglevel_get());
}

void Context::set_log_level(const LogLevel *level)
{
	check(sr_log_loglevel_set(level->id()));
}

string Context::log_domain()
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
	_log_callback = callback;
	check(sr_log_callback_set(call_log_callback, &_log_callback));
} 

void Context::set_log_callback_default()
{
	check(sr_log_callback_set_default());
	_log_callback = nullptr;
} 

shared_ptr<Session> Context::create_session()
{
	return shared_ptr<Session>(
		new Session(shared_from_this()), Session::Deleter());
}

shared_ptr<UserDevice> Context::create_user_device(
		string vendor, string model, string version)
{
	return shared_ptr<UserDevice>(
		new UserDevice(vendor, model, version), UserDevice::Deleter());
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

	check(sr_input_scan_file(filename.c_str(), &input));
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
	_initialized(false)
{
}

Driver::~Driver()
{
}

string Driver::name()
{
	return valid_string(_structure->name);
}

string Driver::long_name()
{
	return valid_string(_structure->longname);
}

vector<shared_ptr<HardwareDevice>> Driver::scan(
	map<const ConfigKey *, Glib::VariantBase> options)
{
	/* Initialise the driver if not yet done. */
	if (!_initialized)
	{
		check(sr_driver_init(_parent->_structure, _structure));
		_initialized = true;
	}

	/* Translate scan options to GSList of struct sr_config pointers. */
	GSList *option_list = NULL;
	for (auto entry : options)
	{
		auto key = entry.first;
		auto value = entry.second;
		auto config = g_new(struct sr_config, 1);
		config->key = key->id();
		config->data = value.gobj();
		option_list = g_slist_append(option_list, config);
	}

	/* Run scan. */
	GSList *device_list = sr_driver_scan(_structure, option_list);

	/* Free option list. */
	g_slist_free_full(option_list, g_free);


	/* Create device objects. */
	vector<shared_ptr<HardwareDevice>> result;
	for (GSList *device = device_list; device; device = device->next)
	{
		auto sdi = (struct sr_dev_inst *) device->data;
		result.push_back(shared_ptr<HardwareDevice>(
			new HardwareDevice(shared_from_this(), sdi),
			HardwareDevice::Deleter()));
	}

	/* Free GSList returned from scan. */
	g_slist_free(device_list);

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
		key->id(), &data));
	return Glib::VariantBase(data);
}

void Configurable::config_set(const ConfigKey *key, Glib::VariantBase value)
{
	check(sr_config_set(
		config_sdi, config_channel_group,
		key->id(), value.gobj()));
}

Glib::VariantContainerBase Configurable::config_list(const ConfigKey *key)
{
	GVariant *data;
	check(sr_config_list(
		config_driver, config_sdi, config_channel_group,
		key->id(), &data));
	return Glib::VariantContainerBase(data);
}

map<const ConfigKey *, set<Capability>> Configurable::config_keys(const ConfigKey *key)
{
	GVariant *gvar_opts;
	gsize num_opts;
	const uint32_t *opts;
	map<const ConfigKey *, set<Capability>> result;

	check(sr_config_list(
		config_driver, config_sdi, config_channel_group,
		key->id(), &gvar_opts));

	opts = (const uint32_t *) g_variant_get_fixed_array(
		gvar_opts, &num_opts, sizeof(uint32_t));

	for (gsize i = 0; i < num_opts; i++)
	{
		auto key = ConfigKey::get(opts[i] & SR_CONF_MASK);
		set<Capability> capabilities;
		if (opts[i] & SR_CONF_GET)
			capabilities.insert(GET);
		if (opts[i] & SR_CONF_SET)
			capabilities.insert(SET);
		if (opts[i] & SR_CONF_LIST)
			capabilities.insert(LIST);
		result[key] = capabilities;
	}

	g_variant_unref(gvar_opts);

	return result;
}

bool Configurable::config_check(const ConfigKey *key,
	const ConfigKey *index_key)
{
	GVariant *gvar_opts;
	gsize num_opts;
	const uint32_t *opts;

	if (sr_config_list(config_driver, config_sdi, config_channel_group,
			index_key->id(), &gvar_opts) != SR_OK)
		return false;

	opts = (const uint32_t *) g_variant_get_fixed_array(
		gvar_opts, &num_opts, sizeof(uint32_t));

	for (gsize i = 0; i < num_opts; i++)
	{
		if ((opts[i] & SR_CONF_MASK) == (uint32_t) key->id())
		{
			g_variant_unref(gvar_opts);
			return true;
		}
	}

	g_variant_unref(gvar_opts);

	return false;
}

Device::Device(struct sr_dev_inst *structure) :
	Configurable(sr_dev_inst_driver_get(structure), structure, NULL),
	_structure(structure)
{
	for (GSList *entry = sr_dev_inst_channels_get(structure); entry; entry = entry->next)
	{
		auto channel = (struct sr_channel *) entry->data;
		_channels[channel] = new Channel(channel);
	}

	for (GSList *entry = sr_dev_inst_channel_groups_get(structure); entry; entry = entry->next)
	{
		auto group = (struct sr_channel_group *) entry->data;
		_channel_groups[group->name] = new ChannelGroup(this, group);
	}
}

Device::~Device()
{
	for (auto entry : _channels)
		delete entry.second;
	for (auto entry : _channel_groups)
		delete entry.second;
}

string Device::vendor()
{
	return valid_string(sr_dev_inst_vendor_get(_structure));
}

string Device::model()
{
	return valid_string(sr_dev_inst_model_get(_structure));
}

string Device::version()
{
	return valid_string(sr_dev_inst_version_get(_structure));
}

string Device::serial_number()
{
	return valid_string(sr_dev_inst_sernum_get(_structure));
}

string Device::connection_id()
{
	return valid_string(sr_dev_inst_connid_get(_structure));
}

vector<shared_ptr<Channel>> Device::channels()
{
	vector<shared_ptr<Channel>> result;
	for (auto channel = sr_dev_inst_channels_get(_structure); channel; channel = channel->next)
		result.push_back(
			_channels[(struct sr_channel *) channel->data]->get_shared_pointer(
				get_shared_from_this()));
	return result;
}

shared_ptr<Channel> Device::get_channel(struct sr_channel *ptr)
{
	return _channels[ptr]->get_shared_pointer(get_shared_from_this());
}

map<string, shared_ptr<ChannelGroup>>
Device::channel_groups()
{
	map<string, shared_ptr<ChannelGroup>> result;
	for (auto entry: _channel_groups)
	{
		auto name = entry.first;
		auto channel_group = entry.second;
		result[name] = channel_group->get_shared_pointer(get_shared_from_this());
	}
	return result;
}

void Device::open()
{
	check(sr_dev_open(_structure));
}

void Device::close()
{
	check(sr_dev_close(_structure));
}

HardwareDevice::HardwareDevice(shared_ptr<Driver> driver,
		struct sr_dev_inst *structure) :
	UserOwned(structure),
	Device(structure),
	_driver(driver)
{
}

HardwareDevice::~HardwareDevice()
{
}

shared_ptr<Device> HardwareDevice::get_shared_from_this()
{
	return static_pointer_cast<Device>(shared_from_this());
}

shared_ptr<Driver> HardwareDevice::driver()
{
	return _driver;
}

UserDevice::UserDevice(string vendor, string model, string version) :
	UserOwned(sr_dev_inst_user_new(
		vendor.c_str(), model.c_str(), version.c_str())),
	Device(UserOwned::_structure)
{
}

UserDevice::~UserDevice()
{
}

shared_ptr<Device> UserDevice::get_shared_from_this()
{
	return static_pointer_cast<Device>(shared_from_this());
}

shared_ptr<Channel> UserDevice::add_channel(unsigned int index,
	const ChannelType *type, string name)
{
	check(sr_dev_inst_channel_add(Device::_structure,
		index, type->id(), name.c_str()));
	struct sr_channel *structure = (struct sr_channel *)
			g_slist_last(sr_dev_inst_channels_get(Device::_structure))->data;
	Channel *channel = new Channel(structure);
	_channels[structure] = channel;
	return get_channel(structure);
}

Channel::Channel(struct sr_channel *structure) :
	ParentOwned(structure),
	_type(ChannelType::get(_structure->type))
{
}

Channel::~Channel()
{
}

string Channel::name()
{
	return valid_string(_structure->name);
}

void Channel::set_name(string name)
{
	check(sr_dev_channel_name_set(_parent->_structure,
		_structure->index, name.c_str()));
}

const ChannelType *Channel::type()
{
	return ChannelType::get(_structure->type);
}

bool Channel::enabled()
{
	return _structure->enabled;
}

void Channel::set_enabled(bool value)
{
	check(sr_dev_channel_enable(_parent->_structure, _structure->index, value));
}

unsigned int Channel::index()
{
	return _structure->index;
}

ChannelGroup::ChannelGroup(Device *device,
		struct sr_channel_group *structure) :
	ParentOwned(structure),
	Configurable(sr_dev_inst_driver_get(device->_structure), device->_structure, structure)
{
	for (GSList *entry = structure->channels; entry; entry = entry->next)
		_channels.push_back(device->_channels[(struct sr_channel *)entry->data]);
}

ChannelGroup::~ChannelGroup()
{
}

string ChannelGroup::name()
{
	return valid_string(_structure->name);
}

vector<shared_ptr<Channel>> ChannelGroup::channels()
{
	vector<shared_ptr<Channel>> result;
	for (auto channel : _channels)
		result.push_back(channel->get_shared_pointer(_parent));
	return result;
}

Trigger::Trigger(shared_ptr<Context> context, string name) : 
	UserOwned(sr_trigger_new(name.c_str())),
	_context(context)
{
	for (auto stage = _structure->stages; stage; stage = stage->next)
		_stages.push_back(
			new TriggerStage((struct sr_trigger_stage *) stage->data));
}

Trigger::~Trigger()
{
	for (auto stage: _stages)
		delete stage;

	sr_trigger_free(_structure);
}

string Trigger::name()
{
	return _structure->name;
}

vector<shared_ptr<TriggerStage>> Trigger::stages()
{
	vector<shared_ptr<TriggerStage>> result;
	for (auto stage : _stages)
		result.push_back(stage->get_shared_pointer(this));
	return result;
}

shared_ptr<TriggerStage> Trigger::add_stage()
{
	auto stage = new TriggerStage(sr_trigger_stage_add(_structure));
	_stages.push_back(stage);
	return stage->get_shared_pointer(this);
}

TriggerStage::TriggerStage(struct sr_trigger_stage *structure) : 
	ParentOwned(structure)
{
}

TriggerStage::~TriggerStage()
{
	for (auto match : _matches)
		delete match;
}
	
int TriggerStage::number()
{
	return _structure->stage;
}

vector<shared_ptr<TriggerMatch>> TriggerStage::matches()
{
	vector<shared_ptr<TriggerMatch>> result;
	for (auto match : _matches)
		result.push_back(match->get_shared_pointer(this));
	return result;
}

void TriggerStage::add_match(shared_ptr<Channel> channel,
	const TriggerMatchType *type, float value)
{
	check(sr_trigger_match_add(_structure,
		channel->_structure, type->id(), value));
	_matches.push_back(new TriggerMatch(
		(struct sr_trigger_match *) g_slist_last(
			_structure->matches)->data, channel));
}

void TriggerStage::add_match(shared_ptr<Channel> channel,
	const TriggerMatchType *type)
{
	add_match(channel, type, NAN);
}

TriggerMatch::TriggerMatch(struct sr_trigger_match *structure,
		shared_ptr<Channel> channel) :
	ParentOwned(structure),
	_channel(channel)
{
}

TriggerMatch::~TriggerMatch()
{
}

shared_ptr<Channel> TriggerMatch::channel()
{
	return _channel;
}

const TriggerMatchType *TriggerMatch::type()
{
	return TriggerMatchType::get(_structure->match);
}

float TriggerMatch::value()
{
	return _structure->value;
}

DatafeedCallbackData::DatafeedCallbackData(Session *session,
		DatafeedCallbackFunction callback) :
	_callback(callback),
	_session(session)
{
}

void DatafeedCallbackData::run(const struct sr_dev_inst *sdi,
	const struct sr_datafeed_packet *pkt)
{
	auto device = _session->get_device(sdi);
	auto packet = shared_ptr<Packet>(new Packet(device, pkt), Packet::Deleter());
	_callback(device, packet);
}

SourceCallbackData::SourceCallbackData(shared_ptr<EventSource> source) :
	_source(source)
{
}

bool SourceCallbackData::run(int revents)
{
	return _source->_callback((Glib::IOCondition) revents);
}

shared_ptr<EventSource> EventSource::create(int fd, Glib::IOCondition events,
	int timeout, SourceCallbackFunction callback)
{
	auto result = new EventSource(timeout, callback);
	result->_type = EventSource::SOURCE_FD;
	result->_fd = fd;
	result->_events = events;
	return shared_ptr<EventSource>(result, EventSource::Deleter());
}

shared_ptr<EventSource> EventSource::create(Glib::PollFD pollfd, int timeout,
	SourceCallbackFunction callback)
{
	auto result = new EventSource(timeout, callback);
	result->_type = EventSource::SOURCE_POLLFD;
	result->_pollfd = pollfd;
	return shared_ptr<EventSource>(result, EventSource::Deleter());
}

shared_ptr<EventSource> EventSource::create(Glib::RefPtr<Glib::IOChannel> channel,
	Glib::IOCondition events, int timeout, SourceCallbackFunction callback)
{
	auto result = new EventSource(timeout, callback);
	result->_type = EventSource::SOURCE_IOCHANNEL;
	result->_channel = channel;
	result->_events = events;
	return shared_ptr<EventSource>(result, EventSource::Deleter());
}

EventSource::EventSource(int timeout, SourceCallbackFunction callback) :
	_timeout(timeout),
	_callback(callback)
{
}

EventSource::~EventSource()
{
}

SessionDevice::SessionDevice(struct sr_dev_inst *structure) :
	ParentOwned(structure),
	Device(structure)
{
}

SessionDevice::~SessionDevice()
{
}

shared_ptr<Device> SessionDevice::get_shared_from_this()
{
	return static_pointer_cast<Device>(shared_from_this());
}

Session::Session(shared_ptr<Context> context) :
	UserOwned(_structure),
	_context(context),
	_saving(false)
{
	check(sr_session_new(&_structure));
	_context->_session = this;
}

Session::Session(shared_ptr<Context> context, string filename) :
	UserOwned(_structure),
	_context(context),
	_filename(filename),
	_saving(false)
{
	check(sr_session_load(filename.c_str(), &_structure));
	GSList *dev_list;
	check(sr_session_dev_list(_structure, &dev_list));
	for (GSList *dev = dev_list; dev; dev = dev->next)
	{
		auto sdi = (struct sr_dev_inst *) dev->data;
		_owned_devices[sdi] = new SessionDevice(sdi);
	}
	_context->_session = this;
}

Session::~Session()
{
	check(sr_session_destroy(_structure));

	for (auto callback : _datafeed_callbacks)
		delete callback;

	for (auto entry : _source_callbacks)
		delete entry.second;

	for (auto entry : _owned_devices)
		delete entry.second;
}

shared_ptr<Device> Session::get_device(const struct sr_dev_inst *sdi)
{
	if (_owned_devices.count(sdi))
		return static_pointer_cast<Device>(
			_owned_devices[sdi]->get_shared_pointer(this));
	else if (_other_devices.count(sdi))
		return _other_devices[sdi];
	else
		throw Error(SR_ERR_BUG);
}

void Session::add_device(shared_ptr<Device> device)
{
	check(sr_session_dev_add(_structure, device->_structure));
	_other_devices[device->_structure] = device;
}

vector<shared_ptr<Device>> Session::devices()
{
	GSList *dev_list;
	check(sr_session_dev_list(_structure, &dev_list));
	vector<shared_ptr<Device>> result;
	for (GSList *dev = dev_list; dev; dev = dev->next)
	{
		auto sdi = (struct sr_dev_inst *) dev->data;
		result.push_back(get_device(sdi));
	}
	return result;
}

void Session::remove_devices()
{
	_other_devices.clear();
	check(sr_session_dev_remove_all(_structure));
}

void Session::start()
{
	check(sr_session_start(_structure));
}

void Session::run()
{
	check(sr_session_run(_structure));
}

void Session::stop()
{
	check(sr_session_stop(_structure));
}

void Session::begin_save(string filename)
{
	_saving = true;
	_save_initialized = false;
	_save_filename = filename;
	_save_samplerate = 0;
}

void Session::append(shared_ptr<Packet> packet)
{
	if (!_saving)
		throw Error(SR_ERR);

	switch (packet->_structure->type)
	{
		case SR_DF_META:
		{
			auto meta = (const struct sr_datafeed_meta *)
				packet->_structure->payload;

			for (auto l = meta->config; l; l = l->next)
			{
				auto config = (struct sr_config *) l->data;
				if (config->key == SR_CONF_SAMPLERATE)
					_save_samplerate = g_variant_get_uint64(config->data);
			}

			break;
		}
		case SR_DF_LOGIC:
		{
			if (_save_samplerate == 0)
			{
				GVariant *samplerate;

				check(sr_config_get(sr_dev_inst_driver_get(packet->_device->_structure),
					packet->_device->_structure, NULL, SR_CONF_SAMPLERATE,
					&samplerate));

				_save_samplerate = g_variant_get_uint64(samplerate);

				g_variant_unref(samplerate);
			}

			if (!_save_initialized)
			{
				vector<shared_ptr<Channel>> save_channels;

				for (auto channel : packet->_device->channels())
					if (channel->_structure->enabled &&
							channel->_structure->type == SR_CHANNEL_LOGIC)
						save_channels.push_back(channel);

				auto channels = g_new(char *, save_channels.size());

				int i = 0;
				for (auto channel : save_channels)
						channels[i++] = channel->_structure->name;
				channels[i] = NULL;

				int ret = sr_session_save_init(_structure, _save_filename.c_str(),
						_save_samplerate, channels);

				g_free(channels);

				if (ret != SR_OK)
					throw Error(ret);

				_save_initialized = true;
			}

			auto logic = (const struct sr_datafeed_logic *)
				packet->_structure->payload;

			check(sr_session_append(_structure, _save_filename.c_str(),
				(uint8_t *) logic->data, logic->unitsize,
				logic->length / logic->unitsize));
		}
	}
}

void Session::append(void *data, size_t length, unsigned int unit_size)
{
	check(sr_session_append(_structure, _save_filename.c_str(),
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
	check(sr_session_datafeed_callback_add(_structure,
		datafeed_callback, cb_data));
	_datafeed_callbacks.push_back(cb_data);
}

void Session::remove_datafeed_callbacks(void)
{
	check(sr_session_datafeed_callback_remove_all(_structure));
	for (auto callback : _datafeed_callbacks)
		delete callback;
	_datafeed_callbacks.clear();
}

static int source_callback(int fd, int revents, void *cb_data)
{
	(void) fd;
	auto callback = (SourceCallbackData *) cb_data;
	return callback->run(revents);
}

void Session::add_source(shared_ptr<EventSource> source)
{
	if (_source_callbacks.count(source) == 1)
		throw Error(SR_ERR_ARG);

	auto cb_data = new SourceCallbackData(source);

	switch (source->_type)
	{
		case EventSource::SOURCE_FD:
			check(sr_session_source_add(_structure, source->_fd, source->_events,
				source->_timeout, source_callback, cb_data));
			break;
		case EventSource::SOURCE_POLLFD:
			check(sr_session_source_add_pollfd(_structure,
				source->_pollfd.gobj(), source->_timeout, source_callback,
				cb_data));
			break;
		case EventSource::SOURCE_IOCHANNEL:
			check(sr_session_source_add_channel(_structure,
				source->_channel->gobj(), source->_events, source->_timeout,
				source_callback, cb_data));
			break;
	}

	_source_callbacks[source] = cb_data;
}

void Session::remove_source(shared_ptr<EventSource> source)
{
	if (_source_callbacks.count(source) == 0)
		throw Error(SR_ERR_ARG);

	switch (source->_type)
	{
		case EventSource::SOURCE_FD:
			check(sr_session_source_remove(_structure, source->_fd));
			break;
		case EventSource::SOURCE_POLLFD:
			check(sr_session_source_remove_pollfd(_structure,
				source->_pollfd.gobj()));
			break;
		case EventSource::SOURCE_IOCHANNEL:
			check(sr_session_source_remove_channel(_structure,
				source->_channel->gobj()));
			break;
	}

	delete _source_callbacks[source];

	_source_callbacks.erase(source);
}

shared_ptr<Trigger> Session::trigger()
{
	return _trigger;
}

void Session::set_trigger(shared_ptr<Trigger> trigger)
{
	check(sr_session_trigger_set(_structure, trigger->_structure));
	_trigger = trigger;
}

string Session::filename()
{
	return _filename;
}

Packet::Packet(shared_ptr<Device> device,
	const struct sr_datafeed_packet *structure) :
	UserOwned(structure),
	_device(device)
{
	switch (structure->type)
	{
		case SR_DF_HEADER:
			_payload = new Header(
				static_cast<const struct sr_datafeed_header *>(
					structure->payload));
			break;
		case SR_DF_META:
			_payload = new Meta(
				static_cast<const struct sr_datafeed_meta *>(
					structure->payload));
			break;
		case SR_DF_LOGIC:
			_payload = new Logic(
				static_cast<const struct sr_datafeed_logic *>(
					structure->payload));
			break;
		case SR_DF_ANALOG:
			_payload = new Analog(
				static_cast<const struct sr_datafeed_analog *>(
					structure->payload));
			break;
		default:
			_payload = nullptr;
			break;
	}
}

Packet::~Packet()
{
	if (_payload)
		delete _payload;
}

const PacketType *Packet::type()
{
	return PacketType::get(_structure->type);
}

shared_ptr<PacketPayload> Packet::payload()
{
	if (_payload)
		return _payload->get_shared_pointer(this);
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

shared_ptr<PacketPayload> Header::get_shared_pointer(Packet *_parent)
{
	return static_pointer_cast<PacketPayload>(
		ParentOwned::get_shared_pointer(_parent));
}

int Header::feed_version()
{
	return _structure->feed_version;
}

Glib::TimeVal Header::start_time()
{
	return Glib::TimeVal(
		_structure->starttime.tv_sec,
		_structure->starttime.tv_usec);
}

Meta::Meta(const struct sr_datafeed_meta *structure) :
	ParentOwned(structure),
	PacketPayload()
{
}

Meta::~Meta()
{
}

shared_ptr<PacketPayload> Meta::get_shared_pointer(Packet *_parent)
{
	return static_pointer_cast<PacketPayload>(
		ParentOwned::get_shared_pointer(_parent));
}

map<const ConfigKey *, Glib::VariantBase> Meta::config()
{
	map<const ConfigKey *, Glib::VariantBase> result;
	for (auto l = _structure->config; l; l = l->next)
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

shared_ptr<PacketPayload> Logic::get_shared_pointer(Packet *_parent)
{
	return static_pointer_cast<PacketPayload>(
		ParentOwned::get_shared_pointer(_parent));
}

void *Logic::data_pointer()
{
	return _structure->data;
}

size_t Logic::data_length()
{
	return _structure->length;
}

unsigned int Logic::unit_size()
{
	return _structure->unitsize;
}

Analog::Analog(const struct sr_datafeed_analog *structure) :
	ParentOwned(structure),
	PacketPayload()
{
}

Analog::~Analog()
{
}

shared_ptr<PacketPayload> Analog::get_shared_pointer(Packet *_parent)
{
	return static_pointer_cast<PacketPayload>(
		ParentOwned::get_shared_pointer(_parent));
}

float *Analog::data_pointer()
{
	return _structure->data;
}

unsigned int Analog::num_samples()
{
	return _structure->num_samples;
}

vector<shared_ptr<Channel>> Analog::channels()
{
	vector<shared_ptr<Channel>> result;
	for (auto l = _structure->channels; l; l = l->next)
		result.push_back(_parent->_device->get_channel(
			(struct sr_channel *)l->data));
	return result;
}

const Quantity *Analog::mq()
{
	return Quantity::get(_structure->mq);
}

const Unit *Analog::unit()
{
	return Unit::get(_structure->unit);
}

vector<const QuantityFlag *> Analog::mq_flags()
{
	return QuantityFlag::flags_from_mask(_structure->mqflags);
}

InputFormat::InputFormat(const struct sr_input_module *structure) :
	ParentOwned(structure)
{
}

InputFormat::~InputFormat()
{
}

string InputFormat::name()
{
	return valid_string(sr_input_id_get(_structure));
}

string InputFormat::description()
{
	return valid_string(sr_input_description_get(_structure));
}

map<string, shared_ptr<Option>> InputFormat::options()
{
	const struct sr_option **options = sr_input_options_get(_structure);
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
	auto input = sr_input_new(_structure, map_to_hash_variant(options));
	if (!input)
		throw Error(SR_ERR_ARG);
	return shared_ptr<Input>(
		new Input(_parent->shared_from_this(), input), Input::Deleter());
}

Input::Input(shared_ptr<Context> context, const struct sr_input *structure) :
	UserOwned(structure),
	_context(context),
	_device(nullptr)
{
}

shared_ptr<InputDevice> Input::device()
{
	if (!_device)
	{
		auto sdi = sr_input_dev_inst_get(_structure);
		if (!sdi)
			throw Error(SR_ERR_NA);
		_device = new InputDevice(shared_from_this(), sdi);
	}

	return _device->get_shared_pointer(shared_from_this());
}

void Input::send(string data)
{
	auto gstr = g_string_new(data.c_str());
	auto ret = sr_input_send(_structure, gstr);
	g_string_free(gstr, false);
	check(ret);
}

void Input::end()
{
	check(sr_input_end(_structure));
}

Input::~Input()
{
	if (_device)
		delete _device;
	sr_input_free(_structure);
}

InputDevice::InputDevice(shared_ptr<Input> input,
		struct sr_dev_inst *structure) :
	ParentOwned(structure),
	Device(structure),
	_input(input)
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
	_structure_array(structure_array)
{
}

Option::~Option()
{
}

string Option::id()
{
	return valid_string(_structure->id);
}

string Option::name()
{
	return valid_string(_structure->name);
}

string Option::description()
{
	return valid_string(_structure->desc);
}

Glib::VariantBase Option::default_value()
{
	return Glib::VariantBase(_structure->def, true);
}

vector<Glib::VariantBase> Option::values()
{
	vector<Glib::VariantBase> result;
	for (auto l = _structure->values; l; l = l->next)
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

string OutputFormat::name()
{
	return valid_string(sr_output_id_get(_structure));
}

string OutputFormat::description()
{
	return valid_string(sr_output_description_get(_structure));
}

map<string, shared_ptr<Option>> OutputFormat::options()
{
	const struct sr_option **options = sr_output_options_get(_structure);
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
	UserOwned(sr_output_new(format->_structure,
		map_to_hash_variant(options), device->_structure)),
	_format(format),
	_device(device),
	_options(options)
{
}

Output::~Output()
{
	check(sr_output_free(_structure));
}

string Output::receive(shared_ptr<Packet> packet)
{
	GString *out;
	check(sr_output_send(_structure, packet->_structure, &out));
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
