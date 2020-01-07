/*-
 * Copyright (c) 2015 Chris Spiegel
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <cctype>
#include <cstdlib>
#include <exception>
#include <map>
#include <string>

#include <audacious/audtag.h>
#include <libaudcore/i18n.h>
#include <libaudcore/plugin.h>

#include <OptimFROG/OptimFROG.h>

class OFR
{
  public:
    class InvalidFile : public std::exception
    {
      public:
        InvalidFile() : std::exception() { }
    };

    explicit OFR(VFSFile &file) : decoder(OptimFROG_createInstance())
    {
      static ReadInterface rint =
      {
        ofr_close,
        ofr_read,
        ofr_eof,
        ofr_seekable,
        ofr_length,
        ofr_get_pos,
        ofr_seek,
      };
      OptimFROG_Tags ofr_tags;

      if(decoder == C_NULL) throw InvalidFile();

      if(!OptimFROG_openExt(decoder, &rint, &file, C_TRUE))
      {
        OptimFROG_destroyInstance(decoder);
        throw InvalidFile();
      }

      OptimFROG_getInfo(decoder, &info);

      /* 24- and 32-bit audio is converted to 16-bit. */
      if(info.bitspersample > 16) info.bitspersample = 16;
      std::map<std::string, int> formats =
      {
        { "UINT8", FMT_U8 }, { "UINT16", FMT_U16_LE }, { "UINT24", FMT_U16_LE }, { "UINT32", FMT_U16_LE },
        { "SINT8", FMT_S8 }, { "SINT16", FMT_S16_LE }, { "SINT24", FMT_S16_LE }, { "SINT32", FMT_S16_LE },
      };

      try
      {
        format_ = formats.at(std::string(info.sampleType));
      }
      catch(const std::out_of_range &)
      {
        OptimFROG_close(decoder);
        OptimFROG_destroyInstance(decoder);
        throw InvalidFile();
      }

      OptimFROG_getTags(decoder, &ofr_tags);
      for(uInt32_t i = 0; i < ofr_tags.keyCount; i++)
      {
        std::string key;

        /* Ensure all keys are lowercase for easy comparison. */
        for(std::size_t j = 0; ofr_tags.keys[i][j] != 0; j++)
        {
          key.push_back(std::tolower(static_cast<unsigned char>(ofr_tags.keys[i][j])));
        }

        tags.insert(std::pair<std::string, std::string>(key, std::string(ofr_tags.values[i])));
      }
      OptimFROG_freeTags(&ofr_tags);
    }

    OFR(const OFR &) = delete;
    OFR &operator=(const OFR &) = delete;

    ~OFR()
    {
      OptimFROG_close(decoder);
      OptimFROG_destroyInstance(decoder);
    }

    sInt32_t read(void *buf, size_t bufsiz)
    {
      sInt32_t point_conversion = (info.bitspersample / 8) * info.channels;
      sInt32_t n;

      n = OptimFROG_read(decoder, buf, bufsiz / point_conversion, C_TRUE);

      return n > 0 ? n * point_conversion : 0;
    }

    void seek(int pos)
    {
      if(OptimFROG_seekable(decoder)) OptimFROG_seekTime(decoder, pos);
    }

    long format() { return format_; }
    long rate() { return info.samplerate; }
    long channels() { return info.channels; }
    long length() { return info.length_ms; }
    long bitrate() { return info.bitrate; }

    const std::string get_tag(std::string tag) { return tags.at(tag); }

  private:
    void *decoder;
    OptimFROG_Info info;

    int format_;

    std::map<std::string, std::string> tags;

    static VFSFile *VFS(void *instance) { return reinterpret_cast<VFSFile *>(instance); }

    static condition_t ofr_close(void *instance) { return C_TRUE; }
    static sInt32_t ofr_read(void *instance, void *buf, uInt32_t n) { return VFS(instance)->fread(buf, 1, n); }
    static condition_t ofr_eof(void* instance) { return C_FALSE; }
    static condition_t ofr_seekable(void* instance) { return C_TRUE; }
    static sInt64_t ofr_length(void* instance) { return VFS(instance)->fsize(); }
    static sInt64_t ofr_get_pos(void* instance) { return VFS(instance)->ftell(); }
    static condition_t ofr_seek(void* instance, sInt64_t offset) { return VFS(instance)->fseek(offset, VFS_SEEK_SET) >= 0; }
};

class OFRPlugin : public InputPlugin
{
  public:
    static const char about[];
    static const char *const exts[];

    static constexpr PluginInfo info =
    {
      N_("OptimFROG decoder"),
      PACKAGE,
      about,
    };

    static constexpr auto iinfo = InputInfo(FlagWritesTag).with_exts(exts);

    constexpr OFRPlugin() : InputPlugin(info, iinfo) { }

    bool is_our_file(const char *filename, VFSFile &file)
    {
      try
      {
        OFR ofr(file);
        return true;
      }
      catch(const OFR::InvalidFile &)
      {
        return false;
      }
    }

    bool read_tag(const char *filename, VFSFile &file, Tuple &tuple, Index<char> *)
    {
      try
      {
        OFR ofr(file);

        tuple.set_int(Tuple::Length, ofr.length());
        tuple.set_filename(filename);
        tuple.set_format("OptimFROG", ofr.channels(), ofr.rate(), ofr.bitrate());

        set_tag(ofr, tuple, Tuple::Title, "title", false);
        set_tag(ofr, tuple, Tuple::Artist, "artist", false);
        set_tag(ofr, tuple, Tuple::Album, "album", false);
        set_tag(ofr, tuple, Tuple::Comment, "comment", false);
        set_tag(ofr, tuple, Tuple::Genre, "genre", false);
        set_tag(ofr, tuple, Tuple::Composer, "composer", false);
        set_tag(ofr, tuple, Tuple::Year, "year", true);
        set_tag(ofr, tuple, Tuple::Track, "track", true);

        return true;
      }
      catch(const OFR::InvalidFile &)
      {
        return false;
      }
    }

    bool write_tuple(const char *filename, VFSFile &file, const Tuple &tuple)
    {
      return audtag::write_tuple(file, tuple, audtag::TagType::APE);
    }

    bool play(const char *filename, VFSFile &file)
    {
      try
      {
        OFR ofr(file);

        open_audio(ofr.format(), ofr.rate(), ofr.channels());

        while(!check_stop())
        {
          unsigned char buf[16384];
          sInt32_t n;
          int seek_value = check_seek();

          if(seek_value >= 0) ofr.seek(seek_value);

          n = ofr.read(buf, sizeof buf);
          if(n == 0) break;

          write_audio(buf, n);
        }
      }
      catch(const OFR::InvalidFile &)
      {
        return false;
      }

      return true;
    }

  private:
    void set_tag(OFR &ofr, Tuple &tuple, Tuple::Field field, std::string key, bool is_int)
    {
      try
      {
        std::string result = ofr.get_tag(key);

        if(is_int)
        {
          tuple.set_int(field, std::stoi(result));
        }
        else
        {
          tuple.set_str(field, result.c_str());
        }
      }
      catch(const std::out_of_range &)
      {
        /* for ofr.get_tag() */
      }
      catch(const std::invalid_argument &)
      {
        /* for std::stoi() */
      }
    }
};

OFRPlugin aud_plugin_instance;

const char OFRPlugin::about[] = N_("Written by: Chris Spiegel <cspiegel@gmail.com>");

const char *const OFRPlugin::exts[] = { "ofr", "ofs", nullptr };
