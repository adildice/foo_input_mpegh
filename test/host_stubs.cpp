// Minimal stand-ins for the functions foobar2000 (shared.dll / the host) normally provides, so that
// the decoder self-test links and runs without foobar2000. Not part of the component.
#include <foobar2000/SDK/foobar2000.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

extern "C" {
void uBugCheck() {
  fprintf(stderr, "uBugCheck\n");
  abort();
}
void uOutputDebugString(const char* msg) { fputs(msg, stderr); }
pfc::eventHandle_t GetInfiniteWaitEvent() { return pfc::eventInvalid; }
int stricmp_utf8(const char* a, const char* b) throw() { return pfc::stringCompareCaseInsensitive(a, b); }
int stricmp_utf8_ex(const char* a, t_size la, const char* b, t_size lb) throw() {
  return pfc::stringCompareCaseInsensitiveEx(pfc::string_part(a, pfc::strlen_max(a, la)),
                                             pfc::string_part(b, pfc::strlen_max(b, lb)));
}
}

#ifndef _WIN32
void uPrintfV(pfc::string_base& out, const char* fmt, va_list args) {
  char buf[2048];
  vsnprintf(buf, sizeof(buf), fmt, args);
  out = buf;
}
namespace fb2k {
void crashWithMessage(const char* msg) {
  fprintf(stderr, "crash: %s\n", msg);
  abort();
}
}
#endif

namespace core_api {
bool is_main_thread() { return true; }
bool are_services_available() { return false; }
const char* get_my_file_name() { return "selftest"; }
}

#undef audio_math
namespace audio_math {
void scale(const audio_sample* s, t_size n, audio_sample* o, audio_sample k) { audio_math_pfc::scale(s, n, o, k); }
void convert_to_int16(const audio_sample* s, t_size n, t_int16* o, audio_sample k) { audio_math_pfc::convert_to_int16(s, n, o, k); }
void convert_to_int32(const audio_sample* s, t_size n, t_int32* o, audio_sample k) { audio_math_pfc::convert_to_int32(s, n, o, k); }
void convert_from_int16(const t_int16* s, t_size n, audio_sample* o, audio_sample k) { audio_math_pfc::convert_from_int16(s, n, o, k); }
void convert_from_int32(const t_int32* s, t_size n, audio_sample* o, audio_sample k) { audio_math_pfc::convert_from_int32(s, n, o, k); }
audio_sample calculate_peak(const audio_sample* s, t_size n) { return audio_math_pfc::calculate_peak(s, n); }
}
