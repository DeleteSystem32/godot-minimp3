#define MINIMP3_ONLY_MP3
#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_IMPLEMENTATION

#include "audio_stream_mp3.h"

#include "core/os/file_access.h"

void AudioStreamPlaybackMP3::_mix_internal(AudioFrame *p_buffer, int p_frames) {

	ERR_FAIL_COND(!active);

	int todo = p_frames;

	mp3dec_frame_info_t info;

	while (todo && active) {
		int next_frame = frames_mixed;

		//populate pcm buffer w/ one full mp3 frame

		if (!pcm_samples || next_frame < pcm_start_sample) {
			populate_first_frame(next_frame, &info);
		}

		while (next_frame >= pcm_start_sample + pcm_samples) {
			//seek forward
			frame_byte_start += frame_bytes;
			pcm_start_sample += pcm_samples;

			if (frame_byte_start >= mp3_stream->data_len || !frame_bytes) {
				//loop
				if (mp3_stream->loop) {
					seek(mp3_stream->loop_offset);
					loops++;
					next_frame = frames_mixed;
					populate_first_frame(next_frame, &info);
					break;
				} else {
					//fill remainder of frames
					for (int i = p_frames - todo; i < p_frames; i++) {
						p_buffer[i] = AudioFrame(0, 0);
					}
					active = false;
					return;
				}
			}

			uint32_t real_byte_len = mp3_stream->max_frame_bytes <= mp3_stream->data_len - frame_byte_start ? mp3_stream->max_frame_bytes : mp3_stream->data_len - frame_byte_start;

			pcm_samples = mp3dec_decode_frame(mp3d, ((const uint8_t *)mp3_stream->data) + frame_byte_start, real_byte_len, pcm, &info);
			frame_bytes = info.frame_bytes;
		}

		//then, copy desired 2 samples to audioframe
		int local_sample = next_frame - pcm_start_sample;
		p_buffer[p_frames - todo] = AudioFrame(pcm[local_sample * mp3_stream->channels], pcm[local_sample * mp3_stream->channels + (mp3_stream->channels - 1)]);
		frames_mixed += 1;
		todo -= 1;
	}
}

void AudioStreamPlaybackMP3::populate_first_frame(int start_at, mp3dec_frame_info_t *info) {
	frame_byte_start = 0;
	pcm_start_sample = 0;
	pcm_samples = 0;

	do {
		int real_byte_len = mp3_stream->max_frame_bytes <= mp3_stream->data_len - frame_byte_start ? mp3_stream->max_frame_bytes : mp3_stream->data_len - frame_byte_start;
		pcm_samples = mp3dec_decode_frame(mp3d, ((const uint8_t *)mp3_stream->data) + frame_byte_start, real_byte_len, pcm, info);
		frame_bytes = info->frame_bytes;
		if (pcm_samples && start_at >= pcm_start_sample && start_at < pcm_start_sample + pcm_samples) {
			//found needed sample
			break;
		}
		frame_byte_start += frame_bytes;
		pcm_start_sample += pcm_samples;
	} while ((!pcm_samples || start_at < pcm_start_sample) && frame_byte_start < mp3_stream->data_len);
}

float AudioStreamPlaybackMP3::get_stream_sampling_rate() {

	return mp3_stream->sample_rate;
}

void AudioStreamPlaybackMP3::start(float p_from_pos) {

	active = true;
	seek(p_from_pos);
	loops = 0;
	_begin_resample();
}

void AudioStreamPlaybackMP3::stop() {

	active = false;
}
bool AudioStreamPlaybackMP3::is_playing() const {

	return active;
}

int AudioStreamPlaybackMP3::get_loop_count() const {

	return loops;
}

float AudioStreamPlaybackMP3::get_playback_position() const {
	return float(frames_mixed) / float(mp3_stream->sample_rate);
}
void AudioStreamPlaybackMP3::seek(float p_time) {
	if (!active)
		return;

	if (p_time >= mp3_stream->get_length()) {
		p_time = 0;
	}
	frames_mixed = uint32_t(mp3_stream->sample_rate * p_time);

	frame_bytes = 0;
	frame_byte_start = 0;
	pcm_samples = 0;
	pcm_start_sample = 0;
}

AudioStreamPlaybackMP3::~AudioStreamPlaybackMP3() {
	if (pcm && mp3d) {
		AudioServer::get_singleton()->audio_data_free(mp3d);
		mp3d = NULL;
		AudioServer::get_singleton()->audio_data_free(pcm);
		pcm = NULL;
	}
}

Ref<AudioStreamPlayback> AudioStreamMP3::instance_playback() {
	Ref<AudioStreamPlaybackMP3> mp3s;

	ERR_FAIL_COND_V(data == NULL, mp3s);

	mp3s.instance();
	mp3s->mp3_stream = Ref<AudioStreamMP3>(this);

	mp3s->mp3d = (mp3dec_t *)AudioServer::get_singleton()->audio_data_alloc(sizeof(mp3dec_t));
	mp3dec_init(mp3s->mp3d);
	mp3s->pcm = (mp3d_sample_t *)AudioServer::get_singleton()->audio_data_alloc(decode_mem_size);
	mp3s->frames_mixed = 0;
	mp3s->active = false;
	mp3s->loops = 0;
	mp3s->pcm_samples = 0;
	mp3s->pcm_start_sample = 0;
	mp3s->frame_bytes = 0;
	mp3s->frame_byte_start = 0;
	if (!mp3s->mp3d) {

		AudioServer::get_singleton()->audio_data_free(mp3s->pcm);
		mp3s->pcm = NULL;
		ERR_FAIL_COND_V(!mp3s->mp3d, Ref<AudioStreamPlaybackMP3>());
	}

	return mp3s;
}

String AudioStreamMP3::get_stream_name() const {

	return ""; //return stream_name;
}

void AudioStreamMP3::clear_data() {
	if (data) {
		AudioServer::get_singleton()->audio_data_free(data);
		data = NULL;
		data_len = 0;
	}
}

void AudioStreamMP3::set_data(const PoolVector<uint8_t> &p_data) {

	int src_data_len = p_data.size();

	mp3dec_frame_info_t info;
	bool info_initialised = false;

	mp3dec_t mp3d;
	mp3dec_init(&mp3d);

	PoolVector<uint8_t>::Read src_datar = p_data.read();
	int total_frames = 0;
	int samples = 0;
	int bitrate_accu = 0;
	total_samples = 0;
	total_frame_size = 0;

	do {
		//pcm = nullptr so it doesn't have to decode the entire file
		samples = mp3dec_decode_frame(&mp3d, src_datar.ptr() + total_frame_size, src_data_len - total_frame_size, nullptr, &info);

		total_frame_size += info.frame_bytes;
		max_frame_bytes = info.frame_bytes > max_frame_bytes ? info.frame_bytes : max_frame_bytes;
		if (samples) {
			++total_frames;
			bitrate_accu += info.bitrate_kbps;
			total_samples += samples;

			if (!info_initialised) {
				sample_rate = info.hz;
				channels = info.channels;
				info_initialised = true;
			}
		}

	} while (total_frame_size < src_data_len && info.frame_bytes);

	if (total_frames > 0) {
		avg_bitrate_kbps = float(bitrate_accu) / float(total_frames);

		length = float(total_samples) / float(sample_rate);
	}

	//todo: error if invalid file

	decode_mem_size = MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(mp3d_sample_t); //maximum frame size in bytes

	// free any existing data
	clear_data();

	data = AudioServer::get_singleton()->audio_data_alloc(src_data_len, src_datar.ptr());
	data_len = src_data_len;
}

PoolVector<uint8_t> AudioStreamMP3::get_data() const {

	PoolVector<uint8_t> vdata;

	if (data_len && data) {
		vdata.resize(data_len);
		{
			PoolVector<uint8_t>::Write w = vdata.write();
			copymem(w.ptr(), data, data_len);
		}
	}

	return vdata;
}

void AudioStreamMP3::set_loop(bool p_enable) {
	loop = p_enable;
}

bool AudioStreamMP3::has_loop() const {

	return loop;
}

void AudioStreamMP3::set_loop_offset(float p_seconds) {
	loop_offset = p_seconds;
}

float AudioStreamMP3::get_loop_offset() const {
	return loop_offset;
}

float AudioStreamMP3::get_length() const {

	return length;
}

void AudioStreamMP3::_bind_methods() {

	ClassDB::bind_method(D_METHOD("set_data", "data"), &AudioStreamMP3::set_data);
	ClassDB::bind_method(D_METHOD("get_data"), &AudioStreamMP3::get_data);

	ClassDB::bind_method(D_METHOD("set_loop", "enable"), &AudioStreamMP3::set_loop);
	ClassDB::bind_method(D_METHOD("has_loop"), &AudioStreamMP3::has_loop);

	ClassDB::bind_method(D_METHOD("set_loop_offset", "seconds"), &AudioStreamMP3::set_loop_offset);
	ClassDB::bind_method(D_METHOD("get_loop_offset"), &AudioStreamMP3::get_loop_offset);

	ADD_PROPERTY(PropertyInfo(Variant::POOL_BYTE_ARRAY, "data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR), "set_data", "get_data");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "loop", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR), "set_loop", "has_loop");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "loop_offset", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR), "set_loop_offset", "get_loop_offset");
}

AudioStreamMP3::AudioStreamMP3() {

	data = NULL;
	data_len = 0;
	length = 0;
	sample_rate = 1;
	channels = 1;
	loop_offset = 0;
	decode_mem_size = 0;
	loop = false;

	avg_bitrate_kbps = 0;
	total_frame_size = 0;
	total_samples = 0;
	max_frame_bytes = 0;
}

AudioStreamMP3::~AudioStreamMP3() {
	clear_data();
}
