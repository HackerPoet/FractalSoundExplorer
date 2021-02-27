#include "WinAudio.h"
#include <Mmreg.h>
#include <iostream>
#include <cassert>

WinAudio* WinAudio::WIN_AUDIO = NULL;

WinAudio::WinAudio(HWND hwnd, int sample_rate) {
  //Initialize variables
  m_CurWaveOut = 0;
  m_SampleRate = sample_rate;
  m_IsReleasing = false;

  //Specify output parameters
  m_Format.wFormatTag = WAVE_FORMAT_PCM;        // simple, uncompressed format
  m_Format.nChannels = 2;                       // 1=mono, 2=stereo
  m_Format.nSamplesPerSec = sample_rate;        // sample rate
  m_Format.nAvgBytesPerSec = sample_rate * 2;   // nSamplesPerSec * n.Channels * wBitsPerSample/8
  m_Format.nBlockAlign = 4;                     // n.Channels * wBitsPerSample/8
  m_Format.wBitsPerSample = 16;                 // 16 for high quality, 8 for telephone-grade
  m_Format.cbSize = 0;                          // must be set to zero

  //Set handle
  assert(WIN_AUDIO == NULL);
  WIN_AUDIO = this;
}

WinAudio::~WinAudio() {
  stop();
}

bool WinAudio::play() {
  MMRESULT result;
  CHAR fault[256];

  //Create a mutex
  m_Mutex = CreateMutex(NULL, FALSE, NULL);
  if (m_Mutex == NULL) {
    std::cout << "Mutex failed.";
    return false;
  }

  //Open the audio driver
  m_IsReleasing = false;
  result = waveOutOpen(&m_HWaveOut, WAVE_MAPPER, &m_Format, (DWORD_PTR)Callback, NULL, CALLBACK_FUNCTION);
  if (result != MMSYSERR_NOERROR) {
    waveInGetErrorText(result, fault, 256);
    std::cout << fault << std::endl;
    return false;
  }

  // Set up and prepare header for output
  for (int i = 0; i < NUM_AUDIO_BUFFS; i++) {
    memset(m_WaveOut[i], 0, AUDIO_BUFF_SIZE * sizeof(int16_t));
    memset(&m_WaveOutHdr[i], 0, sizeof(WAVEHDR));
    m_WaveOutHdr[i].lpData = (LPSTR)m_WaveOut[i];
    m_WaveOutHdr[i].dwBufferLength = AUDIO_BUFF_SIZE * sizeof(int16_t);
    result = waveOutPrepareHeader(m_HWaveOut, &m_WaveOutHdr[i], sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
      waveInGetErrorText(result, fault, 256);
      std::cout << fault << std::endl;
    }
  }

  SubmitBuffer();
  SubmitBuffer();
  return true;
}

bool WinAudio::stop() {
  MMRESULT result;
  CHAR fault[256];
  if (m_IsReleasing) {
    return true;
  }
  WaitForSingleObject(m_Mutex, INFINITE);
  m_IsReleasing = true;
  for (int i = 0; i < NUM_AUDIO_BUFFS; i++) {
    result = waveOutReset(m_HWaveOut);
    if (result != MMSYSERR_NOERROR) {
      waveInGetErrorText(result, fault, 256);
      std::cout << fault << std::endl;
      return false;
    }
    result = waveOutUnprepareHeader(m_HWaveOut, &m_WaveOutHdr[i], sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
      waveInGetErrorText(result, fault, 256);
      std::cout << fault << std::endl;
      return false;
    }
  }
  waveOutClose(m_HWaveOut);
  ReleaseMutex(m_Mutex);
  return true;
}

void WinAudio::SubmitBuffer() {
  MMRESULT result;
  CHAR fault[256];

  //Reject if releasing
  if (m_IsReleasing) { return; }

  //Write the audio to the sound card
  WaitForSingleObject(m_Mutex, INFINITE);
  result = waveOutWrite(m_HWaveOut, &m_WaveOutHdr[m_CurWaveOut], sizeof(WAVEHDR));

  //Check for errors
  if (result != MMSYSERR_NOERROR) {
    waveInGetErrorText(result, fault, 256);
    std::cout << fault << std::endl;
  }
  m_CurWaveOut = (m_CurWaveOut + 1) % NUM_AUDIO_BUFFS;

  //Generate next music
  Chunk chunk;
  onGetData(chunk);
  memcpy(m_WaveOut[m_CurWaveOut], chunk.samples, chunk.sampleCount * sizeof(int16_t));

  //Release the lock
  ReleaseMutex(m_Mutex);
}

void CALLBACK WinAudio::Callback(HWAVEOUT hWaveOut, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2) {
  // Only listen for end of block messages.
  if (uMsg != WOM_DONE) { return; }
  WIN_AUDIO->SubmitBuffer();
}
