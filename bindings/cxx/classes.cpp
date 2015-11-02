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

/* Needed for isascii(), as used in the GNU libstdc++ headers */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include <config.h>
#include <libsigrokcxx/libsigrokcxx.hpp>

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
static inline const char *valid_string(const char *input)
{
	return (input) ? input : "";
}

/** Helper function to convert between map<string, VariantBase> and GHashTable */
static GHashTable *map_to_hash_variant(const map<string, Glib::VariantBase> &input)
{
	auto *const output = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
			reinterpret_cast<GDestroyNotify>(&g_variant_unref));
	for (const auto &entry : input)
		g_hash_table_insert(output,
			g_strdup(entry.first.c_str()),
			entry.second.gobj_copy());
	return output;
}

Error::Error(int result) : result(result)
{
}

const char *Error::what() const noexcept
{
	return sr_strerror(result);
}

Error::~Error() noexcept
{
}

ResourceReader::~ResourceReader()
{
}

SR_PRIV int ResourceReader::open_callback(struct sr_resource *res,
		const char *name, void *cb_data) noexcept
{
	try {
		auto *const reader = static_cast<ResourceReader*>(cb_data);
		reader->open(res, name);
	} catch (const Error &err) {
		return err.result;
	} catch (...) {
		return SR_ERR;
	}
	return SR_OK;
}

SR_PRIV int ResourceReader::close_callback(struct sr_resource *res,
		void *cb_data) noexcept
{
	try {
		auto *const reader = static_cast<ResourceReader*>(cb_data);
		reader->close(res);
	} catch (const Error &err) {
		return err.result;
	} catch (...) {
		return SR_ERR;
	}
	return SR_OK;
}

SR_PRIV ssize_t ResourceReader::read_callback(const struct sr_resource *res,
		void *buf, size_t count, void *cb_data) noexcept
{
	try {
		auto *const reader = static_cast<ResourceReader*>(cb_data);
		return reader->read(res, buf, count);
	} catch (const Error &err) {
		return err.result;
	} catch (...) {
		return SR_ERR;
	}
}

shared_ptr<Context> Context::create()
{
	return shared_ptr<Context>{new Context{}, default_delete<Context>{}};
}

Context::Context() :
	_structure(nullptr),
	_session(nullptr)
{
	check(sr_init(&_structure));

	if (struct sr_dev_driver **driver_list = sr_driver_list(_structure))
		for (int i = 0; driver_list[i]; i++) {
			unique_ptr<Driver> driver {new Driver{driver_list[i]}};
			_drivers.emplace(driver->name(), move(driver));
		}

	if (const struct sr_input_module **input_list = sr_input_list())
		for (int i = 0; input_list[i]; i++) {
			unique_ptr<InputFormat> input {new InputFormat{input_list[i]}};
			_input_formats.emplace(input->name(), move(input));
		}

	if (const struct sr_output_module **output_list = sr_output_list())
		for (int i = 0; output_list[i]; i++) {
			unique_ptr<OutputFormat> output {new OutputFormat{output_list[i]}};
			_output_formats.emplace(output->name(), move(output));
		}
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
	for (const auto &entry: _drivers)
	{
		const auto &name = entry.first;
		const auto &driver = entry.second;
		result.emplace(name, driver->share_owned_by(shared_from_this()));
	}
	return result;
}

map<string, shared_ptr<InputFormat>> Context::input_formats()
{
	map<string, shared_ptr<InputFormat>> result;
	for (const auto &entry: _input_formats)
	{
		const auto &name = entry.first;
		const auto &input_format = entry.second;
		result.emplace(name, input_format->share_owned_by(shared_from_this()));
	}
	return result;
}

map<string, shared_ptr<OutputFormat>> Context::output_formats()
{
	map<string, shared_ptr<OutputFormat>> result;
	for (const auto &entry: _output_formats)
	{
		const auto &name = entry.first;
		const auto &output_format = entry.second;
		result.emplace(name, output_format->share_owned_by(shared_from_this()));
	}
	return result;
}

Context::~Context()
{
	check(sr_exit(_structure));
}

const LogLevel *Context::log_level() const
{
	return LogLevel::get(sr_log_loglevel_get());
}

void Context::set_log_level(const LogLevel *level)
{
	check(sr_log_loglevel_set(level->id()));
}

static int call_log_callback(void *cb_data, int loglevel,
		const char *format, va_list args) noexcept
{
	const unique_ptr<char, decltype(&g_free)>
		message {g_strdup_vprintf(format, args), &g_free};

	auto *const callback = static_cast<LogCallbackFunction *>(cb_data);

	try
	{
		(*callback)(LogLevel::get(loglevel), message.get());
	}
	catch (Error e)
	{
		return e.result;
	}

	return SR_OK;
}

void Context::set_log_callback(LogCallbackFunction callback)
{
	_log_callback = move(callback);
	check(sr_log_callback_set(call_log_callback, &_log_callback));
}

void Context::set_log_callback_default()
{
	check(sr_log_callback_set_default());
	_log_callback = nullptr;
}

void Context::set_resource_reader(ResourceReader *reader)
{
	if (reader) {
		check(sr_resource_set_hooks(_structure,
				&ResourceReader::open_callback,
				&ResourceReader::close_callback,
				&ResourceReader::read_callback, reader));
	} else {
		check(sr_resource_set_hooks(_structure,
				nullptr, nullptr, nullptr, nullptr));
	}
}

shared_ptr<Session> Context::create_session()
{
	return shared_ptr<Session>{new Session{shared_from_this()},
		default_delete<Session>{}};
}

shared_ptr<UserDevice> Context::create_user_device(
		string vendor, string model, string version)
{
	return shared_ptr<UserDevice>{
		new UserDevice{move(vendor), move(model), move(version)},
		default_delete<UserDevice>{}};
}

shared_ptr<Packet> Context::create_header_packet(Glib::TimeVal start_time)
{
	auto header = g_new(struct sr_datafeed_header, 1);
	header->feed_version = 1;
	header->starttime.tv_sec = start_time.tv_sec;
	header->starttime.tv_usec = start_time.tv_usec;
	auto packet = g_new(struct sr_datafeed_packet, 1);
	packet->type = SR_DF_HEADER;
	packet->payload = header;
	return shared_ptr<Packet>{new Packet{nullptr, packet},
		default_delete<Packet>{}};
}

shared_ptr<Packet> Context::create_meta_packet(
	map<const ConfigKey *, Glib::VariantBase> config)
{
	auto meta = g_new0(struct sr_datafeed_meta, 1);
	for (const auto &input : config)
	{
		const auto &key = input.first;
		const auto &value = input.second;
		auto *const output = g_new(struct sr_config, 1);
		output->key = key->id();
		output->data = value.gobj_copy();
		meta->config = g_slist_append(meta->config, output);
	}
	auto packet = g_new(struct sr_datafeed_packet, 1);
	packet->type = SR_DF_META;
	packet->payload = meta;
	return shared_ptr<Packet>{new Packet{nullptr, packet},
		default_delete<Packet>{}};
}

shared_ptr<Packet> Context::create_logic_packet(
	void *data_pointer, size_t data_length, unsigned int unit_size)
{
	auto logic = g_new(struct sr_datafeed_logic, 1);
	logic->length = data_length;
	logic->unitsize = unit_size;
	logic->data = data_pointer;
	auto packet = g_new(struct sr_datafeed_packet, 1);
	packet->type = SR_DF_LOGIC;
	packet->payload = logic;
	return shared_ptr<Packet>{new Packet{nullptr, packet}, default_delete<Packet>{}};
}

shared_ptr<Packet> Context::create_analog_packet(
	vector<shared_ptr<Channel> > channels,
	float *data_pointer, unsigned int num_samples, const Quantity *mq,
	const Unit *unit, vector<const QuantityFlag *> mqflags)
{
	auto analog = g_new0(struct sr_datafeed_analog, 1);
	auto meaning = g_new0(struct sr_analog_meaning, 1);

	analog->meaning = meaning;

	for (const auto &channel : channels)
		meaning->channels = g_slist_append(meaning->channels, channel->_structure);
	analog->num_samples = num_samples;
	meaning->mq = static_cast<sr_mq>(mq->id());
	meaning->unit = static_cast<sr_unit>(unit->id());
	meaning->mqflags = static_cast<sr_mqflag>(QuantityFlag::mask_from_flags(move(mqflags)));
	analog->data = data_pointer;
	auto packet = g_new(struct sr_datafeed_packet, 1);
	packet->type = SR_DF_ANALOG;
	packet->payload = analog;
	return shared_ptr<Packet>{new Packet{nullptr, packet}, default_delete<Packet>{}};
}

shared_ptr<Session> Context::load_session(string filename)
{
	return shared_ptr<Session>{
		new Session{shared_from_this(), move(filename)},
		default_delete<Session>{}};
}

shared_ptr<Trigger> Context::create_trigger(string name)
{
	return shared_ptr<Trigger>{
		new Trigger{shared_from_this(), move(name)},
		default_delete<Trigger>{}};
}

shared_ptr<Input> Context::open_file(string filename)
{
	const struct sr_input *input;

	check(sr_input_scan_file(filename.c_str(), &input));
	return shared_ptr<Input>{
		new Input{shared_from_this(), input},
		default_delete<Input>{}};
}

shared_ptr<Input> Context::open_stream(string header)
{
	const struct sr_input *input;

	auto gstr = g_string_new(header.c_str());
	auto ret = sr_input_scan_buffer(gstr, &input);
	g_string_free(gstr, true);
	check(ret);
	return shared_ptr<Input>{
		new Input{shared_from_this(), input},
		default_delete<Input>{}};
}

map<string, string> Context::serials(shared_ptr<Driver> driver) const
{
	GSList *serial_list = sr_serial_list(driver ? driver->_structure : nullptr);
	map<string, string> serials;

	for (GSList *serial = serial_list; serial; serial = serial->next) {
		auto *const port = static_cast<sr_serial_port *>(serial->data);
		serials[string(port->name)] = string(port->description);
	}

	g_slist_free_full(serial_list,
		reinterpret_cast<GDestroyNotify>(&sr_serial_free));
	return serials;
}

Driver::Driver(struct sr_dev_driver *structure) :
	Configurable(structure, nullptr, nullptr),
	_structure(structure),
	_initialized(false)
{
}

Driver::~Driver()
{
}

string Driver::name() const
{
	return valid_string(_structure->name);
}

string Driver::long_name() const
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
	GSList *option_list = nullptr;
	for (const auto &entry : options)
	{
		const auto &key = entry.first;
		const auto &value = entry.second;
		auto *const config = g_new(struct sr_config, 1);
		config->key = key->id();
		config->data = const_cast<GVariant*>(value.gobj());
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
		auto *const sdi = static_cast<struct sr_dev_inst *>(device->data);
		shared_ptr<HardwareDevice> hwdev {
			new HardwareDevice{shared_from_this(), sdi},
			default_delete<HardwareDevice>{}};
		result.push_back(move(hwdev));
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

Glib::VariantBase Configurable::config_get(const ConfigKey *key) const
{
	GVariant *data;
	check(sr_config_get(
		config_driver, config_sdi, config_channel_group,
		key->id(), &data));
	return Glib::VariantBase(data);
}

void Configurable::config_set(const ConfigKey *key, const Glib::VariantBase &value)
{
	check(sr_config_set(
		config_sdi, config_channel_group,
		key->id(), const_cast<GVariant*>(value.gobj())));
}

Glib::VariantContainerBase Configurable::config_list(const ConfigKey *key) const
{
	GVariant *data;
	check(sr_config_list(
		config_driver, config_sdi, config_channel_group,
		key->id(), &data));
	return Glib::VariantContainerBase(data);
}

map<const ConfigKey *, set<const Capability *>> Configurable::config_keys(const ConfigKey *key)
{
	GVariant *gvar_opts;
	gsize num_opts;
	const uint32_t *opts;
	map<const ConfigKey *, set<const Capability *>> result;

	check(sr_config_list(
		config_driver, config_sdi, config_channel_group,
		key->id(), &gvar_opts));

	opts = static_cast<const uint32_t *>(g_variant_get_fixed_array(
		gvar_opts, &num_opts, sizeof(uint32_t)));

	for (gsize i = 0; i < num_opts; i++)
	{
		auto key = ConfigKey::get(opts[i] & SR_CONF_MASK);
		set<const Capability *> capabilities;
		if (opts[i] & SR_CONF_GET)
			capabilities.insert(Capability::GET);
		if (opts[i] & SR_CONF_SET)
			capabilities.insert(Capability::SET);
		if (opts[i] & SR_CONF_LIST)
			capabilities.insert(Capability::LIST);
		result[key] = capabilities;
	}

	g_variant_unref(gvar_opts);

	return result;
}

bool Configurable::config_check(const ConfigKey *key,
	const ConfigKey *index_key) const
{
	GVariant *gvar_opts;
	gsize num_opts;
	const uint32_t *opts;

	if (sr_config_list(config_driver, config_sdi, config_channel_group,
			index_key->id(), &gvar_opts) != SR_OK)
		return false;

	opts = static_cast<const uint32_t *>(g_variant_get_fixed_array(
		gvar_opts, &num_opts, sizeof(uint32_t)));

	for (gsize i = 0; i < num_opts; i++)
	{
		if ((opts[i] & SR_CONF_MASK) == unsigned(key->id()))
		{
			g_variant_unref(gvar_opts);
			return true;
		}
	}

	g_variant_unref(gvar_opts);

	return false;
}

Device::Device(struct sr_dev_inst *structure) :
	Configurable(sr_dev_inst_driver_get(structure), structure, nullptr),
	_structure(structure)
{
	for (GSList *entry = sr_dev_inst_channels_get(structure); entry; entry = entry->next)
	{
		auto *const ch = static_cast<struct sr_channel *>(entry->data);
		unique_ptr<Channel> channel {new Channel{ch}};
		_channels.emplace(ch, move(channel));
	}

	for (GSList *entry = sr_dev_inst_channel_groups_get(structure); entry; entry = entry->next)
	{
		auto *const cg = static_cast<struct sr_channel_group *>(entry->data);
		unique_ptr<ChannelGroup> group {new ChannelGroup{this, cg}};
		_channel_groups.emplace(group->name(), move(group));
	}
}

Device::~Device()
{}

string Device::vendor() const
{
	return valid_string(sr_dev_inst_vendor_get(_structure));
}

string Device::model() const
{
	return valid_string(sr_dev_inst_model_get(_structure));
}

string Device::version() const
{
	return valid_string(sr_dev_inst_version_get(_structure));
}

string Device::serial_number() const
{
	return valid_string(sr_dev_inst_sernum_get(_structure));
}

string Device::connection_id() const
{
	return valid_string(sr_dev_inst_connid_get(_structure));
}

vector<shared_ptr<Channel>> Device::channels()
{
	vector<shared_ptr<Channel>> result;
	for (auto channel = sr_dev_inst_channels_get(_structure); channel; channel = channel->next) {
		auto *const ch = static_cast<struct sr_channel *>(channel->data);
		result.push_back(_channels[ch]->share_owned_by(get_shared_from_this()));
	}
	return result;
}

shared_ptr<Channel> Device::get_channel(struct sr_channel *ptr)
{
	return _channels[ptr]->share_owned_by(get_shared_from_this());
}

map<string, shared_ptr<ChannelGroup>>
Device::channel_groups()
{
	map<string, shared_ptr<ChannelGroup>> result;
	for (const auto &entry: _channel_groups)
	{
		const auto &name = entry.first;
		const auto &channel_group = entry.second;
		result.emplace(name, channel_group->share_owned_by(get_shared_from_this()));
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
	Device(structure),
	_driver(move(driver))
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
	Device(sr_dev_inst_user_new(
		vendor.c_str(), model.c_str(), version.c_str()))
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
	GSList *const last = g_slist_last(sr_dev_inst_channels_get(Device::_structure));
	auto *const ch = static_cast<struct sr_channel *>(last->data);
	unique_ptr<Channel> channel {new Channel{ch}};
	_channels.emplace(ch, move(channel));
	return get_channel(ch);
}

Channel::Channel(struct sr_channel *structure) :
	_structure(structure),
	_type(ChannelType::get(_structure->type))
{
}

Channel::~Channel()
{
}

string Channel::name() const
{
	return valid_string(_structure->name);
}

void Channel::set_name(string name)
{
	check(sr_dev_channel_name_set(_structure, name.c_str()));
}

const ChannelType *Channel::type() const
{
	return ChannelType::get(_structure->type);
}

bool Channel::enabled() const
{
	return _structure->enabled;
}

void Channel::set_enabled(bool value)
{
	check(sr_dev_channel_enable(_structure, value));
}

unsigned int Channel::index() const
{
	return _structure->index;
}

ChannelGroup::ChannelGroup(const Device *device,
		struct sr_channel_group *structure) :
	Configurable(sr_dev_inst_driver_get(device->_structure), device->_structure, structure)
{
	for (GSList *entry = config_channel_group->channels; entry; entry = entry->next) {
		auto *const ch = static_cast<struct sr_channel *>(entry->data);
		/* Note: This relies on Device::_channels to keep the Channel
		 * objects around over the lifetime of the ChannelGroup. */
		_channels.push_back(device->_channels.find(ch)->second.get());
	}
}

ChannelGroup::~ChannelGroup()
{
}

string ChannelGroup::name() const
{
	return valid_string(config_channel_group->name);
}

vector<shared_ptr<Channel>> ChannelGroup::channels()
{
	vector<shared_ptr<Channel>> result;
	for (const auto &channel : _channels)
		result.push_back(channel->share_owned_by(_parent));
	return result;
}

Trigger::Trigger(shared_ptr<Context> context, string name) : 
	_structure(sr_trigger_new(name.c_str())),
	_context(move(context))
{
	for (auto *stage = _structure->stages; stage; stage = stage->next) {
		unique_ptr<TriggerStage> ts {new TriggerStage{
				static_cast<struct sr_trigger_stage *>(stage->data)}};
		_stages.push_back(move(ts));
	}
}

Trigger::~Trigger()
{
	sr_trigger_free(_structure);
}

string Trigger::name() const
{
	return _structure->name;
}

vector<shared_ptr<TriggerStage>> Trigger::stages()
{
	vector<shared_ptr<TriggerStage>> result;
	for (const auto &stage : _stages)
		result.push_back(stage->share_owned_by(shared_from_this()));
	return result;
}

shared_ptr<TriggerStage> Trigger::add_stage()
{
	unique_ptr<TriggerStage> stage {new TriggerStage{sr_trigger_stage_add(_structure)}};
	_stages.push_back(move(stage));
	return _stages.back()->share_owned_by(shared_from_this());
}

TriggerStage::TriggerStage(struct sr_trigger_stage *structure) :
	_structure(structure)
{
}

TriggerStage::~TriggerStage()
{
}
	
int TriggerStage::number() const
{
	return _structure->stage;
}

vector<shared_ptr<TriggerMatch>> TriggerStage::matches()
{
	vector<shared_ptr<TriggerMatch>> result;
	for (const auto &match : _matches)
		result.push_back(match->share_owned_by(shared_from_this()));
	return result;
}

void TriggerStage::add_match(shared_ptr<Channel> channel,
	const TriggerMatchType *type, float value)
{
	check(sr_trigger_match_add(_structure,
		channel->_structure, type->id(), value));
	GSList *const last = g_slist_last(_structure->matches);
	unique_ptr<TriggerMatch> match {new TriggerMatch{
			static_cast<struct sr_trigger_match *>(last->data),
			move(channel)}};
	_matches.push_back(move(match));
}

void TriggerStage::add_match(shared_ptr<Channel> channel,
	const TriggerMatchType *type)
{
	add_match(move(channel), type, NAN);
}

TriggerMatch::TriggerMatch(struct sr_trigger_match *structure,
		shared_ptr<Channel> channel) :
	_structure(structure),
	_channel(move(channel))
{
}

TriggerMatch::~TriggerMatch()
{
}

shared_ptr<Channel> TriggerMatch::channel()
{
	return _channel;
}

const TriggerMatchType *TriggerMatch::type() const
{
	return TriggerMatchType::get(_structure->match);
}

float TriggerMatch::value() const
{
	return _structure->value;
}

DatafeedCallbackData::DatafeedCallbackData(Session *session,
		DatafeedCallbackFunction callback) :
	_callback(move(callback)),
	_session(session)
{
}

void DatafeedCallbackData::run(const struct sr_dev_inst *sdi,
	const struct sr_datafeed_packet *pkt)
{
	auto device = _session->get_device(sdi);
	shared_ptr<Packet> packet {new Packet{device, pkt}, default_delete<Packet>{}};
	_callback(move(device), move(packet));
}

SessionDevice::SessionDevice(struct sr_dev_inst *structure) :
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
	_structure(nullptr),
	_context(move(context))
{
	check(sr_session_new(_context->_structure, &_structure));
	_context->_session = this;
}

Session::Session(shared_ptr<Context> context, string filename) :
	_structure(nullptr),
	_context(move(context)),
	_filename(move(filename))
{
	check(sr_session_load(_context->_structure, _filename.c_str(), &_structure));
	GSList *dev_list;
	check(sr_session_dev_list(_structure, &dev_list));
	for (GSList *dev = dev_list; dev; dev = dev->next) {
		auto *const sdi = static_cast<struct sr_dev_inst *>(dev->data);
		unique_ptr<SessionDevice> device {new SessionDevice{sdi}};
		_owned_devices.emplace(sdi, move(device));
	}
	_context->_session = this;
}

Session::~Session()
{
	check(sr_session_destroy(_structure));
}

shared_ptr<Device> Session::get_device(const struct sr_dev_inst *sdi)
{
	if (_owned_devices.count(sdi))
		return static_pointer_cast<Device>(
			_owned_devices[sdi]->share_owned_by(shared_from_this()));
	else if (_other_devices.count(sdi))
		return _other_devices[sdi];
	else
		throw Error(SR_ERR_BUG);
}

void Session::add_device(shared_ptr<Device> device)
{
	const auto dev_struct = device->_structure;
	check(sr_session_dev_add(_structure, dev_struct));
	_other_devices[dev_struct] = move(device);
}

vector<shared_ptr<Device>> Session::devices()
{
	GSList *dev_list;
	check(sr_session_dev_list(_structure, &dev_list));
	vector<shared_ptr<Device>> result;
	for (GSList *dev = dev_list; dev; dev = dev->next) {
		auto *const sdi = static_cast<struct sr_dev_inst *>(dev->data);
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

bool Session::is_running() const
{
	const int ret = sr_session_is_running(_structure);
	if (ret < 0)
		throw Error{ret};
	return (ret != 0);
}

static void session_stopped_callback(void *data) noexcept
{
	auto *const callback = static_cast<SessionStoppedCallback*>(data);
	(*callback)();
}

void Session::set_stopped_callback(SessionStoppedCallback callback)
{
	_stopped_callback = move(callback);
	if (_stopped_callback)
		check(sr_session_stopped_callback_set(_structure,
				&session_stopped_callback, &_stopped_callback));
	else
		check(sr_session_stopped_callback_set(_structure,
				nullptr, nullptr));
}

static void datafeed_callback(const struct sr_dev_inst *sdi,
	const struct sr_datafeed_packet *pkt, void *cb_data) noexcept
{
	auto callback = static_cast<DatafeedCallbackData *>(cb_data);
	callback->run(sdi, pkt);
}

void Session::add_datafeed_callback(DatafeedCallbackFunction callback)
{
	unique_ptr<DatafeedCallbackData> cb_data
		{new DatafeedCallbackData{this, move(callback)}};
	check(sr_session_datafeed_callback_add(_structure,
			&datafeed_callback, cb_data.get()));
	_datafeed_callbacks.push_back(move(cb_data));
}

void Session::remove_datafeed_callbacks()
{
	check(sr_session_datafeed_callback_remove_all(_structure));
	_datafeed_callbacks.clear();
}

shared_ptr<Trigger> Session::trigger()
{
	return _trigger;
}

void Session::set_trigger(shared_ptr<Trigger> trigger)
{
	if (!trigger)
		// Set NULL trigger, i.e. remove any trigger from the session.
		check(sr_session_trigger_set(_structure, nullptr));
	else
		check(sr_session_trigger_set(_structure, trigger->_structure));
	_trigger = move(trigger);
}

string Session::filename() const
{
	return _filename;
}

shared_ptr<Context> Session::context()
{
	return _context;
}

Packet::Packet(shared_ptr<Device> device,
	const struct sr_datafeed_packet *structure) :
	_structure(structure),
	_device(move(device))
{
	switch (structure->type)
	{
		case SR_DF_HEADER:
			_payload.reset(new Header{
				static_cast<const struct sr_datafeed_header *>(
					structure->payload)});
			break;
		case SR_DF_META:
			_payload.reset(new Meta{
				static_cast<const struct sr_datafeed_meta *>(
					structure->payload)});
			break;
		case SR_DF_LOGIC:
			_payload.reset(new Logic{
				static_cast<const struct sr_datafeed_logic *>(
					structure->payload)});
			break;
		case SR_DF_ANALOG:
			_payload.reset(new Analog{
				static_cast<const struct sr_datafeed_analog *>(
					structure->payload)});
			break;
	}
}

Packet::~Packet()
{
}

const PacketType *Packet::type() const
{
	return PacketType::get(_structure->type);
}

shared_ptr<PacketPayload> Packet::payload()
{
	if (_payload)
		return _payload->share_owned_by(shared_from_this());
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
	PacketPayload(),
	_structure(structure)
{
}

Header::~Header()
{
}

shared_ptr<PacketPayload> Header::share_owned_by(shared_ptr<Packet> _parent)
{
	return static_pointer_cast<PacketPayload>(
		ParentOwned::share_owned_by(_parent));
}

int Header::feed_version() const
{
	return _structure->feed_version;
}

Glib::TimeVal Header::start_time() const
{
	return Glib::TimeVal(
		_structure->starttime.tv_sec,
		_structure->starttime.tv_usec);
}

Meta::Meta(const struct sr_datafeed_meta *structure) :
	PacketPayload(),
	_structure(structure)
{
}

Meta::~Meta()
{
}

shared_ptr<PacketPayload> Meta::share_owned_by(shared_ptr<Packet> _parent)
{
	return static_pointer_cast<PacketPayload>(
		ParentOwned::share_owned_by(_parent));
}

map<const ConfigKey *, Glib::VariantBase> Meta::config() const
{
	map<const ConfigKey *, Glib::VariantBase> result;
	for (auto l = _structure->config; l; l = l->next) {
		auto *const config = static_cast<struct sr_config *>(l->data);
		result[ConfigKey::get(config->key)] = Glib::VariantBase(config->data, true);
	}
	return result;
}

Logic::Logic(const struct sr_datafeed_logic *structure) :
	PacketPayload(),
	_structure(structure)
{
}

Logic::~Logic()
{
}

shared_ptr<PacketPayload> Logic::share_owned_by(shared_ptr<Packet> _parent)
{
	return static_pointer_cast<PacketPayload>(
		ParentOwned::share_owned_by(_parent));
}

void *Logic::data_pointer()
{
	return _structure->data;
}

size_t Logic::data_length() const
{
	return _structure->length;
}

unsigned int Logic::unit_size() const
{
	return _structure->unitsize;
}

Analog::Analog(const struct sr_datafeed_analog *structure) :
	PacketPayload(),
	_structure(structure)
{
}

Analog::~Analog()
{
}

shared_ptr<PacketPayload> Analog::share_owned_by(shared_ptr<Packet> _parent)
{
	return static_pointer_cast<PacketPayload>(
		ParentOwned::share_owned_by(_parent));
}

void *Analog::data_pointer()
{
	return _structure->data;
}

unsigned int Analog::num_samples() const
{
	return _structure->num_samples;
}

vector<shared_ptr<Channel>> Analog::channels()
{
	vector<shared_ptr<Channel>> result;
	for (auto l = _structure->meaning->channels; l; l = l->next) {
		auto *const ch = static_cast<struct sr_channel *>(l->data);
		result.push_back(_parent->_device->get_channel(ch));
	}
	return result;
}

const Quantity *Analog::mq() const
{
	return Quantity::get(_structure->meaning->mq);
}

const Unit *Analog::unit() const
{
	return Unit::get(_structure->meaning->unit);
}

vector<const QuantityFlag *> Analog::mq_flags() const
{
	return QuantityFlag::flags_from_mask(_structure->meaning->mqflags);
}

InputFormat::InputFormat(const struct sr_input_module *structure) :
	_structure(structure)
{
}

InputFormat::~InputFormat()
{
}

string InputFormat::name() const
{
	return valid_string(sr_input_id_get(_structure));
}

string InputFormat::description() const
{
	return valid_string(sr_input_description_get(_structure));
}

vector<string> InputFormat::extensions() const
{
	vector<string> exts;
	for (const char *const *e = sr_input_extensions_get(_structure);
		e && *e; e++)
		exts.push_back(*e);
	return exts;
}

map<string, shared_ptr<Option>> InputFormat::options()
{
	map<string, shared_ptr<Option>> result;

	if (const struct sr_option **options = sr_input_options_get(_structure))
	{
		shared_ptr<const struct sr_option *> option_array
			{options, &sr_input_options_free};
		for (int i = 0; options[i]; i++) {
			shared_ptr<Option> opt {
				new Option{options[i], option_array},
				default_delete<Option>{}};
			result.emplace(opt->id(), move(opt));
		}
	}
	return result;
}

shared_ptr<Input> InputFormat::create_input(
	map<string, Glib::VariantBase> options)
{
	auto input = sr_input_new(_structure, map_to_hash_variant(options));
	if (!input)
		throw Error(SR_ERR_ARG);
	return shared_ptr<Input>{new Input{_parent, input}, default_delete<Input>{}};
}

Input::Input(shared_ptr<Context> context, const struct sr_input *structure) :
	_structure(structure),
	_context(move(context))
{
}

shared_ptr<InputDevice> Input::device()
{
	if (!_device)
	{
		auto sdi = sr_input_dev_inst_get(_structure);
		if (!sdi)
			throw Error(SR_ERR_NA);
		_device.reset(new InputDevice{shared_from_this(), sdi});
	}

	return _device->share_owned_by(shared_from_this());
}

void Input::send(void *data, size_t length)
{
	auto gstr = g_string_new_len(static_cast<char *>(data), length);
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
	sr_input_free(_structure);
}

InputDevice::InputDevice(shared_ptr<Input> input,
		struct sr_dev_inst *structure) :
	Device(structure),
	_input(move(input))
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
	_structure(structure),
	_structure_array(move(structure_array))
{
}

Option::~Option()
{
}

string Option::id() const
{
	return valid_string(_structure->id);
}

string Option::name() const
{
	return valid_string(_structure->name);
}

string Option::description() const
{
	return valid_string(_structure->desc);
}

Glib::VariantBase Option::default_value() const
{
	return Glib::VariantBase(_structure->def, true);
}

vector<Glib::VariantBase> Option::values() const
{
	vector<Glib::VariantBase> result;
	for (auto l = _structure->values; l; l = l->next) {
		auto *const var = static_cast<GVariant *>(l->data);
		result.push_back(Glib::VariantBase(var, true));
	}
	return result;
}

OutputFormat::OutputFormat(const struct sr_output_module *structure) :
	_structure(structure)
{
}

OutputFormat::~OutputFormat()
{
}

string OutputFormat::name() const
{
	return valid_string(sr_output_id_get(_structure));
}

string OutputFormat::description() const
{
	return valid_string(sr_output_description_get(_structure));
}

vector<string> OutputFormat::extensions() const
{
	vector<string> exts;
	for (const char *const *e = sr_output_extensions_get(_structure);
		e && *e; e++)
		exts.push_back(*e);
	return exts;
}

map<string, shared_ptr<Option>> OutputFormat::options()
{
	map<string, shared_ptr<Option>> result;

	if (const struct sr_option **options = sr_output_options_get(_structure))
	{
		shared_ptr<const struct sr_option *> option_array
			{options, &sr_output_options_free};
		for (int i = 0; options[i]; i++) {
			shared_ptr<Option> opt {
				new Option{options[i], option_array},
				default_delete<Option>{}};
			result.emplace(opt->id(), move(opt));
		}
	}
	return result;
}

shared_ptr<Output> OutputFormat::create_output(
	shared_ptr<Device> device, map<string, Glib::VariantBase> options)
{
	return shared_ptr<Output>{
		new Output{shared_from_this(), move(device), move(options)},
		default_delete<Output>{}};
}

shared_ptr<Output> OutputFormat::create_output(string filename,
	shared_ptr<Device> device, map<string, Glib::VariantBase> options)
{
	return shared_ptr<Output>{
		new Output{move(filename), shared_from_this(), move(device), move(options)},
		default_delete<Output>{}};
}

bool OutputFormat::test_flag(const OutputFlag *flag) const
{
	return sr_output_test_flag(_structure, flag->id());
}

Output::Output(shared_ptr<OutputFormat> format,
		shared_ptr<Device> device, map<string, Glib::VariantBase> options) :
	_structure(sr_output_new(format->_structure,
		map_to_hash_variant(options), device->_structure, nullptr)),
	_format(move(format)),
	_device(move(device)),
	_options(move(options))
{
}

Output::Output(string filename, shared_ptr<OutputFormat> format,
		shared_ptr<Device> device, map<string, Glib::VariantBase> options) :
	_structure(sr_output_new(format->_structure,
		map_to_hash_variant(options), device->_structure, filename.c_str())),
	_format(move(format)),
	_device(move(device)),
	_options(move(options))
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

#include <enums.cpp>

}
