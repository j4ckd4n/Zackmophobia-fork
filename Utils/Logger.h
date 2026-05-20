#pragma once

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <mutex>
#include <string>
#include <deque>

namespace Logger {
     inline std::mutex gMutex;
     inline FILE* gFile = nullptr;

     static constexpr int MAX_LOG_LINES = 30;
     inline std::deque<std::string> gLogLines;

     inline void Init()
     {
          std::lock_guard<std::mutex> lock(gMutex);
          if (gFile) {
               return;
          }

          char modulePath[MAX_PATH] = {};
          if (!GetModuleFileNameA(nullptr, modulePath, MAX_PATH)) {
               fopen_s(&gFile, "Zackmophobia.log", "a");
               return;
          }

          std::string logPath(modulePath);
          size_t slashPos = logPath.find_last_of("\\/");
          if (slashPos != std::string::npos) {
               logPath = logPath.substr(0, slashPos + 1);
          } else {
               logPath.clear();
          }

          logPath += "Zackmophobia.log";
          fopen_s(&gFile, logPath.c_str(), "a");
     }

     inline void Log(const char* fmt, ...)
     {
          char msg[1024] = {};
          va_list args;
          va_start(args, fmt);
          vsnprintf(msg, sizeof(msg), fmt, args);
          va_end(args);

          SYSTEMTIME st;
          GetLocalTime(&st);

          char line[1300] = {};
          snprintf(
               line,
               sizeof(line),
               "[%02u:%02u:%02u] %s",
               st.wHour,
               st.wMinute,
               st.wSecond,
               msg
          );

          std::lock_guard<std::mutex> lock(gMutex);

          // Queue for menu display
          gLogLines.push_back(line);
          if ((int)gLogLines.size() > MAX_LOG_LINES)
               gLogLines.pop_front();

          // File sink
          if (gFile) {
               fputs(line, gFile);
               fputc('\n', gFile);
               fflush(gFile);
          }

          OutputDebugStringA(line);
     }
}
