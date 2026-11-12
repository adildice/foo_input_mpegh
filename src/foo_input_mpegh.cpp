// foo_input_mpegh - MPEG-H 3D Audio (mha1 / mhm1 in MP4) decoder for foobar2000
//
// Demuxing : Fraunhofer mmtisobmff
// Decoding : Fraunhofer FDK MPEG-H decoder (mpeghdec)
//
// The input claims .mp4 / .m4a files but rejects every file that has no MPEG-H
// sample entry, so foobar2000's own MP4 decoder keeps handling ordinary files.

#include <foobar2000/SDK/foobar2000.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "ilo/memory.h"
#include "mmtisobmff/types.h"
#include "mmtisobmff/reader/input.h"
#include "mmtisobmff/reader/reader.h"
#include "mmtisobmff/reader/trackreader.h"
#include "mpeghdecoder.h"

using namespace mmt::isobmff;

#define COMPONENT_VERSION "0.1"

DECLARE_COMPONENT_VERSION(
    "MPEG-H 3D Audio Decoder", COMPONENT_VERSION,
    "Decodes MPEG-H 3D Audio (mha1 / mhm1) tracks in MP4 / M4A files,\n"
    "for example 360 Reality Audio music.\n\n"
    "Settings: Preferences > Advanced > Decoding > MPEG-H 3D Audio decoder.\n\n"
    "Contains the Fraunhofer FDK MPEG-H Software (mpeghdec), mmtisobmff and ilo,\n"
    "(c) Fraunhofer-Gesellschaft zur Foerderung der angewandten Forschung e.V.\n"
    "No patent licence is granted; personal, non-commercial use.");

VALIDATE_COMPONENT_FILENAME("foo_input_mpegh.dll");

// ------------------------------------------------------------------------------------
// Settings (Preferences > Advanced > Decoding > MPEG-H 3D Audio decoder)
// ------------------------------------------------------------------------------------
namespace {

// {0A7027E4-269F-4089-B2CA-B3FF30ED7C6A}
const GUID guid_branch = {0x0a7027e4, 0x269f, 0x4089, {0xb2, 0xca, 0xb3, 0xff, 0x30, 0xed, 0x7c, 0x6a}};
// {26351890-7F8C-4ABA-98FF-C7ADEF7DC736}
const GUID guid_branch_layout = {0x26351890, 0x7f8c, 0x4aba, {0x98, 0xff, 0xc7, 0xad, 0xef, 0x7d, 0xc7, 0x36}};
// {037955A2-1CFB-46D3-AD83-FAC887281ACD}
const GUID guid_layout_stereo = {0x037955a2, 0x1cfb, 0x46d3, {0xad, 0x83, 0xfa, 0xc8, 0x87, 0x28, 0x1a, 0xcd}};
// {D0022DE2-B974-47EF-A93E-28A925527550}
const GUID guid_layout_51 = {0xd0022de2, 0xb974, 0x47ef, {0xa9, 0x3e, 0x28, 0xa9, 0x25, 0x52, 0x75, 0x50}};
// {69A43D3A-CDD2-49A3-B157-F098D328E7CF}
const GUID guid_layout_71 = {0x69a43d3a, 0xcdd2, 0x49a3, {0xb1, 0x57, 0xf0, 0x98, 0xd3, 0x28, 0xe7, 0xcf}};
// {2F5F1FE0-84A1-4ABE-93BA-5A17A81271F1}
const GUID guid_loudness = {0x2f5f1fe0, 0x84a1, 0x4abe, {0x93, 0xba, 0x5a, 0x17, 0xa8, 0x12, 0x71, 0xf1}};
// {EE425EC9-93A6-4035-A53C-4B649636C645}
const GUID guid_drc = {0xee425ec9, 0x93a6, 0x4035, {0xa5, 0x3c, 0x4b, 0x64, 0x96, 0x36, 0xc6, 0x45}};
// {D428FF95-162F-4840-B5BF-C801B6226271}
const GUID guid_input = {0xd428ff95, 0x162f, 0x4840, {0xb5, 0xbf, 0xc8, 0x01, 0xb6, 0x22, 0x62, 0x71}};

advconfig_branch_factory g_branch("MPEG-H 3D Audio decoder", guid_branch,
                                  advconfig_branch::guid_branch_decoding, 0);
advconfig_branch_factory g_branchLayout("Output layout (applies to the next track opened)",
                                        guid_branch_layout, guid_branch, 0);
advconfig_radio_factory g_layoutStereo("Stereo", guid_layout_stereo, guid_branch_layout, 0, true);
advconfig_radio_factory g_layout51("5.1", guid_layout_51, guid_branch_layout, 1, false);
advconfig_radio_factory g_layout71("7.1", guid_layout_71, guid_branch_layout, 2, false);
advconfig_string_factory g_loudness(
    "Target loudness in LKFS, -10 to -31.75 (empty = decoder default, -24)", guid_loudness,
    guid_branch, 1, "");
advconfig_string_factory g_drc(
    "DRC effect: -1 off, 1 night, 2 noisy, 3 limited, 4 low level, 5 dialog, 6 general (empty = default)",
    guid_drc, guid_branch, 2, "");

struct Settings {
  int cicp = 2;            // target loudspeaker layout (CICP index)
  int targetLoudness = -1; // decoder units (-0.25 LU steps, 40..127), -1 = decoder default
  int drcEffect = -100;    // -1..6, -100 = decoder default
};

bool parseNumber(const char* s, double& out) {
  while (*s == ' ' || *s == '\t') s++;
  if (!*s) return false;
  char* end = nullptr;
  out = strtod(s, &end);
  return end != s;
}

#ifdef MPEGH_SELFTEST
// Self-test build (no foobar2000 host): the layout comes from the environment.
Settings loadSettings() {
  Settings s;
  if (const char* e = getenv("MPEGH_CICP")) s.cicp = atoi(e);
  return s;
}
#else
Settings loadSettings() {
  Settings s;
  if (g_layout71.get()) s.cicp = 12;
  else if (g_layout51.get()) s.cicp = 6;
  else s.cicp = 2;

  pfc::string8 v;
  double d = 0;
  g_loudness.get(v);
  if (parseNumber(v.c_str(), d)) {
    int q = static_cast<int>((d < 0 ? -d : d) * 4.0 + 0.5); // "-16" and "16" both mean -16 LKFS
    s.targetLoudness = std::max(40, std::min(127, q));
  }
  g_drc.get(v);
  if (parseNumber(v.c_str(), d)) {
    int n = static_cast<int>(d);
    if (n >= -1 && n <= 6) s.drcEffect = n;
  }
  return s;
}
#endif

int channelsForCicp(int cicp) {
  switch (cicp) {
    case 1: return 1;
    case 2: return 2;
    case 6: return 6;
    case 12: return 8;
    default: return 0;
  }
}

unsigned channelMask(int cicp, int channels) {
  if (cicp == 6 && channels == 6) return audio_chunk::channel_config_5point1;
  if (cicp == 12 && channels == 8) return audio_chunk::channel_config_7point1;
  unsigned m = audio_chunk::g_guess_channel_config(static_cast<unsigned>(channels));
  if (m == 0) m = (channels >= 32) ? 0xFFFFFFFFu : ((1u << channels) - 1u);
  return m;
}

// ------------------------------------------------------------------------------------
// foobar2000 file -> mmtisobmff input adapter
// ------------------------------------------------------------------------------------
struct FileShared {
  service_ptr_t<file> f;
  abort_callback* abort = nullptr;
  uint64_t size = 0;

  size_t readAt(uint64_t at, uint8_t* dst, size_t n) {
    if (at >= size || n == 0) return 0;
    n = static_cast<size_t>(std::min<uint64_t>(n, size - at));
    abort_callback& a = abort ? *abort : fb2k::noAbort;
    f->seek(at, a);
    return f->read(dst, n, a);
  }
};

// While alive, file reads made by the MP4 reader observe the caller's abort_callback.
struct AbortScope {
  FileShared* sh;
  AbortScope(const std::shared_ptr<FileShared>& s, abort_callback& a) : sh(s.get()) {
    if (sh) sh->abort = &a;
  }
  ~AbortScope() {
    if (sh) sh->abort = nullptr;
  }
};

class CFb2kInput : public IIsobmffInput {
 public:
  explicit CFb2kInput(std::shared_ptr<FileShared> sh) : m_sh(std::move(sh)) {}

  size_t read(ilo::ByteBuffer::iterator inBegin, ilo::ByteBuffer::iterator inEnd) override {
    size_t n = static_cast<size_t>(inEnd - inBegin);
    if (n == 0) return 0;
    uint8_t* dst = &(*inBegin);
    size_t done = 0;
    while (done < n) {
      if (m_pos >= m_cachePos && m_pos < m_cachePos + m_cache.size()) {
        size_t off = static_cast<size_t>(m_pos - m_cachePos);
        size_t c = std::min(n - done, m_cache.size() - off);
        memcpy(dst + done, m_cache.data() + off, c);
        done += c;
        m_pos += c;
        continue;
      }
      if (n - done >= kCacheSize) {
        size_t r = m_sh->readAt(m_pos, dst + done, n - done);
        done += r;
        m_pos += r;
        break;
      }
      m_cache.resize(kCacheSize);
      size_t r = m_sh->readAt(m_pos, m_cache.data(), kCacheSize);
      m_cache.resize(r);
      m_cachePos = m_pos;
      if (r == 0) break;
    }
    return done;
  }

  void seek(pos_type pos) override { m_pos = pos; }

  void seek(offset_type offset, SeekingOrigin origin) override {
    int64_t base = 0;
    switch (origin) {
      case SeekingOrigin::beg: base = 0; break;
      case SeekingOrigin::end: base = static_cast<int64_t>(m_sh->size); break;
      case SeekingOrigin::cur: base = static_cast<int64_t>(m_pos); break;
    }
    int64_t p = base + offset;
    m_pos = p < 0 ? 0 : static_cast<uint64_t>(p);
  }

  pos_type tell() override { return m_pos; }
  bool isEOI() override { return m_pos >= m_sh->size; }

  std::unique_ptr<IIsobmffInput> clone() override {
    auto c = ilo::make_unique<CFb2kInput>(m_sh);
    c->m_pos = m_pos;
    return std::unique_ptr<IIsobmffInput>(c.release());
  }

 private:
  static constexpr size_t kCacheSize = 64 * 1024;
  std::shared_ptr<FileShared> m_sh;
  uint64_t m_pos = 0;
  std::vector<uint8_t> m_cache;
  uint64_t m_cachePos = 0;
};

// ------------------------------------------------------------------------------------
// Quick MP4 inspection: is there an MPEG-H track? Also collects iTunes-style tags.
// ------------------------------------------------------------------------------------
inline uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline uint64_t be64(const uint8_t* p) { return (uint64_t(be32(p)) << 32) | be32(p + 4); }

struct Tag {
  const char* field;
  std::string value;
};
typedef std::vector<Tag> Tags;

std::string textValue(const uint8_t* p, size_t n) {
  std::string s(reinterpret_cast<const char*>(p), n);
  while (!s.empty() && s.back() == '\0') s.pop_back();
  return s;
}

// Finds a child box of the given type inside [p, p+n). Returns payload pointer/size.
bool findBox(const uint8_t* p, size_t n, const char* type, const uint8_t** payload,
             size_t* payloadSize) {
  size_t off = 0;
  while (off + 8 <= n) {
    uint64_t sz = be32(p + off);
    size_t hdr = 8;
    if (sz == 1) {
      if (off + 16 > n) return false;
      sz = be64(p + off + 8);
      hdr = 16;
    } else if (sz == 0) {
      sz = n - off;
    }
    if (sz < hdr || sz > n - off) return false;
    if (memcmp(p + off + 4, type, 4) == 0) {
      *payload = p + off + hdr;
      *payloadSize = static_cast<size_t>(sz) - hdr;
      return true;
    }
    off += static_cast<size_t>(sz);
  }
  return false;
}

void parseTags(const uint8_t* moov, size_t n, Tags& t) {
  const uint8_t *udta, *meta, *ilst;
  size_t udtaN, metaN, ilstN;
  if (!findBox(moov, n, "udta", &udta, &udtaN)) return;
  if (!findBox(udta, udtaN, "meta", &meta, &metaN) || metaN < 4) return;
  if (!findBox(meta + 4, metaN - 4, "ilst", &ilst, &ilstN)) return;

  static const struct {
    const char* atom;
    const char* field;
  } kText[] = {
      {"\xA9nam", "title"},   {"\xA9" "ART", "artist"}, {"\xA9" "alb", "album"},
      {"aART", "album artist"}, {"\xA9" "day", "date"},  {"\xA9gen", "genre"},
      {"\xA9wrt", "composer"}, {"\xA9" "cmt", "comment"},
  };

  size_t off = 0;
  while (off + 8 <= ilstN) {
    uint32_t sz = be32(ilst + off);
    if (sz < 8 || sz > ilstN - off) break;
    const uint8_t* name = ilst + off + 4;
    const uint8_t* data;
    size_t dataN;
    if (findBox(ilst + off + 8, sz - 8, "data", &data, &dataN) && dataN >= 8) {
      const uint8_t* v = data + 8;
      size_t vn = dataN - 8;
      bool done = false;
      for (const auto& k : kText) {
        if (memcmp(name, k.atom, 4) == 0) {
          std::string s = textValue(v, vn);
          if (!s.empty()) t.push_back(Tag{k.field, s});
          done = true;
          break;
        }
      }
      if (!done && vn >= 6 && (memcmp(name, "trkn", 4) == 0 || memcmp(name, "disk", 4) == 0)) {
        bool track = name[0] == 't';
        int a = (v[2] << 8) | v[3], b = (v[4] << 8) | v[5];
        if (a) t.push_back(Tag{track ? "tracknumber" : "discnumber", std::to_string(a)});
        if (b) t.push_back(Tag{track ? "totaltracks" : "totaldiscs", std::to_string(b)});
      }
    }
    off += sz;
  }
}

bool containsFourcc(const std::vector<uint8_t>& buf, const char* fcc) {
  if (buf.size() < 4) return false;
  const uint8_t* p = buf.data();
  const uint8_t* end = p + buf.size() - 3;
  while (p < end) {
    p = static_cast<const uint8_t*>(memchr(p, fcc[0], static_cast<size_t>(end - p)));
    if (!p) return false;
    if (memcmp(p, fcc, 4) == 0) return true;
    p++;
  }
  return false;
}

// Returns true if the file looks like an MP4 with an MPEG-H sample entry.
bool probeMpegh(FileShared& sh, Tags& tags) {
  uint8_t hdr[16];
  if (sh.readAt(0, hdr, 12) != 12) return false;
  if (memcmp(hdr + 4, "ftyp", 4) != 0 && memcmp(hdr + 4, "styp", 4) != 0) return false;

  uint64_t off = 0;
  for (int i = 0; i < 4096 && off + 8 <= sh.size; i++) {
    if (sh.readAt(off, hdr, 8) != 8) return false;
    uint64_t sz = be32(hdr);
    uint64_t hsz = 8;
    if (sz == 1) {
      if (sh.readAt(off + 8, hdr + 8, 8) != 8) return false;
      sz = be64(hdr + 8);
      hsz = 16;
    } else if (sz == 0) {
      sz = sh.size - off;
    }
    if (sz < hsz || sz > sh.size - off) return false;

    if (memcmp(hdr + 4, "moov", 4) == 0) {
      uint64_t payload = sz - hsz;
      if (payload > (256u << 20)) return true; // unusually large index: let the real parser decide
      std::vector<uint8_t> moov(static_cast<size_t>(payload));
      if (sh.readAt(off + hsz, moov.data(), moov.size()) != moov.size()) return false;
      bool ok = containsFourcc(moov, "mha1") || containsFourcc(moov, "mhm1") ||
                containsFourcc(moov, "mha2") || containsFourcc(moov, "mhm2");
      if (ok) parseTags(moov.data(), moov.size(), tags);
      return ok;
    }
    off += sz;
  }
  return false;
}

const int kMaxChannels = 24;
const int kMaxFrame = 3072;

[[noreturn]] void fail(const char* what) {
  pfc::string8 msg("MPEG-H decoder: ");
  msg += what;
#ifdef MPEGH_SELFTEST
  fprintf(stderr, "%s\n", msg.c_str());
#else
  console::print(msg.c_str());
#endif
  throw exception_io_data();
}

// Runs fn(); errors raised by the MP4 reader / decoder libraries become foobar2000 I/O errors,
// while foobar2000's own exceptions (abort, I/O) pass through unchanged. (On Windows
// pfc::exception is std::exception, so the foobar2000 types are caught by name.)
template <typename F>
auto guarded(abort_callback& abort, F&& fn) -> decltype(fn()) {
  try {
    return fn();
  } catch (exception_aborted const&) {
    throw;
  } catch (exception_io const&) {
    throw;
  } catch (std::exception const& e) {
    abort.check();
    fail(e.what());
  }
}

// ------------------------------------------------------------------------------------
// The input
// ------------------------------------------------------------------------------------
class input_mpegh : public input_stubs {
 public:
  input_mpegh() {}
  ~input_mpegh() {
    if (m_dec) mpeghdecoder_destroy(m_dec);
    m_track.reset();
    m_reader.reset();
  }

  void open(service_ptr_t<file> p_filehint, const char* p_path, t_input_open_reason p_reason,
            abort_callback& p_abort) {
    if (p_reason == input_open_info_write) throw exception_tagging_unsupported();

    service_ptr_t<file> f = p_filehint;
    input_open_file_helper(f, p_path, p_reason, p_abort);

    m_shared = std::make_shared<FileShared>();
    m_shared->f = f;
    m_shared->size = f->get_size_ex(p_abort);
    AbortScope scope(m_shared, p_abort);

    if (!probeMpegh(*m_shared, m_tags)) throw exception_io_unsupported_format();

    guarded(p_abort, [&] {
      m_reader = ilo::make_unique<CIsobmffReader>(
          std::unique_ptr<IIsobmffInput>(new CFb2kInput(m_shared)));

      for (const auto& ti : m_reader->trackInfos()) {
        if (ti.codec != Codec::mpegh_mha && ti.codec != Codec::mpegh_mhm) continue;
        m_track = m_reader->trackByIndex<CMpeghTrackReader>(ti.trackIndex);
        if (!m_track) continue;
        m_info = ti;
        break;
      }
      if (!m_track) throw exception_io_unsupported_format();

      m_duration = m_info.timescale ? static_cast<double>(m_info.duration) / m_info.timescale : 0.0;
      m_sampleRate = static_cast<int>(m_track->sampleRate());
    });
    if (m_sampleRate <= 0) m_sampleRate = 48000;
    m_settings = loadSettings();
  }

  void get_info(file_info& p_info, abort_callback&) {
    p_info.set_length(m_duration);
    p_info.info_set_int("samplerate", m_sampleRate);
    int ch = m_channels > 0 ? m_channels : channelsForCicp(m_settings.cicp);
    if (ch > 0) p_info.info_set_int("channels", ch);
    if (m_duration > 0.0 && m_shared) {
      t_int64 kbps = static_cast<t_int64>(m_shared->size * 8.0 / m_duration / 1000.0 + 0.5);
      if (kbps > 0) p_info.info_set_bitrate(kbps);
    }
    p_info.info_set("codec", "MPEG-H 3D Audio");
    p_info.info_set("encoding", "lossy");
    for (const auto& t : m_tags) p_info.meta_add(t.field, t.value.c_str());
  }

  t_filestats2 get_stats2(uint32_t f, abort_callback& a) { return m_shared->f->get_stats2_(f, a); }

  void decode_initialize(unsigned, abort_callback& p_abort) {
    AbortScope scope(m_shared, p_abort);
    guarded(p_abort, [&] {
      if (m_dec) {
        mpeghdecoder_destroy(m_dec);
        m_dec = nullptr;
      }
      m_settings = loadSettings();
      m_dec = mpeghdecoder_init(m_settings.cicp);
      if (!m_dec && m_settings.cicp != 2) {
        m_settings.cicp = 2; // layout not supported by the decoder: fall back to stereo
        m_dec = mpeghdecoder_init(2);
      }
      if (!m_dec) fail("MPEG-H decoder could not be created");

      if (m_settings.targetLoudness >= 0)
        mpeghdecoder_setParam(m_dec, MPEGH_DEC_PARAM_TARGET_REFERENCE_LEVEL, m_settings.targetLoudness);
      if (m_settings.drcEffect != -100)
        mpeghdecoder_setParam(m_dec, MPEGH_DEC_PARAM_EFFECT_TYPE, m_settings.drcEffect);

      if (m_info.codec == Codec::mpegh_mha) {
        auto dcr = m_track->mhaDecoderConfigRecord();
        if (!dcr || dcr->mpegh3daConfig().empty())
          fail("MPEG-H decoder configuration is missing");
        if (mpeghdecoder_setMhaConfig(m_dec, dcr->mpegh3daConfig().data(),
                                      static_cast<uint32_t>(dcr->mpegh3daConfig().size())) != MPEGH_DEC_OK)
          fail("MPEG-H decoder rejected the stream configuration");
      }

      m_sample = CSample{m_info.maxSampleSize};
      m_out.resize(static_cast<size_t>(kMaxChannels) * kMaxFrame);
      m_channels = 0;
      m_eof = false;
      m_flushed = false;

      // Start from the first sample and decode one frame so that the real output format is known.
      if (!m_started) {
        m_started = true;
        m_sampleInfo = m_track->nextSample(m_sample);
        m_haveSample = !m_sample.empty();
        decodeFrame();
      } else {
        seekTo(0);
      }
      if (m_pending == 0 || m_channels <= 0)
        fail("MPEG-H stream could not be decoded");
    });
  }

  bool decode_run(audio_chunk& p_chunk, abort_callback& p_abort) {
    AbortScope scope(m_shared, p_abort);
    return guarded(p_abort, [&]() -> bool {
      p_abort.check();
      if (m_pending == 0 && !decodeFrame()) return false;
      p_chunk.set_data_fixedpoint(m_out.data() + static_cast<size_t>(m_pendingOff) * m_channels,
                                  static_cast<t_size>(m_pending) * m_channels * 4,
                                  static_cast<unsigned>(m_outRate),
                                  static_cast<unsigned>(m_channels), 32,
                                  channelMask(m_settings.cicp, m_channels));
      m_pending = 0;
      m_pendingOff = 0;
      return true;
    });
  }

  void decode_seek(double p_seconds, abort_callback& p_abort) {
    AbortScope scope(m_shared, p_abort);
    guarded(p_abort, [&] {
      if (p_seconds < 0) p_seconds = 0;
      seekTo(static_cast<int64_t>(p_seconds * m_outRate + 0.5));
    });
  }

  bool decode_can_seek() { return true; }

  void retag(const file_info&, abort_callback&) { throw exception_tagging_unsupported(); }
  void remove_tags(abort_callback&) { throw exception_tagging_unsupported(); }

  static bool g_is_our_content_type(const char*) { return false; }
  static bool g_is_our_path(const char*, const char* p_extension) {
    return stricmp_utf8(p_extension, "mp4") == 0 || stricmp_utf8(p_extension, "m4a") == 0;
  }
  static GUID g_get_guid() { return guid_input; }
  static const char* g_get_name() { return "MPEG-H 3D Audio Decoder"; }

 private:
  // Positions the stream at the given output frame (sample rate ticks) and leaves the PCM that
  // starts there in m_out.
  void seekTo(int64_t target) {
    if (target < 0) target = 0;
    const int rate = m_outRate > 0 ? m_outRate : m_sampleRate;
    uint64_t ts = static_cast<uint64_t>(static_cast<double>(target) / rate * m_info.timescale);

    // Prefer the last sync sample at or before the target, then decode forward to it.
    bool found = false;
    if (target > 0) {
      try {
        SSeekConfig cfg(CTimeDuration(m_info.timescale, ts + 1),
                        ESampleSeekMode::lastSyncSampleSmaller);
        m_sampleInfo = m_track->sampleByTimestamp(cfg, m_sample);
        found = !m_sample.empty();
      } catch (exception_aborted const&) {
        throw;
      } catch (exception_io const&) {
        throw;
      } catch (std::exception const&) {
        found = false;
      }
    }
    if (!found) {
      SSeekConfig cfg(CTimeDuration(m_info.timescale, ts), ESampleSeekMode::nearestSyncSample);
      m_sampleInfo = m_track->sampleByTimestamp(cfg, m_sample);
    }
    m_haveSample = !m_sample.empty();
    m_flushed = false;
    m_eof = false;
    m_pending = 0;
    m_pendingOff = 0;
    mpeghdecoder_flush(m_dec);

    if (!m_haveSample) {
      m_eof = true;
      return;
    }
    // Decode forward to the exact position (the sync sample lies at or before the target).
    for (int guard = 0; guard < 4096; guard++) {
      if (!decodeFrame()) break;
      int64_t start = static_cast<int64_t>(m_lastTicks);
      int64_t end = start + m_pending;
      if (start >= target || end > target) {
        if (start < target) {
          int skip = static_cast<int>(target - start);
          m_pendingOff = skip;
          m_pending -= skip;
        }
        break;
      }
      m_pending = 0;
      m_pendingOff = 0;
    }
  }

  // Produces the next block of PCM in m_out (m_pending frames). Returns false at end of stream.
  bool decodeFrame() {
    m_pending = 0;
    m_pendingOff = 0;
    int errors = 0;
    while (!m_eof) {
      MPEGH_DECODER_OUTPUT_INFO oi;
      MPEGH_DECODER_ERROR st =
          mpeghdecoder_getSamples(m_dec, m_out.data(), static_cast<uint32_t>(m_out.size()), &oi);
      if (st == MPEGH_DEC_OK) {
        if (oi.numChannels <= 0 || oi.numSamplesPerChannel <= 0) continue;
        if (m_channels == 0) {
          m_channels = oi.numChannels;
          m_outRate = oi.sampleRate;
        } else if (oi.numChannels != m_channels || oi.sampleRate != m_outRate) {
          m_eof = true; // output format changed mid-stream: not representable, stop cleanly
          return false;
        }
        m_lastTicks = oi.ticks;
        m_pending = oi.numSamplesPerChannel;
        reorder(m_pending);
        return true;
      }
      if (st != MPEGH_DEC_FEED_DATA) {
        m_eof = true;
        return false;
      }
      // decoder wants more input
      if (m_haveSample) {
        MPEGH_DECODER_ERROR e = mpeghdecoder_processTimescale(
            m_dec, m_sample.rawData.data(), static_cast<uint32_t>(m_sample.rawData.size()),
            m_sampleInfo.timestamp.ptsValue(), m_sampleInfo.timestamp.timescale());
        if (e != MPEGH_DEC_OK && ++errors > 16) {
          m_eof = true;
          return false;
        }
        m_sampleInfo = m_track->nextSample(m_sample);
        m_haveSample = !m_sample.empty();
      } else if (!m_flushed) {
        m_flushed = true;
        if (mpeghdecoder_flushAndGet(m_dec) != MPEGH_DEC_OK) {
          m_eof = true;
          return false;
        }
      } else {
        m_eof = true;
        return false;
      }
    }
    return false;
  }

  // MPEG 7.1 (CICP 12) is L R C LFE Ls Rs Lb Rb; Windows expects L R C LFE Lb Rb Ls Rs.
  void reorder(int frames) {
    if (m_settings.cicp != 12 || m_channels != 8) return;
    int32_t* p = m_out.data();
    for (int i = 0; i < frames; i++, p += 8) {
      std::swap(p[4], p[6]);
      std::swap(p[5], p[7]);
    }
  }

  Settings m_settings;
  Tags m_tags;
  std::shared_ptr<FileShared> m_shared;
  std::unique_ptr<CIsobmffReader> m_reader;
  std::unique_ptr<CMpeghTrackReader> m_track;
  CTrackInfo m_info;
  HANDLE_MPEGH_DECODER_CONTEXT m_dec = nullptr;

  CSample m_sample;
  SSampleExtraInfo m_sampleInfo;
  bool m_haveSample = false;
  bool m_flushed = false;
  bool m_eof = false;
  bool m_started = false;

  std::vector<int32_t> m_out;
  int m_pending = 0;    // frames available in m_out
  int m_pendingOff = 0; // frames already consumed from m_out
  uint64_t m_lastTicks = 0;

  int m_channels = 0;   // decoded channel count (0 until the first frame is decoded)
  int m_outRate = 0;    // decoded sample rate
  int m_sampleRate = 0; // sample rate from the MP4 sample entry
  double m_duration = 0.0;
};

input_singletrack_factory_t<input_mpegh> g_input_mpegh_factory;

} // namespace
