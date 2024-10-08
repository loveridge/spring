/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef MUSIC_STREAM_H
#define MUSIC_STREAM_H

#include "System/Misc/SpringTime.h"

#include <al.h>
#include <ogg/ogg.h>
#include <vorbis/vorbisfile.h>

#include <array>
#include <string>


class MusicStream
{
public:
	MusicStream(ALuint _source = 0);
	~MusicStream();

	MusicStream(const MusicStream& rhs) = delete;
	MusicStream& operator=(const MusicStream& rhs) = delete;

	MusicStream(MusicStream&& rhs) noexcept { *this = std::move(rhs); }
	MusicStream& operator=(MusicStream&& rhs) noexcept;

	void Play(const std::string& path, float volume);
	void Stop();
	void Update();

	float GetPlayTime() const { return (msecsPlayed.toSecsf()); }
	float GetTotalTime() const { return totalTime; }

	bool TogglePause();
	bool Valid() const { return source != 0; }
	bool IsFinished() { return !Valid() || (GetPlayTime() >= GetTotalTime()); }

private:
	bool IsPlaying();
	bool StartPlaying();

	bool DecodeStream(ALuint buffer);
	void EmptyBuffers();
	void ReleaseBuffers();

	/**
	 * @brief Decode next part of the stream and queue it for playing
	 * @return whether it is the end of the stream
	 *   (check for IsPlaying() whether the complete stream was played)
	 */
	bool UpdateBuffers();

	static constexpr unsigned int NUM_BUFFERS = 2;

	std::vector<uint8_t> pcmDecodeBuffer;

	std::array<ALuint, NUM_BUFFERS> buffers;
	ALuint source;
	ALenum format;

	bool stopped;
	bool paused;

	spring_time msecsPlayed;
	spring_time lastTick;
	float totalTime;
};

#endif // MUSIC_STREAM_H
