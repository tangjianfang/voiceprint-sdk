# VoicePrint SDK Test Data

This directory contains test audio files for unit tests, integration tests, and evaluation.

## Structure

- Synthetic WAV files are generated automatically by test code
- For real evaluation, place trial data here:
  - `trials.txt` - Trial pair list (format: `label enroll_wav test_wav`)
  - Audio files referenced by the trial list

## Getting Real Test Data

### VoxCeleb1 Test Set
1. Register at https://www.robots.ox.ac.uk/~vgg/data/voxceleb/
2. Download VoxCeleb1 test set
3. Extract to `testdata/voxceleb1/`
4. Create trial list from VoxCeleb1-O

### Custom Data
- 16kHz WAV mono recommended
- Minimum 2 seconds per utterance
- At least 2 speakers with 3+ utterances each
