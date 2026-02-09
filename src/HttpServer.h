#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H


#include <WebServer.h>
#include <WebSocketsServer.h>
#include "Audio.h"
#include "LVGL_ST77916.h"


void wsServer_Begin();
void wsSendAudioChunk(const int16_t* samples, size_t byteCount);

void HttpServer_Begin(Audio& audio);
void HttpServer_Loop();

#endif