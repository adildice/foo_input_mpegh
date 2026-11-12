// Self-test for foo_input_mpegh: drives the input class directly, without a foobar2000 host.
//
//   selftest in.mp4 out.wav            decode everything to a 32-bit float WAV
//   selftest in.mp4 --seek             decode everything, then check seeking against that decode
//   selftest in.mp4 --reject           expect the file to be rejected as "not MPEG-H"
//
// The output layout is taken from the MPEGH_CICP environment variable (default 2 = stereo).
#define MPEGH_SELFTEST
#include "../src/foo_input_mpegh.cpp"

#include <cmath>
#include <cstdio>

namespace {

class mem_file : public file_readonly_t<file_v2> {
 public:
  explicit mem_file(std::vector<uint8_t> d) : m_data(std::move(d)) {}
  t_size read(void* p, t_size n, abort_callback&) override {
    t_size left = m_data.size() - m_pos;
    if (n > left) n = left;
    memcpy(p, m_data.data() + m_pos, n);
    m_pos += n;
    return n;
  }
  t_filesize get_size(abort_callback&) override { return m_data.size(); }
  t_filesize get_position(abort_callback&) override { return m_pos; }
  void seek(t_filesize pos, abort_callback&) override {
    if (pos > m_data.size()) throw exception_io_seek_out_of_range();
    m_pos = static_cast<size_t>(pos);
  }
  void reopen(abort_callback& a) override { seek(0, a); }
  bool can_seek() override { return true; }
  bool get_content_type(pfc::string_base&) override { return false; }
  bool is_remote() override { return false; }
  t_filetimestamp get_timestamp(abort_callback&) override { return filetimestamp_invalid; }
  t_filestats2 get_stats2(uint32_t, abort_callback&) override {
    t_filestats2 r;
    r.m_size = m_data.size();
    return r;
  }

 private:
  std::vector<uint8_t> m_data;
  size_t m_pos = 0;
};

bool loadFile(const char* path, std::vector<uint8_t>& out) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize(static_cast<size_t>(n));
  bool ok = fread(out.data(), 1, out.size(), f) == out.size();
  fclose(f);
  return ok;
}

void put32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
void put16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }

void writeWav(const char* path, const std::vector<float>& pcm, unsigned ch, unsigned rate) {
  FILE* f = fopen(path, "wb");
  if (!f) return;
  uint32_t bytes = static_cast<uint32_t>(pcm.size() * 4);
  fwrite("RIFF", 4, 1, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 8, 1, f);
  put32(f, 16); put16(f, 3); put16(f, static_cast<uint16_t>(ch)); put32(f, rate);
  put32(f, rate * ch * 4); put16(f, static_cast<uint16_t>(ch * 4)); put16(f, 32);
  fwrite("data", 4, 1, f); put32(f, bytes);
  fwrite(pcm.data(), 4, pcm.size(), f);
  fclose(f);
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: selftest in.mp4 (out.wav | --seek | --reject)\n");
    return 2;
  }
  std::vector<uint8_t> data;
  if (!loadFile(argv[1], data)) {
    fprintf(stderr, "cannot read %s\n", argv[1]);
    return 2;
  }
  const std::string mode = argv[2];
  abort_callback_dummy abort;

  auto openInput = [&](input_mpegh& in) {
    service_ptr_t<file> f = new service_impl_t<mem_file>(data);
    in.open(f, argv[1], input_open_decode, abort);
  };

  try {
    if (mode == "--reject") {
      try {
        input_mpegh in;
        openInput(in);
      } catch (exception_io_unsupported_format const&) {
        printf("REJECT OK\n");
        return 0;
      }
      printf("FAIL: file was accepted\n");
      return 1;
    }

    input_mpegh in;
    openInput(in);
    file_info_impl info;
    in.get_info(info, abort);
    printf("length %.3f s, samplerate %s, channels %s, bitrate %s, codec %s\n", info.get_length(),
           info.info_get("samplerate"), info.info_get("channels") ? info.info_get("channels") : "?",
           info.info_get("bitrate") ? info.info_get("bitrate") : "?", info.info_get("codec"));
    for (t_size i = 0; i < info.meta_get_count(); i++)
      printf("tag %s = %s\n", info.meta_enum_name(i), info.meta_enum_value(i, 0));

    in.decode_initialize(0, abort);
    std::vector<float> pcm;
    unsigned ch = 0, rate = 0, mask = 0;
    audio_chunk_impl chunk;
    while (in.decode_run(chunk, abort)) {
      if (ch == 0) {
        ch = chunk.get_channel_count();
        rate = chunk.get_sample_rate();
        mask = chunk.get_channel_config();
      } else if (ch != chunk.get_channel_count() || rate != chunk.get_sample_rate()) {
        printf("FAIL: format changed\n");
        return 1;
      }
      const audio_sample* p = chunk.get_data();
      size_t n = chunk.get_sample_count() * ch;
      for (size_t i = 0; i < n; i++) pcm.push_back(static_cast<float>(p[i]));
    }
    if (ch == 0) {
      printf("FAIL: nothing decoded\n");
      return 1;
    }
    size_t frames = pcm.size() / ch;
    printf("decoded %zu frames, %u ch, %u Hz, mask 0x%x, %.3f s\n", frames, ch, rate, mask,
           static_cast<double>(frames) / rate);

    if (mode != "--seek") {
      writeWav(argv[2], pcm, ch, rate);
      printf("DECODE OK\n");
      return 0;
    }

    // Seek test: after seeking to t, the decoder must continue at frame t * rate of the straight
    // decode. The decoder restarts from a sync frame, so levels may differ very slightly; a wrong
    // position would show up as a difference orders of magnitude larger than the tolerance.
    const double total = static_cast<double>(frames) / rate;
    const double points[] = {total * 0.5, total * 0.1, total * 0.9, 0.0, total * 0.333, 0.777};
    int bad = 0;
    for (double t : points) {
      if (t >= total) continue;
      in.decode_seek(t, abort);
      size_t at = static_cast<size_t>(t * rate + 0.5);
      size_t got = 0;
      double maxDiff = 0;
      while (got < 4096 && in.decode_run(chunk, abort)) {
        const audio_sample* p = chunk.get_data();
        size_t n = chunk.get_sample_count();
        for (size_t i = 0; i < n && at + got < frames; i++, got++)
          for (unsigned c = 0; c < ch; c++)
            maxDiff = std::max(maxDiff, std::fabs(static_cast<double>(p[i * ch + c]) -
                                                  pcm[(at + got) * ch + c]));
      }
      bool ok = got > 0 && maxDiff < 5e-3;
      printf("seek %.3f s: compared %zu frames, max diff %.2e %s\n", t, got, maxDiff,
             ok ? "ok" : "MISMATCH");
      if (!ok) bad++;
    }
    // Seeking past the end must simply end the stream.
    in.decode_seek(total + 5.0, abort);
    if (in.decode_run(chunk, abort) && chunk.get_sample_count() > static_cast<t_size>(rate)) {
      printf("FAIL: data after seeking past the end\n");
      bad++;
    }
    printf(bad ? "SEEK FAIL\n" : "SEEK OK\n");
    return bad ? 1 : 0;
  } catch (std::exception const& e) {
    printf("FAIL: exception: %s\n", e.what());
    return 1;
  }
}
