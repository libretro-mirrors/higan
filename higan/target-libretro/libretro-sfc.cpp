// Normally, these files are loaded from an external folder after Higan is installed,
// but we shouldn't require that for a libretro core.
// One alternative is installing this inside the asset directory or something.
static vfs::shared::file load_builtin_system_file(string name)
{
	if (name == "manifest.bml")
	{
		static const char manifest[] = R"(
system name:Super Famicom
  cpu version=2
    ram name=work.ram size=0x20000 volatile
  smp
    rom name=ipl.rom size=64
  ppu1 version=1
    ram name=video.ram size=0x8000 volatile
    ram name=object.ram size=544 volatile
  ppu2 version=3
    ram name=palette.ram size=512 volatile
  dsp
    ram name=apu.ram size=0x10000 volatile
)";
		return vfs::memory::file::open(reinterpret_cast<const uint8_t *>(manifest), strlen(manifest));
	}
	else if (name == "ipl.rom")
	{
		static const uint8_t ipl_rom[] = {
			0xcd, 0xef, 0xbd, 0xe8, 0x00, 0xc6, 0x1d, 0xd0, 0xfc, 0x8f, 0xaa, 0xf4,
			0x8f, 0xbb, 0xf5, 0x78, 0xcc, 0xf4, 0xd0, 0xfb, 0x2f, 0x19, 0xeb, 0xf4,
			0xd0, 0xfc, 0x7e, 0xf4, 0xd0, 0x0b, 0xe4, 0xf5, 0xcb, 0xf4, 0xd7, 0x00,
			0xfc, 0xd0, 0xf3, 0xab, 0x01, 0x10, 0xef, 0x7e, 0xf4, 0x10, 0xeb, 0xba,
			0xf6, 0xda, 0x00, 0xba, 0xf4, 0xc4, 0xf4, 0xdd, 0x5d, 0xd0, 0xdb, 0x1f,
			0x00, 0x00, 0xc0, 0xff
		};
		return vfs::memory::file::open(ipl_rom, sizeof(ipl_rom));
	}
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

static void set_default_controller_ports()
{
	// Super Famicom specific.
	program->emulator->connect(SuperFamicom::ID::Port::Controller1,
			SuperFamicom::ID::Device::Gamepad);
	program->emulator->connect(SuperFamicom::ID::Port::Controller2,
			SuperFamicom::ID::Device::Gamepad);
}

static void set_controller_ports(unsigned port, unsigned device)
{
	// TODO:
}

namespace BackendSpecific
{
static const char *extensions = "sfc|bml";
static const char *medium_type = "sfc";
static const char *name = "higan (Super Famicom)";
static const uint system_id = SuperFamicom::ID::System;
static const double audio_rate = 44100.0; // MSU-1 is 44.1k CD, so use that.
}

