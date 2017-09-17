#include "libretro.h"

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_input_poll_t input_poll;
static retro_input_state_t input_state;

RETRO_API void retro_set_environment(retro_environment_t cb)
{
	environ_cb = cb;
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
}

RETRO_API void retro_deinit()
{
}

RETRO_API unsigned retro_api_version()
{
	return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(retro_system_info *info)
{
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

RETRO_API void retro_reset()
{
}

RETRO_API void retro_run()
{
}

RETRO_API size_t retro_serialize_size()
{
	return 0;
}

RETRO_API bool retro_serialize(void *data, size_t size)
{
	return false;
}

RETRO_API bool retro_unserialize(const void *data, size_t size)
{
	return false;
}

RETRO_API void retro_cheat_reset()
{
}

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
}

RETRO_API bool retro_load_game(const retro_game_info *game)
{
	return false;
}

RETRO_API bool retro_load_game_special(unsigned game_type,
		const struct retro_game_info *info, size_t num_info)
{
	return false;
}

RETRO_API void retro_unload_game()
{
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
