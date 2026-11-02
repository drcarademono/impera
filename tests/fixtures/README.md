`audio-tone.ogg` is a generated 0.12-second, 440 Hz sine wave (stereo, 48 kHz)
used to test Ogg decoding and packaged music. It contains no game audio.

Generated with:
`ffmpeg -f lavfi -i sine=frequency=440:sample_rate=48000:duration=0.12 -ac 2 -c:a libvorbis -q:a 1 -map_metadata -1 audio-tone.ogg`
