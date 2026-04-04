#ifndef CHATBOT_H
#define CHATBOT_H

#include "Audio.h"

// Initialize the chatbot module (call once during setup)
void Chatbot_Init(Audio& audio);

// Start a chatbot session: connect WS → backend → OpenAI Realtime API
void Chatbot_Start();

// Stop the chatbot session: disconnect WS, stop mic, stop playback
void Chatbot_Stop();

// Check if chatbot is currently active
bool Chatbot_IsActive();

// Send raw PCM16 audio chunk to the chatbot backend
void Chatbot_SendAudioChunk(const void* buffer, size_t byteCount);

// Get the last error occurred (if any)
const char* Chatbot_GetLastError();

// Clear current error status
void Chatbot_ClearError();

#endif // CHATBOT_H
