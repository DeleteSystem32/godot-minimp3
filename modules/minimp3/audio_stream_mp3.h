#ifndef AUDIO_STREAM_MP3_H
#define AUDIO_STREAM_MP3_H

#include "core/io/resource_loader.h"
#include "servers/audio/audio_stream.h"
#include "thirdparty/minimp3/minimp3.h"

class AudioStreamMP3;

class AudioStreamPlaybackMP3 : public AudioStreamPlaybackResampled {

	GDCLASS(AudioStreamPlaybackMP3, AudioStreamPlaybackResampled);

	mp3dec_t *mp3d;
	uint32_t frames_mixed;
	mp3d_sample_t *pcm; //contains the current decoded mp3 frame

	int pcm_samples;
	int pcm_start_sample;
	uint32_t frame_byte_start;
	uint32_t frame_bytes;

	bool active;
	int loops;

	friend class AudioStreamMP3;

	Ref<AudioStreamMP3> mp3_stream; //vorbis_stream;

	void populate_first_frame(int, mp3dec_frame_info_t *);

protected:
	virtual void _mix_internal(AudioFrame *p_buffer, int p_frames);
	virtual float get_stream_sampling_rate();

public:
	virtual void start(float p_from_pos = 0.0);
	virtual void stop();
	virtual bool is_playing() const;

	virtual int get_loop_count() const; //times it looped

	virtual float get_playback_position() const;
	virtual void seek(float p_time);

	AudioStreamPlaybackMP3() {}
	~AudioStreamPlaybackMP3();
};

class AudioStreamMP3 : public AudioStream {

	GDCLASS(AudioStreamMP3, AudioStream);
	OBJ_SAVE_TYPE(AudioStream) //children are all saved as AudioStream, so they can be exchanged
	RES_BASE_EXTENSION("mp3str");

	friend class AudioStreamPlaybackMP3;

	void *data;
	uint32_t data_len;

	float avg_bitrate_kbps;
	int total_frame_size;
	int total_samples;
	uint32_t max_frame_bytes;

	int decode_mem_size;
	float sample_rate;
	int channels;
	float length;
	bool loop;
	float loop_offset;
	void clear_data();

protected:
	static void _bind_methods();

public:
	void set_loop(bool p_enable);
	bool has_loop() const;

	void set_loop_offset(float p_seconds);
	float get_loop_offset() const;

	virtual Ref<AudioStreamPlayback> instance_playback();
	virtual String get_stream_name() const;

	void set_data(const PoolVector<uint8_t> &p_data);
	PoolVector<uint8_t> get_data() const;

	virtual float get_length() const; //if supported, otherwise return 0

	AudioStreamMP3();
	virtual ~AudioStreamMP3();
};

#endif
