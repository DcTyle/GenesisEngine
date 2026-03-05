# Voice Corpus + Deterministic TTS (English-first)

Goal: enable self-hosted vocal output and measurable cross-modal learning without integrating external TTS engines.

What is implemented (CPU reference):

- WAV PCM16 mono reader/writer (`GE_audio_wav`)
- Audio frame features (RMS, ZCR, spectral centroid) (`GE_audio_features`)
- Speech corpus loaders:
  - Common Voice TSV + clips directory prefix (`GE_speech_alignment`)
  - Simple two-column manifest: `wav_path<TAB>text` (`GE_speech_alignment`)
- Deterministic minimal English TTS scaffold:
  - Fallback text->phones rules (deterministic lexicon + OOV grapheme rules (engine-owned))
  - Simple source-filter-ish synthesizer producing PCM16 mono (`GE_voice_synth`)
- Tools:
  - `ge_speech_ingest` (loads corpus manifest/tsv, validates WAVs, extracts features)
  - `ge_tts_speak` (generates a WAV from English text)

Notes:

- This is intentionally small and fully local. It is not neural TTS quality.
- The learning path is: ingest audio + captions -> align -> derive measurable features -> train trajectory-based predictors.
- Once `GE_language_foundation` loads CMUdict, the fallback G2P can be replaced with lexicon-driven phones.
