#include "libretro.h"
#include <nall/nall.hpp>
#include <emulator/emulator.hpp>

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_input_poll_t input_poll;
static retro_input_state_t input_state;
static retro_log_printf_t libretro_print;

#define ICARUS_LIBRARY
#include "../../icarus/icarus.cpp"

static string locate_libretro(string name)
{
	// Try libretro specific paths first ...
	// This is relevant for special chip ROMs/BIOS, etc.
	const char *sys = nullptr;
	if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys) && sys)
	{
		string location = { sys, "/higan/", name };
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

struct LibretroIcarus : Icarus
{
	LibretroIcarus()
	{
		settings["icarus/CreateManifests"].setValue(true);
	}

	// Stub out most of this. We use a memory-only interface.
	bool create(const string &) override
	{
		return true;
	}

	bool copy(const string &, const string &) override
	{
		return true;
	}

	bool exists(const string &) override
	{
		return true;
	}

	bool directory_exists(const string &) override
	{
		return true;
	}

	bool readable(const string &) override
	{
		return true;
	}

	bool write(const string &filename, const uint8_t *data, uint size) override
	{
		// To avoid any directories in the mix in case Icarus wants to write to ~/Emulation or similar.
		auto imported_path = Location::file(filename);
		vector<uint8_t> imported_data;

		imported_data.resize(size);
		memory::copy(imported_data.data(), data, size);
		imported_files.insert(imported_path, std::move(imported_data));
		libretro_print(RETRO_LOG_INFO, "Icarus writes file: %s.\n", static_cast<const char *>(imported_path));
		return true;
	}

	vector<uint8_t> read(const string &pathname) override
	{
		auto result = imported_files.find(pathname);
		if (result)
		{
			libretro_print(RETRO_LOG_INFO, "Icarus successfully read: %s.\n", static_cast<const char *>(pathname));
			return result.get();
		}
		else
		{
			libretro_print(RETRO_LOG_ERROR, "Icarus failed to read: %s.\n", static_cast<const char *>(pathname));
			return {};
		}
	}

	void reset()
	{
		imported_files.reset();
	}

	bool import_rom(const string &fake_path, const uint8_t *data, size_t size);

	map<string, vector<uint8_t>> imported_files;
};
static LibretroIcarus icarus;

bool LibretroIcarus::import_rom(const string &fake_path, const uint8_t *rom_data, size_t rom_size)
{
	icarus.reset();
	icarus.write(fake_path, rom_data, rom_size);

	if (icarus.import(fake_path))
		return true;

	auto missing_files = missing();
	if (!missing_files)
		return false;

	// If there are missing files, try to load them from disk, and append them in order to the ROM data in question.
	// If that fails, bail out.
	for (auto &rom : missing_files)
	{
		libretro_print(RETRO_LOG_INFO, "ROM did not include missing file: %s.\n", static_cast<const char *>(rom));

		auto &file = imported_files.find(fake_path).get();
		auto path = locate_libretro(rom);
		auto data = file::read(path);
		if (!data)
		{
			libretro_print(RETRO_LOG_ERROR, "Could not find missing file: %s.\n", static_cast<const char *>(rom));
			return false;
		}

		libretro_print(RETRO_LOG_INFO, "Found missing ROM in: %s.\n", static_cast<const char *>(path));

		size_t offset = file.size();
		file.resize(offset + data.size());
		memory::copy(file.data() + offset, data.data(), data.size());
	}

	// Try again.
	return icarus.import(fake_path);
}

struct Program : Emulator::Platform
{
	Program();
	~Program();

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

	string fake_game_path;
	string loaded_manifest;

	bool failed = false;
	bool polled = false;
	bool overscan = false;

	void poll_once();

	uint current_width = 0;
	uint current_height = 0;

	double overscan_crop_ratio_offset_x;
	double overscan_crop_ratio_offset_y;
	double overscan_crop_ratio_scale_x;
	double overscan_crop_ratio_scale_y;
};

static Program *program = nullptr;

#if defined(libretro_use_sfc)
#include "libretro-sfc.cpp"
#else
#error "Unrecognized higan core."
#endif

Program::Program()
{
	Emulator::platform = this;
	emulator = create_emulator_interface();
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

	// Load the game manifest.
	if (name == "manifest.bml" && id != BackendSpecific::system_id && loaded_manifest)
	{
		string manifest = loaded_manifest;

		libretro_print(RETRO_LOG_INFO,
				"Loaded Manifest:\n%s\n",
				static_cast<const char *>(manifest));

		return vfs::memory::file::open(manifest.data<uint8_t>(), manifest.size());
	}

	// Try to load static internal files.
	if (id == BackendSpecific::system_id)
	{
		auto builtin = load_builtin_system_file(name);
		if (builtin)
			return builtin;
	}

	// If this is file we imported in Icarus, use that.
	auto imported = icarus.imported_files.find(name);
	if (imported)
	{
		libretro_print(RETRO_LOG_INFO, "Reading imported file: %s.\n", static_cast<const char *>(name));

		auto &file = imported.get();
		if (name == "manifest.bml")
		{
			string manifest(reinterpret_cast<const char *>(file.data()), file.size());
			libretro_print(RETRO_LOG_INFO,
					"Loaded Manifest:\n%s\n",
					static_cast<const char *>(manifest));
		}
		return vfs::memory::file::open(file.data(), file.size());
	}

	// This will be the default save path as chosen during load.
	// If we load from manifest, this will always point to the appropriate directory.
	string p = { path(id), name };

	// If we're trying to load something, and it doesn't exist, try to find it elsewhere.
	if (mode == vfs::file::mode::read && !inode::exists(p))
	{
		libretro_print(RETRO_LOG_INFO,
				"%s does not exist, trying another path.\n",
				static_cast<const char *>(p));

		p = locate_libretro(name);
	}

	// Something else, load it from disk.
	libretro_print(RETRO_LOG_INFO,
			"Trying to %s file %s.\n", mode == vfs::file::mode::read ? "read" : "write",
			static_cast<const char *>(p));

	if (auto result = vfs::fs::file::open(p, mode))
		return result;

	// Fail load if necessary.
	if (required)
	{
		libretro_print(RETRO_LOG_ERROR, "Failed to open required file %s.\n", static_cast<const char *>(p));
		failed = true;
	}

	return {};
}

Emulator::Platform::Load Program::load(uint id, string name, string, string_vector options)
{
	// Have to return the first option here to get automatic region detection.
	return { id, options(0) };
}

void Program::videoRefresh(const uint32 *data, uint pitch, uint width, uint height)
{
	if (!program->overscan)
	{
		uint word_pitch = pitch >> 2;

		data += uint(round(width * overscan_crop_ratio_offset_x));
		data += word_pitch * uint(round(height * overscan_crop_ratio_offset_y));
		width = uint(round(width * overscan_crop_ratio_scale_x));
		height = uint(round(height * overscan_crop_ratio_scale_y));
	}

	if (width != program->current_width || height != program->current_height)
	{
		// If internal resolution changes, notify the frontend if it cares about it.
		retro_game_geometry geom = {};
		geom.base_width = width;
		geom.base_height = height;
		geom.aspect_ratio = emulator->videoInformation().aspectCorrection;
		program->current_width = width;
		program->current_height = height;
		environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geom);
	}

	// On at least SNES, this will be pixel doubled in most cases.
	// We could expose a core option to blend down to 256x224 or similar,
	// but I'd rather that be handled by special downsampling shaders.

	video_cb(data, width, height, pitch);
}

// Double the fun!
static int16_t d2i16(double v)
{
	v *= 0x8000;
	if (v > 0x7fff)
		v = 0x7fff;
	else if (v < -0x8000)
		v = -0x8000;
	return int16_t(floor(v + 0.5));
}

void Program::audioSample(const double *samples, uint channels)
{
	int16_t left = d2i16(samples[0]);
	int16_t right = d2i16(samples[1]);
	audio_cb(left, right);
}

void Program::inputRumble(uint port, uint device, uint input, bool enable)
{
	// Might be useful for other cores.
}

uint Program::dipSettings(Markup::Node)
{
	// Might be useful for other cores.
}

void Program::notify(string text)
{
	libretro_print(RETRO_LOG_INFO, "higan INFO: %s\n", static_cast<const char *>(text));
}

void Program::poll_once()
{
	// Poll as late as possible, frontend might also do it like this.
	if (!program->polled)
	{
		input_poll();
		program->polled = true;
	}
}

RETRO_API void retro_set_environment(retro_environment_t cb)
{
	environ_cb = cb;

	retro_log_callback log = {};
	if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log) && log.log)
		libretro_print = log.log;

	set_environment_info(cb);
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
	info->library_name = BackendSpecific::name;
	info->library_version = Emulator::Version;
	info->valid_extensions = BackendSpecific::extensions;
	info->need_fullpath = false;
	info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
	auto res = program->emulator->videoInformation();
	info->geometry.base_width = res.width;
	info->geometry.base_height = res.height;
	info->geometry.max_width = res.internalWidth;
	info->geometry.max_height = res.internalHeight;
	info->geometry.aspect_ratio = res.aspectCorrection;

	if (!program->overscan)
	{
		info->geometry.base_width =
			uint(round(info->geometry.base_width * program->overscan_crop_ratio_scale_x));
		info->geometry.base_height =
			uint(round(info->geometry.base_height * program->overscan_crop_ratio_scale_y));
	}

	info->timing.fps = res.refreshRate;

	libretro_print(RETRO_LOG_INFO, "Reported video rate: %.4f Hz.\n", info->timing.fps);

	// We control this.
	info->timing.sample_rate = BackendSpecific::audio_rate;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
	set_controller_ports(port, device);
}

RETRO_API void retro_reset()
{
	program->emulator->power();
}

RETRO_API void retro_run()
{
	program->polled = false;
	program->has_cached_serialize = false;
	program->emulator->run();
	program->poll_once(); // In case higan did not poll this frame.
}

RETRO_API size_t retro_serialize_size()
{
	// To avoid having to serialize twice to query the size -> serialize.
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
	// TODO: v094 implementation seems to have had something here, but this can wait.
	program->has_cached_serialize = false;
}

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
	// TODO: v094 implementation seems to have had something here, but this can wait.
	program->has_cached_serialize = false;
}

RETRO_API bool retro_load_game(const retro_game_info *game)
{
	// Need 8-bit (well, apparently 9-bit for SNES to be pedantic, but higan also uses 8-bit).
	retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
		return false;

	Emulator::audio.reset(2, BackendSpecific::audio_rate);
	const Emulator::Interface::Medium *emulator_medium = nullptr;

	// Each libretro implementation just has a single core, but it might have multiple mediums.
	for (auto &medium : program->emulator->media)
	{
		if (medium.type == BackendSpecific::medium_type)
		{
			emulator_medium = &medium;
			break;
		}
	}

	if (!program->emulator || !emulator_medium)
		return false;

	if (!environ_cb(RETRO_ENVIRONMENT_GET_OVERSCAN, &program->overscan))
		program->overscan = false;

	// Get the folder of the system directory.
	// Generally, this will go unused, but it will be relevant for some backends.
	program->medium_paths(BackendSpecific::system_id) = locate_libretro({ emulator_medium->name, ".sys/" });

	// TODO: Can we detect more robustly if we have a BML loaded from memory?
	// If we don't have a path (game loaded from pure VFS for example), we cannot use manifests.
	bool loading_manifest = game->path && string(game->path).endsWith(".bml");

	program->fake_game_path.reset();
	program->loaded_manifest.reset();

	if (loading_manifest)
	{
		// Load ROM and RAM from the directory.
		program->medium_paths(emulator_medium->id) = Location::dir(game->path);
		program->loaded_manifest = string_view(static_cast<const char *>(game->data), game->size);
	}
	else
	{
		// Try to find appropriate paths for save data.
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
			// Fallback. No idea, use the game SHA256.
			auto sha = string{ Hash::SHA256(static_cast<const uint8_t *>(game->data), game->size).digest(), ".sfc/" };

			const char *save = nullptr;
			if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save) && save)
			{
				string save_path = { save, "/", sha };
				directory::create(save_path);
				program->medium_paths(emulator_medium->id) = save_path;
			}
			else
			{
				// Use the system data somehow ... This is really deep into fallback territory.
				auto path = locate_libretro(sha);
				directory::create(path);
				program->medium_paths(emulator_medium->id) = path;
			}
		}

		// Import the game with Icarus.
		// Create a fake path for Icarus to import.
		// We need a sane extension so Icarus can dispatch to the right importer.
		program->fake_game_path = { "game.", BackendSpecific::medium_type };
		if (!icarus.import_rom(program->fake_game_path, static_cast<const uint8_t *>(game->data), game->size))
		{
			libretro_print(RETRO_LOG_ERROR, "Failed to import game with Icarus.\n");
			return false;
		}
	}

	libretro_print(RETRO_LOG_INFO, "Using base path: %s for game data.\n",
			static_cast<const char *>(program->path(emulator_medium->id)));

	if (!program->emulator->load(emulator_medium->id))
		return false;

	if (program->failed)
		return false;

	if (!program->emulator->loaded())
		return false;

	if (retro_get_region() == RETRO_REGION_NTSC)
	{
		program->overscan_crop_ratio_offset_x = BackendSpecific::NTSC::overscan_crop_ratio_offset_x;
		program->overscan_crop_ratio_offset_y = BackendSpecific::NTSC::overscan_crop_ratio_offset_y;
		program->overscan_crop_ratio_scale_x = BackendSpecific::NTSC::overscan_crop_ratio_scale_x;
		program->overscan_crop_ratio_scale_y = BackendSpecific::NTSC::overscan_crop_ratio_scale_y;
	}
	else
	{
		program->overscan_crop_ratio_offset_x = BackendSpecific::PAL::overscan_crop_ratio_offset_x;
		program->overscan_crop_ratio_offset_y = BackendSpecific::PAL::overscan_crop_ratio_offset_y;
		program->overscan_crop_ratio_scale_x = BackendSpecific::PAL::overscan_crop_ratio_scale_x;
		program->overscan_crop_ratio_scale_y = BackendSpecific::PAL::overscan_crop_ratio_scale_y;
	}

	// Setup some defaults.
	// TODO: Might want to use the option interface for these,
	// but most of these seem better suited for shaders tbh ...
	program->emulator->power();
	Emulator::video.setSaturation(1.0);
	Emulator::video.setGamma(1.0);
	Emulator::video.setLuminance(1.0);

	Emulator::video.setPalette();
	Emulator::audio.setFrequency(44100.0);
	Emulator::audio.setVolume(1.0);
	Emulator::audio.setBalance(0.0);
	Emulator::audio.setReverb(false);

	set_default_controller_ports();
	program->has_cached_serialize = false;

	retro_system_av_info av_info;
	retro_get_system_av_info(&av_info);
	program->current_width = av_info.geometry.base_width;
	program->current_height = av_info.geometry.base_height;

	return true;
}

RETRO_API bool retro_load_game_special(unsigned game_type,
		const struct retro_game_info *info, size_t num_info)
{
	// TODO: Sufami and other shenanigans?
	return false;
}

RETRO_API void retro_unload_game()
{
	program->emulator->unload();
}

RETRO_API unsigned retro_get_region()
{
	// This function isn't all that important,
	// but less than 59 FPS would mean PAL to account for possible variations in the implementation.
	if (program && program->emulator)
	{
		if (program->emulator->videoInformation().refreshRate < 59.0f)
			return RETRO_REGION_PAL;
		else
			return RETRO_REGION_NTSC;
	}
	else
	{
		// Shouldn't happen, but provide some fallback.
		return RETRO_REGION_NTSC;
	}
}

// Currently, there is no safe/sensible way to use the memory interface without severe hackery.
// Rely on higan to load and save SRAM until there is really compelling reason not to.
RETRO_API void *retro_get_memory_data(unsigned id)
{
	return nullptr;
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
	return 0;
}
