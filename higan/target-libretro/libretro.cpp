#include "libretro.h"
#include <nall/nall.hpp>
#include <emulator/emulator.hpp>

#include <sfc/interface/interface.hpp>

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_input_poll_t input_poll;
static retro_input_state_t input_state;
static retro_log_printf_t libretro_print;

#include "../../icarus/heuristics/super-famicom.cpp"

static string locate(string name)
{
	// Try libretro specific paths first ...
	const char *sys = nullptr;
	if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys) && sys)
	{
		string location = { sys, "/higan/", name };
		if (inode::exists(location))
			return location;
	}

	const char *save = nullptr;
	if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save) && save)
	{
		string location = { save, "/higan/", name };
		if (inode::exists(location))
			return location;
	}

	// If not, fall back to standard higan paths.
	string location = { Path::config(), "higan/", name };
	if (inode::exists(location))
		return location;

	directory::create({ Path::local(), "higan/" });
	return { Path::local(), "higan/", name };
}

struct Program : Emulator::Platform
{
	Program();
	~Program();

	vector<Emulator::Interface *> emulators;
	Emulator::Interface *emulator = nullptr;

	string path(uint id) override;
	vfs::shared::file open(uint id, string name, vfs::file::mode mode, bool required) override;
	Emulator::Platform::Load load(uint id, string name, string type, string_vector options) override;
	void videoRefresh(const uint32 *data, uint pitch, uint width, uint height) override;
	void audioSample(const double *samples, uint channels) override;
	int16 inputPoll(uint port, uint device, uint input) override;
	void inputRumble(uint port, uint device, uint input, bool enable) override;
	uint dipSettings(Markup::Node node) override;
	void notify(string text) override;

	vector<string> medium_paths;

	serializer cached_serialize;
	bool has_cached_serialize = false;

	const uint8_t *game_data = nullptr;
	size_t game_size = 0;
	bool failed = false;
};

static Program *program = nullptr;

Program::Program()
{
	Emulator::platform = this;
	emulator = new SuperFamicom::Interface;
}

Program::~Program()
{
	delete emulator;
}

string Program::path(uint id)
{
	return medium_paths(id);
}

vfs::shared::file Program::open(uint id, string name, vfs::file::mode mode, bool required)
{
	libretro_print(RETRO_LOG_INFO, "Accessing data from %u: %s (required: %s)\n",
			id, static_cast<const char *>(name), required ? "yes" : "no");

	// Load the system-wide manifest.
	if (name == "manifest.bml" && id != SuperFamicom::ID::System)
	{
		auto cart = SuperFamicomCartridge(game_data, game_size);
		string manifest = cart.markup;
		libretro_print(RETRO_LOG_INFO, "Manifest:\n%s\n", static_cast<const char *>(manifest));
		return vfs::memory::file::open(manifest.data<uint8_t>(), manifest.size());
	}

	// This is the game, so load from memory.
	if (name == "program.rom")
		return vfs::memory::file::open(game_data, game_size);

	string p = locate(name);
	if (!inode::exists(p))
	{
		libretro_print(RETRO_LOG_INFO, "%s does not exist, trying another path.\n", static_cast<const char *>(p));
		p = { path(id), name };
	}

	// Something else, load it from disk.
	libretro_print(RETRO_LOG_INFO, "Trying to access file %s.\n", static_cast<const char *>(p));
	if (auto result = vfs::fs::file::open(p, mode))
		return result;

	if (required)
	{
		libretro_print(RETRO_LOG_ERROR, "Failed to open required file %s.\n", static_cast<const char *>(p));
		failed = true;
	}

	return {};
}

Emulator::Platform::Load Program::load(uint id, string name, string, string_vector)
{
	return { id, name };
}

void Program::videoRefresh(const uint32 *data, uint pitch, uint width, uint height)
{
	video_cb(data, width, height, pitch);
}

static int16_t d2i16(double v)
{
	v *= 0x8000;
	if (v > 0x7fff)
		v = 0x7fff;
	else if (v < -0x8000)
		v = -0x8000;
	return int16_t(v);
}

void Program::audioSample(const double *samples, uint channels)
{
	int16_t left = d2i16(samples[0]);
	int16_t right = d2i16(samples[1]);
	audio_cb(left, right);
}

int16 Program::inputPoll(uint port, uint device, uint input)
{
	// TODO: This will need to be remapped on a per-system basis.
	unsigned libretro_port;
	unsigned libretro_index = 0;
	unsigned libretro_id;
	unsigned libretro_device;

	// It is not possible to just include sfc/controller/gamepad/gamepad.hpp without severe include errors, so
	// don't bother ...
	static const unsigned joypad_mapping[] = {
		RETRO_DEVICE_ID_JOYPAD_UP,
		RETRO_DEVICE_ID_JOYPAD_DOWN,
		RETRO_DEVICE_ID_JOYPAD_LEFT,
		RETRO_DEVICE_ID_JOYPAD_RIGHT,
		RETRO_DEVICE_ID_JOYPAD_B,
		RETRO_DEVICE_ID_JOYPAD_A,
		RETRO_DEVICE_ID_JOYPAD_Y,
		RETRO_DEVICE_ID_JOYPAD_X,
		RETRO_DEVICE_ID_JOYPAD_L,
		RETRO_DEVICE_ID_JOYPAD_R,
		RETRO_DEVICE_ID_JOYPAD_SELECT,
		RETRO_DEVICE_ID_JOYPAD_START,
	};

	switch (port)
	{
		case SuperFamicom::ID::Port::Controller1:
			libretro_port = 0;
			break;
		case SuperFamicom::ID::Port::Controller2:
			libretro_port = 1;
			break;

		default:
			return 0;
	}

	switch (device)
	{
		case SuperFamicom::ID::Device::Gamepad:
			libretro_device = RETRO_DEVICE_JOYPAD;
			libretro_id = joypad_mapping[input];
			break;

		default:
			return 0;
	}

	return input_state(libretro_port, libretro_device, libretro_index, libretro_id);
}

void Program::inputRumble(uint port, uint device, uint input, bool enable)
{
}

uint Program::dipSettings(Markup::Node node)
{
}

void Program::notify(string text)
{
}

RETRO_API void retro_set_environment(retro_environment_t cb)
{
	environ_cb = cb;

	retro_log_callback log = {};
	if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log) && log.log)
		libretro_print = log.log;
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
	video_cb = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
	audio_cb = cb;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t)
{
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb)
{
	input_poll = cb;
}

RETRO_API void retro_set_input_state(retro_input_state_t cb)
{
	input_state = cb;
}

RETRO_API void retro_init()
{
	program = new Program;
}

RETRO_API void retro_deinit()
{
	delete program;
	program = nullptr;
}

RETRO_API unsigned retro_api_version()
{
	return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(retro_system_info *info)
{
	info->library_name = "higan (Super Famicom)";
	info->library_version = Emulator::Version;
	info->valid_extensions = "sfc";
	info->need_fullpath = false;
	info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
	auto res = program->emulator->videoResolution();
	info->geometry.base_width = res.width;
	info->geometry.base_height = res.height;
	info->geometry.max_width = res.internalWidth;
	info->geometry.max_height = res.internalHeight;
	info->geometry.aspect_ratio = res.aspectCorrection;

	// TODO: This is currently not exposed in Emulator::Interface.
	info->timing.fps = 21477272.0 / 357366.0;
	info->timing.sample_rate = 44100.0;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

RETRO_API void retro_reset()
{
	program->emulator->power();
}

RETRO_API void retro_run()
{
	program->has_cached_serialize = false;
	input_poll();
	program->emulator->run();
}

RETRO_API size_t retro_serialize_size()
{
	if (program->has_cached_serialize)
	{
		return program->cached_serialize.size();
	}
	else
	{
		program->cached_serialize = program->emulator->serialize();
		program->has_cached_serialize = true;
		return program->cached_serialize.size();
	}
}

RETRO_API bool retro_serialize(void *data, size_t size)
{
	if (!program->has_cached_serialize)
	{
		program->cached_serialize = program->emulator->serialize();
		program->has_cached_serialize = true;
	}

	if (program->cached_serialize.size() != size)
		return false;

	memcpy(data, program->cached_serialize.data(), size);
	return true;
}

RETRO_API bool retro_unserialize(const void *data, size_t size)
{
	serializer s(static_cast<const uint8_t *>(data), size);
	program->has_cached_serialize = false;
	return program->emulator->unserialize(s);
}

RETRO_API void retro_cheat_reset()
{
	program->has_cached_serialize = false;
}

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
	program->has_cached_serialize = false;
}

RETRO_API bool retro_load_game(const retro_game_info *game)
{
	retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
		return false;

	Emulator::audio.reset(2, 44100);
	const Emulator::Interface::Medium *emulator_medium = nullptr;

	for (auto &medium : program->emulator->media)
	{
		if (medium.type == "sfc")
		{
			emulator_medium = &medium;
			break;
		}
	}

	if (!program->emulator || !emulator_medium)
		return false;

	program->medium_paths(SuperFamicom::ID::System) = locate({ emulator_medium->name, ".sys/" });

	// Try to find appropriate paths for save data.
	// TODO: We need some sensible way to use retro_get_memory_data() I think ...
	if (game->path)
	{
		auto base_name = string(game->path);
		string save_path;

		const char *save = nullptr;
		if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save) && save)
			save_path = { save, "/", Location::base(base_name), "." };
		else
			save_path = { base_name.trimRight(Location::suffix(base_name)), "." };

		program->medium_paths(emulator_medium->id) = save_path;
	}
	else
	{
		// No idea, use the game SHA256.
		auto sha = string{ program->emulator->sha256(), ".sfc" };

		const char *save = nullptr;
		if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save) && save)
		{
			string save_path = { save, "/", sha };
			directory::create(save_path);
			program->medium_paths(emulator_medium->id) = save_path;
		}

		// Use the system data somehow ...
		auto path = locate(sha);
		if (path)
			directory::create(path);
		program->medium_paths(emulator_medium->id) = path;
	}

	libretro_print(RETRO_LOG_INFO, "Using base path: %s for game data.\n", static_cast<const char *>(program->path(emulator_medium->id)));

	program->game_data = static_cast<const uint8_t *>(game->data);
	program->game_size = game->size;

	if (!program->emulator->load(emulator_medium->id))
		return false;

	if (program->failed)
		return false;

	if (!program->emulator->loaded())
		return false;

	// Setup some defaults.
	program->emulator->power();
	Emulator::video.setSaturation(1.0);
	Emulator::video.setGamma(1.0);
	Emulator::video.setLuminance(1.0);
	Emulator::video.setPalette();
	Emulator::audio.setFrequency(44100.0);
	Emulator::audio.setVolume(1.0);
	Emulator::audio.setBalance(0.0);
	Emulator::audio.setReverb(false);

	// Super Famicom specific.
	program->emulator->connect(SuperFamicom::ID::Port::Controller1,
			SuperFamicom::ID::Device::Gamepad);
	program->emulator->connect(SuperFamicom::ID::Port::Controller2,
			SuperFamicom::ID::Device::Gamepad);

	program->has_cached_serialize = false;

	return true;
}

RETRO_API bool retro_load_game_special(unsigned game_type,
		const struct retro_game_info *info, size_t num_info)
{
	return false;
}

RETRO_API void retro_unload_game()
{
	program->emulator->unload();
}

RETRO_API unsigned retro_get_region()
{
}

RETRO_API void *retro_get_memory_data(unsigned id)
{
	return nullptr;
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
	return 0;
}
